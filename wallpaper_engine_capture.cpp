#include "wallpaper_engine_capture.h"
#include "video_frame_image.h"
#include "native_window_capture.h"

#include <QCoreApplication>
#include <QCapturableWindow>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QProcess>
#include <QSaveFile>
#include <QPoint>
#include <QPointer>
#include <QPromise>
#include <QThreadPool>
#include <QThread>
#include <QTemporaryDir>
#include <QUuid>
#include <QVideoSink>
#include <QWindowCapture>
#include <QWidget>
#include <QOperatingSystemVersion>
#include <QSysInfo>
#include <QJsonArray>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
int frameRateFromConfiguration(const QJsonObject &configuration, const QString &user,
                               QString *error)
{
    const QJsonValue value = configuration.value(user).toObject()
        .value(QStringLiteral("general")).toObject()
        .value(QStringLiteral("user")).toObject().value(QStringLiteral("fps"));
    const double rate = value.toDouble(-1);
    if (user.isEmpty() || !value.isDouble() || !std::isfinite(rate)
        || rate < 1 || rate > 10000 || std::floor(rate) != rate) {
        if (error)
            *error = QStringLiteral("未能读取当前用户的 Wallpaper Engine 画面帧率，请在其设置中确认。");
        return 0;
    }
    if (error) error->clear();
    return int(rate);
}

QPoint rendererParkingPosition()
{
#ifdef Q_OS_WIN
    // These are virtual-desktop coordinates, including monitors left of (0,0).
    // WGC captures the window's own surface even when it is wholly offscreen.
    return QPoint(GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 256,
                  GetSystemMetrics(SM_YVIRTUALSCREEN));
#else
    return QPoint(32767, 32767);
#endif
}

#ifdef Q_OS_WIN
QPoint rendererCapturePosition(quintptr owner)
{
    const HWND window = reinterpret_cast<HWND>(owner);
    const HMONITOR monitor = window && IsWindow(window)
        ? MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(monitor, &info))
        return QPoint(info.rcMonitor.left, info.rcMonitor.top);
    return QPoint(0, 0);
}

bool isNamedRenderer(HWND window, const QString &name)
{
    if (!window || !IsWindow(window) || !name.startsWith(QStringLiteral("PHSRadioWallpaper_")))
        return false;
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    return QString::fromWCharArray(title) == name;
}

bool rendererIsUsable(HWND window, const QString &name, QSize *size = nullptr)
{
    RECT bounds{};
    if (!isNamedRenderer(window, name) || !IsWindowVisible(window) || IsIconic(window)
        || !GetClientRect(window, &bounds) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return false;
    if (size)
        *size = QSize(bounds.right - bounds.left, bounds.bottom - bounds.top);
    return true;
}

bool rendererIsOutsideDesktop(HWND window)
{
    RECT bounds{};
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0 || !GetWindowRect(window, &bounds))
        return false;
    RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    desktop.right = desktop.left + width;
    desktop.bottom = desktop.top + height;
    RECT overlap{};
    return !IntersectRect(&overlap, &bounds, &desktop);
}

bool setRendererOpacity(HWND window, BYTE desiredAlpha)
{
    COLORREF key{};
    BYTE alpha{};
    DWORD flags{};
    if (GetLayeredWindowAttributes(window, &key, &alpha, &flags)
        && alpha == desiredAlpha && (flags & LWA_ALPHA))
        return true;
    if (SetLayeredWindowAttributes(window, 0, desiredAlpha, LWA_ALPHA))
        return true;
    ShowWindow(window, SW_HIDE);
    return false;
}

bool resizeRenderer(HWND window, const QSize &size)
{
    RECT client{};
    if (GetClientRect(window, &client)) {
        if (client.right == size.width() && client.bottom == size.height())
            return true;
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        RECT desired{0, 0, size.width(), size.height()};
        if (AdjustWindowRectEx(&desired, style, FALSE, exStyle)
            && SetWindowPos(window, nullptr, 0, 0, desired.right - desired.left,
                desired.bottom - desired.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            return true;
    }
    ShowWindow(window, SW_HIDE);
    return false;
}
#endif

void removeMutedProject(const QString &directory)
{
    const QFileInfo info(directory);
    // No recursive removal: the private directory contains only our descriptor.
    if (directory.isEmpty() || !info.fileName().startsWith(QStringLiteral("PHSRadioWallpaper-"))
        || QDir::cleanPath(info.absolutePath()) != QDir::cleanPath(QDir::tempPath()))
        return;
    QFile::remove(QDir(directory).filePath(QStringLiteral("project.json")));
    QDir().rmdir(directory);
}

QString mutedProject(const QString &sourcePath, QString &directory, QString &error)
{
    const QFileInfo source(sourcePath);
    const QDir sourceDirectory(source.absolutePath());
    QJsonObject project;
    const bool descriptor = source.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0;
    QFile file(descriptor ? source.absoluteFilePath() : sourceDirectory.filePath(QStringLiteral("project.json")));
    if (file.open(QIODevice::ReadOnly)) {
        if (file.size() > 4 * 1024 * 1024) {
            error = QStringLiteral("壁纸工程描述文件过大，无法安全设置加载前静音。"); return {};
        }
        QJsonParseError parse{};
        const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            error = QStringLiteral("无法解析壁纸工程描述，未打开未静音的背景。"); return {};
        }
        project = document.object();
    } else if (descriptor) {
        error = QStringLiteral("无法读取壁纸工程描述，未打开未静音的背景。"); return {};
    }
    QString asset = descriptor ? project.value(QStringLiteral("file")).toString() : source.absoluteFilePath();
    if (QDir::isRelativePath(asset)) asset = sourceDirectory.absoluteFilePath(asset);
    if (!QFileInfo(asset).isFile()) {
        const QString package = sourceDirectory.filePath(QStringLiteral("scene.pkg"));
        if (QFileInfo(package).isFile()) asset = package;
        else { error = QStringLiteral("壁纸原始资源不可访问，未打开未静音的背景。"); return {}; }
    }
    QString type = project.value(QStringLiteral("type")).toString().toLower();
    if (type.isEmpty()) type = source.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("web") : source.suffix().compare(QStringLiteral("pkg"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("scene") : QStringLiteral("video");
    project.insert(QStringLiteral("type"), type);
    project.insert(QStringLiteral("file"), QDir::fromNativeSeparators(QFileInfo(asset).absoluteFilePath()));
    const QString preview = project.value(QStringLiteral("preview")).toString();
    if (!preview.isEmpty() && QDir::isRelativePath(preview))
        project.insert(QStringLiteral("preview"), QDir::fromNativeSeparators(sourceDirectory.absoluteFilePath(preview)));
    QJsonObject properties = project.value(QStringLiteral("presetproperties")).toObject();
    properties.insert(QStringLiteral("volume"), 0);
    project.insert(QStringLiteral("presetproperties"), properties);
    QTemporaryDir temporary(QDir(QDir::tempPath()).filePath(QStringLiteral("PHSRadioWallpaper-XXXXXX")));
    if (!temporary.isValid()) { error = QStringLiteral("无法创建加载前静音的临时壁纸工程。"); return {}; }
    const QString path = QDir(temporary.path()).filePath(QStringLiteral("project.json"));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(QJsonDocument(project).toJson(QJsonDocument::Compact)) < 0 || !output.commit()) {
        error = QStringLiteral("无法写入加载前静音的临时壁纸工程。"); return {};
    }
    QString compatibleDirectory = temporary.path();
#ifdef Q_OS_WIN
    const auto isAscii = [](const QString &text) {
        for (const QChar character : text) if (character.unicode() > 127) return false;
        return true;
    };
    if (!isAscii(compatibleDirectory)) {
        const QString native = QDir::toNativeSeparators(compatibleDirectory);
        const DWORD count = GetShortPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, 0);
        if (count) {
            std::wstring shortPath(count, L'\0');
            const DWORD written = GetShortPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), shortPath.data(), count);
            if (written > 0 && written < count) compatibleDirectory = QString::fromWCharArray(shortPath.c_str());
        }
        if (!isAscii(compatibleDirectory)) {
            error = QStringLiteral("Wallpaper Engine 无法读取当前临时目录的字符，未打开未静音的背景。"); return {};
        }
    }
#endif
    directory = temporary.path();
    temporary.setAutoRemove(false);
    return QDir(compatibleDirectory).filePath(QStringLiteral("project.json"));
}

bool controlNamedWallpaper(const QString &executable, const QString &windowName, bool mute)
{
    // A missing location would target every wallpaper, so never issue such a command.
    if (executable.isEmpty() || !windowName.startsWith(QStringLiteral("PHSRadioWallpaper_")))
        return false;
#ifdef Q_OS_WIN
    if (!mute) {
        const HWND window = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(windowName.utf16()));
        if (isNamedRenderer(window, windowName)) {
            ShowWindow(window, SW_HIDE);
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
#endif
    QProcess command;
    command.setProgram(executable);
    command.setWorkingDirectory(QFileInfo(executable).absolutePath());
    command.setStandardOutputFile(QProcess::nullDevice());
    command.setStandardErrorFile(QProcess::nullDevice());
    QStringList arguments{QStringLiteral("-control"),
        mute ? QStringLiteral("applyProperties") : QStringLiteral("closeWallpaper"),
        QStringLiteral("-location"), windowName};
#ifdef Q_OS_WIN
    command.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    if (mute) {
        // WE extracts this RAW region itself. setArguments would escape the
        // JSON quotes before its own parser sees them on Windows.
        command.setNativeArguments(QStringLiteral("-properties RAW~({\"volume\":0})~END"));
    }
#else
    if (mute)
        arguments.append({QStringLiteral("-properties"), QStringLiteral("RAW~({\"volume\":0})~END")});
#endif
    command.setArguments(arguments);
    return command.startDetached();
}
} // namespace

WallpaperEngineCapture::WallpaperEngineCapture(QObject *parent) : QObject(parent)
{
    m_session = new QMediaCaptureSession(this);
    m_capture = new QWindowCapture(this);
    m_nativeCapture = std::make_unique<NativeWindowCapture>();
    m_sink = new QVideoSink;
    m_command = new QProcess(this);
    m_rgbWatcher = new QFutureWatcher<QImage>(this);
    connect(m_rgbWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
        const QImage image = m_rgbWatcher->result();
        m_rgbConversionBusy = false;
        if (m_rgbJobGeneration == m_conversionGeneration && m_requested && !m_paused && !image.isNull())
            m_pendingImage = image;
        // Completion never publishes or rearms the frame timer. Presentation
        // belongs to the next display tick. Start the newest compatible frame
        // immediately so conversion does not wait an extra presentation interval.
        // An incompatible frame must remain available to the GUI-only fallback.
        if (m_requested && !m_paused) {
            const QVideoFrame frame = m_frames->takeIf(VideoFrames::canConvertRgb);
            if (frame.isValid())
                startRgbConversion(frame);
        }
    });
    m_command->setStandardOutputFile(QProcess::nullDevice());
    m_command->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    m_command->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_session->setWindowCapture(m_capture);
    m_session->setVideoSink(m_sink);
    // Qt's upstream capture connection follows the platform sink's affinity.
    // Attach on the owner thread first, then move the parentless sink and its
    // platform children so incoming frame events cannot queue behind GUI paint.
    m_sinkThread = new QThread(this);
    m_sinkThread->setObjectName(QStringLiteral("wallpaperFrameSink"));
    m_sink->moveToThread(m_sinkThread);
    m_sinkThread->start();
    m_findTimer.setInterval(120);
    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setSingleShot(true);
    if (auto *widget = qobject_cast<QWidget *>(parent)) {
        setFrameRate(Aero::displayRefreshRate(widget));
        m_displayObserver = Aero::observeDisplayRefresh(widget, this, [this, widget = QPointer<QWidget>(widget)] {
            if (widget) setFrameRate(Aero::displayRefreshRate(widget));
        });
    }
    m_guardTimer.setInterval(250);
    m_timeout.setSingleShot(true);
    connect(&m_findTimer, &QTimer::timeout, this, [this] { findWindow(); });
    connect(&m_frameTimer, &QTimer::timeout, this, [this] { publishFrame(); });
    connect(&m_guardTimer, &QTimer::timeout, this, [this] {
        if (m_requested && !parkWindow())
            fail(QStringLiteral("无法保持 Wallpaper Engine 专属渲染窗口不可见，已停止背景连接。"));
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_requested && !m_paused && !m_usingNativeCapture && startNativeFallback())
            return;
        fail(QStringLiteral("Wallpaper Engine 连接超时，未收到动态画面。请在主题窗口复制连接信息。"));
    });
    // Backend frames replace a single buffer directly. A stalled GUI must not
    // accumulate queued callbacks and retained capture textures for old frames.
    connect(m_sink, &QVideoSink::videoFrameChanged, this,
            [frames = m_frames](const QVideoFrame &frame) { frames->offer(frame); },
            Qt::DirectConnection);
    connect(m_capture, &QWindowCapture::errorOccurred, this,
            [this](QWindowCapture::Error error, const QString &detail) {
        handleCaptureError(error, detail);
    });
    connect(m_command, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_requested && error == QProcess::FailedToStart)
            fail(QStringLiteral("无法启动 Wallpaper Engine 控制命令，请检查安装路径。"));
    });
}

WallpaperEngineCapture::~WallpaperEngineCapture()
{
    delete m_displayObserver;
    onError = {};
    onStatusChanged = {};
    onFrame = {};
    stop();
    // The capture grabber has stopped and its queued sink frames are drained.
    // Return the sink to the session's thread before detaching/deleting it.
    // Independently owned RGB conversion jobs never participate in this wait.
    if (m_sink->thread() != thread()) {
        const auto returnSink = [sink = m_sink, owner = thread()] {
            sink->moveToThread(owner);
        };
        if (m_sink->thread() == QThread::currentThread()) {
            returnSink();
        } else {
            if (!m_sinkThread->isRunning())
                m_sinkThread->start();
            QMetaObject::invokeMethod(m_sink, returnSink, Qt::BlockingQueuedConnection);
        }
    }
    m_session->setVideoSink(nullptr);
    delete m_sink;
    m_sink = nullptr;
    m_sinkThread->quit();
    m_sinkThread->wait();
}

int WallpaperEngineCapture::configuredFrameRate(const QString &executable, QString *error)
{
    const QFileInfo binary(executable);
    QFile file(binary.dir().filePath(QStringLiteral("config.json")));
    if (!binary.isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        if (error) *error = QStringLiteral("无法读取 Wallpaper Engine 配置文件。");
        return 0;
    }
    const QByteArray bytes = file.read(4 * 1024 * 1024 + 1);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (bytes.size() > 4 * 1024 * 1024 || parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Wallpaper Engine 配置文件暂时不可用，请稍后刷新。");
        return 0;
    }
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = qEnvironmentVariable("USER");
    return frameRateFromConfiguration(document.object(), user, error);
}

void WallpaperEngineCapture::start(const QString &executable, const QString &projectPath, QSize size)
{
    stop();
    const QFileInfo binary(executable), project(projectPath);
    const QString type = project.suffix().toLower();
    if (!binary.isFile() || !project.isFile()
        || (type != QStringLiteral("json") && type != QStringLiteral("pkg")
            && type != QStringLiteral("html") && type != QStringLiteral("mp4")
            && type != QStringLiteral("webm"))) {
        fail(QStringLiteral("请选择已安装的 Wallpaper Engine 和有效的壁纸工程。"));
        return;
    }
    QString wrapperError;
    const QString wrapper = mutedProject(project.absoluteFilePath(), m_mutedProjectDirectory, wrapperError);
    if (wrapper.isEmpty()) {
        fail(wrapperError);
        return;
    }
    m_executable = binary.absoluteFilePath();
    m_windowName = QStringLiteral("PHSRadioWallpaper_%1_%2")
        .arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::Id128));
    // Render at actual device pixels: logical widget sizes look soft on high-DPI screens.
    size = size.expandedTo(QSize(640, 360));
    m_renderSize = size;
    m_requested = true;
    m_announced = false;
    m_paused = false;
    m_command->setWorkingDirectory(binary.absolutePath());
    m_command->setProgram(m_executable);
    const QPoint parking = rendererParkingPosition();
    m_command->setArguments({QStringLiteral("-control"), QStringLiteral("openWallpaper"),
        QStringLiteral("-file"), wrapper, QStringLiteral("-playInWindow"), m_windowName,
        QStringLiteral("-width"), QString::number(size.width()), QStringLiteral("-height"), QString::number(size.height()),
        QStringLiteral("-x"), QString::number(parking.x()), QStringLiteral("-y"), QString::number(parking.y()),
        QStringLiteral("-borderless"), QStringLiteral("1"), QStringLiteral("-activate"), QStringLiteral("0")});
    if (!m_command->startDetached()) {
        fail(QStringLiteral("无法启动 Wallpaper Engine 控制命令，请检查安装路径。"));
        return;
    }
    // The descriptor applies volume zero before initialization; keep it zero
    // through the verified per-window control route while the renderer opens.
    if (!muteWallpaper()) {
        fail(QStringLiteral("无法设置本软件专属 Wallpaper Engine 窗口的静音。"));
        return;
    }
    const QString name = m_windowName;
    for (int delay : {200, 500, 1000, 1800}) {
        QTimer::singleShot(delay, this, [this, name] {
            if (m_requested && m_windowName == name && !muteWallpaper())
                fail(QStringLiteral("无法保持本软件专属 Wallpaper Engine 窗口静音。"));
        });
    }
    m_findTimer.start();
    m_timeout.start(18000);
    updateStatus(QStringLiteral("正在连接 Wallpaper Engine 动态壁纸…"));
}

void WallpaperEngineCapture::findWindow()
{
    if (!m_requested || m_paused || m_captureErrorPending || m_usingNativeCapture)
        return;
#ifdef Q_OS_WIN
    // Launch offscreen, apply zero opacity before moving to a capture-capable
    // monitor, then enumerate. An offscreen cold start does not produce frames.
    const HWND renderer = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    // A titled HWND may precede the renderer's visible, non-zero swap chain.
    // Do not resize an unready window into appearing ready for WGC.
    if (!rendererIsUsable(renderer, m_windowName)) {
        resetCaptureCandidate();
        updateStatus(isNamedRenderer(renderer, m_windowName)
            ? QStringLiteral("正在等待壁纸窗口初始化…")
            : QStringLiteral("正在等待 Wallpaper Engine 创建壁纸窗口…"));
        return;
    }
    if (!parkWindow()) {
        fail(QStringLiteral("无法隐藏 Wallpaper Engine 专属渲染窗口，已停止背景连接。"));
        return;
    }
#endif
    if (!captureWindowReady()) {
        updateStatus(QStringLiteral("正在等待壁纸窗口尺寸稳定…"));
        return;
    }
    for (const auto &window : QWindowCapture::capturableWindows()) {
        if (window.isValid() && window.description() == m_windowName) {
            m_findTimer.stop();
            if (!muteWallpaper()) {
                fail(QStringLiteral("无法设置本软件专属 Wallpaper Engine 窗口的静音。"));
                return;
            }
            m_capture->setWindow(window);
            if (!m_paused) {
                m_frames->reset(true);
                m_capture->start();
            }
            // start() can report an error synchronously. Recovery happens only
            // after that stack unwinds; never rearm timers for the failed start.
            if (!m_requested || m_captureErrorPending)
                return;
            if (!m_paused) {
                m_frameClock.start();
                m_frameSchedule.reset(0);
                m_frameTimer.start(m_frameSchedule.delayMs(0));
            }
            m_guardTimer.start();
#ifdef Q_OS_WIN
            if (m_paused)
                ShowWindow(reinterpret_cast<HWND>(m_nativeWindow), SW_HIDE);
#endif
            updateStatus(QStringLiteral("已找到壁纸窗口，正在等待动态画面…"));
            return;
        }
    }
    updateStatus(QStringLiteral("已找到壁纸窗口，正在等待 Windows 捕获识别…"));
}

void WallpaperEngineCapture::resetCaptureCandidate()
{
    m_candidateWindow = 0;
    m_candidateSize = {};
    m_candidateStablePolls = 0;
}

bool WallpaperEngineCapture::captureWindowReady()
{
#ifdef Q_OS_WIN
    const HWND renderer = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    QSize size;
    if (!rendererIsUsable(renderer, m_windowName, &size)) {
        resetCaptureCandidate();
        return false;
    }
    const auto handle = reinterpret_cast<quintptr>(renderer);
    if (handle == m_candidateWindow && size == m_candidateSize) {
        m_candidateStablePolls = qMin(m_candidateStablePolls + 1, 3);
    } else {
        m_candidateWindow = handle;
        m_candidateSize = size;
        m_candidateStablePolls = 1;
    }
    // Three 120 ms discovery polls allow native style/size changes and slower
    // wallpaper initialization to settle before the backend creates its item.
    return m_candidateStablePolls >= 3;
#else
    return true;
#endif
}

void WallpaperEngineCapture::handleCaptureError(int error, const QString &detail)
{
    if (!m_requested || m_paused || m_usingNativeCapture
        || error == QWindowCapture::NoError || m_captureErrorPending)
        return;
    m_captureDetail = detail;
    m_captureErrorPending = true;
    const quint64 generation = m_captureGeneration;
    const QString name = m_windowName;
    const QString message = QStringLiteral("无法读取 Wallpaper Engine 动态窗口：%1").arg(detail);
    // Qt may emit this while start() is still constructing its grabber. Stop
    // and cleanup must run after that call returns, not re-enter the backend.
    QTimer::singleShot(0, this, [this, generation, name, error, message] {
        if (!m_requested || m_paused || generation != m_captureGeneration || name != m_windowName)
            return;
        const bool transient = error == QWindowCapture::CaptureFailed
            || error == QWindowCapture::InternalError || error == QWindowCapture::NotFound;
        if (((transient && m_captureRecoveryAttempts >= 3)
             || error == QWindowCapture::CapturingNotSupported) && startNativeFallback())
            return;
        if (!transient || m_captureRecoveryAttempts >= 3) {
            fail(message);
            return;
        }
        ++m_captureRecoveryAttempts;
        m_findTimer.stop();
        m_frameTimer.stop();
        m_guardTimer.stop();
        m_capture->stop();
        drainSinkFrames();
        resetConvertedFrames();
        m_frames->reset(false);
        resetCaptureCandidate();
        // Preserve the original initialization deadline. A runtime failure
        // gets one deadline shared by all recovery attempts, not one per retry.
        if (!m_timeout.isActive())
            m_timeout.start(18000);
        QTimer::singleShot(300, this, [this, generation, name] {
            if (!m_requested || m_paused || generation != m_captureGeneration || name != m_windowName)
                return;
            m_captureErrorPending = false;
            m_frames->reset(true);
            m_findTimer.start();
        });
        updateStatus(QStringLiteral("Windows 捕获暂时失败，正在重新连接（%1/3）…")
            .arg(m_captureRecoveryAttempts));
    });
}

bool WallpaperEngineCapture::startNativeFallback()
{
#ifdef Q_OS_WIN
    const HWND renderer = reinterpret_cast<HWND>(m_nativeWindow);
    if (!rendererIsUsable(renderer, m_windowName))
        return false;
    ++m_captureGeneration;
    m_findTimer.stop();
    m_frameTimer.stop();
    m_capture->stop();
    drainSinkFrames();
    resetConvertedFrames();
    m_frames->reset(false);
    m_usingNativeCapture = true;
    m_announced = false;
    m_captureErrorPending = false;
    if (m_nativeCreateBeforeOpacity) {
        m_waitingForCaptureItem = true;
        if (!parkWindow())
            return false;
    }
    // Preparation happens offscreen. Select the player's intended display,
    // rather than whichever monitor happens to be closest to that parking spot.
    m_nativeCapture->start(m_nativeWindow, m_ownerWindow, m_ownerWindow);
    m_frameClock.start();
    m_frameSchedule.reset(0);
    m_frameTimer.start(m_frameSchedule.delayMs(0));
    m_guardTimer.start();
    m_timeout.start(18000);
    updateStatus(m_nativeCreateBeforeOpacity
        ? QStringLiteral("正在准备兼容捕获窗口…") : m_nativeWithoutOwner
        ? QStringLiteral("正在尝试另一种窗口兼容模式…")
        : QStringLiteral("正在使用 Windows 原生兼容捕获连接壁纸…"));
    return true;
#else
    return false;
#endif
}

bool WallpaperEngineCapture::retryNativeTargetWithoutOwner()
{
#ifdef Q_OS_WIN
    if (!m_requested || m_paused || !m_usingNativeCapture || m_nativeWithoutOwner
        || !rendererIsUsable(reinterpret_cast<HWND>(m_nativeWindow), m_windowName))
        return false;
    DWORD affinity = WDA_NONE;
    if (GetWindowDisplayAffinity(reinterpret_cast<HWND>(m_nativeWindow), &affinity)
        && affinity != WDA_NONE)
        return false;
    // If Windows rejects the target item, test a different owner state once.
    // This is a capability probe, not an assumption about an OS or GPU. Preserve zero
    // desktop opacity and input isolation throughout. Do not try other GPUs
    // for an HWND rejection, and never retry protected-window failures.
    m_nativeWithoutOwner = true;
    m_nativeCapture->stop();
    if (!parkWindow())
        return false;
    return startNativeFallback();
#else
    return false;
#endif
}

bool WallpaperEngineCapture::retryNativeTargetBeforeOpacity()
{
#ifdef Q_OS_WIN
    if (!m_requested || m_paused || !m_usingNativeCapture || !m_nativeWithoutOwner
        || m_nativeCreateBeforeOpacity
        || !rendererIsUsable(reinterpret_cast<HWND>(m_nativeWindow), m_windowName))
        return false;
    DWORD affinity = WDA_NONE;
    if (GetWindowDisplayAffinity(reinterpret_cast<HWND>(m_nativeWindow), &affinity)
        && affinity != WDA_NONE)
        return false;
    // Test whether the target can be initialized before zero opacity is applied.
    // Validate its offscreen placement before raising alpha during preparation.
    m_nativeCreateBeforeOpacity = true;
    m_nativeCapture->stop();
    return startNativeFallback();
#else
    return false;
#endif
}

bool WallpaperEngineCapture::parkWindow()
{
#ifdef Q_OS_WIN
    const HWND handle = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    if (!isNamedRenderer(handle, m_windowName))
        return false;
    m_nativeWindow = reinterpret_cast<quintptr>(handle);
    const auto exStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    // An earlier preparation pass can have non-zero alpha. Make every style,
    // owner and geometry mutation invisible before allowing the renderer's
    // WINDOWPOS handlers to run. Raise alpha only after the final placement check.
    if ((exStyle & WS_EX_LAYERED) && !setRendererOpacity(handle, 0))
        return false;
    // Tool windows are rejected by Qt's Windows capture backend ("No tooltips").
    // Ownership keeps the renderer out of Alt+Tab without that window style.
    const auto desiredStyle = (exStyle | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED)
        & ~(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW);
    if (desiredStyle != exStyle) {
        SetLastError(0);
        if (!SetWindowLongPtrW(handle, GWL_EXSTYLE, desiredStyle) && GetLastError() != 0) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
    }
    // WGC reads the original swap-chain image, independently of the desktop
    // compositor's constant window opacity. Zero opacity is genuinely invisible
    // and still yields opaque, full-resolution dynamic frames. Cloaking a WE
    // window from this process is denied; hiding or cold-starting offscreen stops
    // its frame production. Do not use either as the active rendering strategy.
    if (!setRendererOpacity(handle, 0))
        return false;
    // An invisible helper owner keeps the renderer behind the player. Directly
    // owning it by the main window would force the wallpaper above that window.
    if (!m_nativeWithoutOwner && !m_hiddenOwner)
        m_hiddenOwner = reinterpret_cast<quintptr>(CreateWindowExW(WS_EX_TOOLWINDOW,
            L"STATIC", L"PHSRadioBackgroundOwner", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (!m_nativeWithoutOwner && !m_hiddenOwner) {
        ShowWindow(handle, SW_HIDE);
        return false;
    }
    const LONG_PTR desiredOwner = m_nativeWithoutOwner ? 0 : static_cast<LONG_PTR>(m_hiddenOwner);
    if (GetWindowLongPtrW(handle, GWLP_HWNDPARENT) != desiredOwner) {
        SetLastError(0);
        if (!SetWindowLongPtrW(handle, GWLP_HWNDPARENT, desiredOwner) && GetLastError() != 0) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
    }
    const QPoint position = m_waitingForCaptureItem
        ? rendererParkingPosition() : rendererCapturePosition(m_ownerWindow);
    RECT geometry{};
    GetWindowRect(handle, &geometry);
    if (geometry.left != position.x() || geometry.top != position.y() || desiredStyle != exStyle) {
        if (!SetWindowPos(handle, HWND_BOTTOM, position.x(), position.y(), 0, 0,
                          SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
    }
    if (!resizeRenderer(handle, m_renderSize))
        return false;
    if (m_waitingForCaptureItem) {
        // A renderer can constrain WINDOWPOS while moving OR resizing. Trust
        // the final rectangle only after all such mutations have completed.
        if (!rendererIsOutsideDesktop(handle)) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
        if (!setRendererOpacity(handle, 255))
            return false;
    }
#endif
    return true;
}

void WallpaperEngineCapture::publishFrame()
{
    if (m_paused || !m_requested || m_captureErrorPending)
        return;
    const qint64 now = m_frameClock.isValid() ? m_frameClock.nsecsElapsed() : 0;
    if (!m_frameSchedule.advance(now)) {
        m_frameTimer.start(m_frameSchedule.delayMs(now));
        return;
    }
    // Arm before callbacks: a callback that pauses/stops the backend cancels
    // this timer, and the single latest-frame mailbox cannot grow while busy.
    m_frameTimer.start(m_frameSchedule.delayMs(now));
    QImage image;
    if (m_usingNativeCapture) {
        const QString error = m_nativeCapture->errorString();
        if (!error.isEmpty()) {
            if (m_nativeTargetAttempts.size() < 3) {
                m_nativeTargetAttempts.append(QJsonObject{
                    {QStringLiteral("profile"), m_nativeCreateBeforeOpacity
                        ? QStringLiteral("prepare_before_opacity") : m_nativeWithoutOwner
                        ? QStringLiteral("standalone") : QStringLiteral("owned")},
                    {QStringLiteral("error"), error},
                    {QStringLiteral("steps"), m_nativeCapture->diagnosticReport().left(8192)}});
            }
            if (m_nativeCapture->targetWindowRejected()
                && (retryNativeTargetWithoutOwner() || retryNativeTargetBeforeOpacity()))
                return;
            fail(QStringLiteral("壁纸兼容捕获失败：%1").arg(error));
            return;
        }
        if (m_waitingForCaptureItem && m_nativeCapture->targetItemReady()) {
            m_waitingForCaptureItem = false;
            if (!parkWindow()) {
                fail(QStringLiteral("无法恢复兼容壁纸窗口的不可见状态，已停止背景连接。"));
                return;
            }
        }
        image = m_nativeCapture->takeLatestFrame();
        if (!image.isNull() && image.size() != m_renderSize)
            image = {};
    } else {
        image = std::exchange(m_pendingImage, QImage{});
    }
    if (!m_usingNativeCapture && !m_rgbConversionBusy) {
        const QVideoFrame frame = m_frames->take();
        if (frame.isValid()) {
            if (VideoFrames::canConvertRgb(frame)) {
                startRgbConversion(frame);
            } else {
                image = VideoFrames::toImage(frame);
            }
        }
    }
    if (image.isNull())
        return;
    const quint64 generation = m_conversionGeneration;
    const QPointer<WallpaperEngineCapture> guard(this);
    m_timeout.stop();
    if (!m_announced) {
        m_announced = true;
        updateStatus(m_usingNativeCapture
            ? QStringLiteral("Wallpaper Engine 动态背景已连接（兼容捕获）")
            : QStringLiteral("Wallpaper Engine 动态背景已连接"));
        if (!guard || generation != m_conversionGeneration || m_paused || !m_requested)
            return;
    }
    const auto callback = onFrame;
    if (callback)
        callback(image);
}

void WallpaperEngineCapture::startRgbConversion(const QVideoFrame &frame)
{
    m_rgbConversionBusy = true;
    m_rgbJobGeneration = m_conversionGeneration;
    auto promise = std::make_shared<QPromise<QImage>>();
    promise->start();
    m_rgbWatcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, frame] {
        // Only conservatively matched CPU RGB reaches this worker.
        // Mapping failure returns null; Qt/QRhi fallback stays GUI-only.
        promise->addResult(VideoFrames::toRgbImage(frame));
        promise->finish();
    });
}

void WallpaperEngineCapture::resetConvertedFrames()
{
    ++m_conversionGeneration;
    m_pendingImage = {};
    // A running job owns its frame/promise independently. Keep busy until its
    // watcher finishes so a quick pause/restart cannot enqueue another job.
}

void WallpaperEngineCapture::drainSinkFrames()
{
    if (!m_sink || !m_sinkThread || !m_sinkThread->isRunning()
        || m_sink->thread() == QThread::currentThread())
        return;
    // A synchronous grabber stop precedes this FIFO barrier. Old source frames
    // are discarded with the mailbox disabled, before a later resume enables it.
    QMetaObject::invokeMethod(m_sink, [] {}, Qt::BlockingQueuedConnection);
}

void WallpaperEngineCapture::setPaused(bool paused)
{
    const QPointer<WallpaperEngineCapture> guard(this);
    if (m_paused == paused)
        return;
    if (!paused && m_requested && !muteWallpaper()) {
        fail(QStringLiteral("无法保持本软件专属 Wallpaper Engine 窗口静音。"));
        return;
    }
    ++m_captureGeneration;
    m_captureErrorPending = false;
    resetCaptureCandidate();
    m_paused = paused;
    resetConvertedFrames();
    m_findTimer.stop();
    m_frameTimer.stop();
    m_guardTimer.stop();
    m_capture->stop();
    m_nativeCapture->stop();
    drainSinkFrames();
    m_frames->reset(m_requested && !paused);
    if (m_requested) {
#ifdef Q_OS_WIN
        const HWND handle = reinterpret_cast<HWND>(m_nativeWindow);
        if (isNamedRenderer(handle, m_windowName)) {
            if (paused)
                ShowWindow(handle, SW_HIDE);
            else {
                if (!parkWindow()) {
                    fail(QStringLiteral("无法隐藏 Wallpaper Engine 专属渲染窗口，已停止背景连接。"));
                    return;
                }
                ShowWindow(handle, SW_SHOWNOACTIVATE);
            }
        }
#endif
    }
    if (paused) {
        m_timeout.stop();
    } else if (m_requested) {
        m_announced = false;
        // Re-enumerate and wait for the resumed native surface to settle.
        if (m_usingNativeCapture) {
            if (!startNativeFallback()) {
                if (!guard)
                    return;
                fail(QStringLiteral("无法恢复壁纸兼容捕获，请重新选择背景。"));
                return;
            }
            if (!guard || !m_requested || m_paused)
                return;
        } else {
            m_findTimer.start();
        }
        m_timeout.start(18000);
    }
    if (m_requested)
        updateStatus(paused ? QStringLiteral("动态壁纸已暂停，回到播放器后继续连接")
            : m_usingNativeCapture ? QStringLiteral("正在恢复壁纸兼容捕获…")
                                   : QStringLiteral("正在恢复 Wallpaper Engine 动态壁纸连接…"));
}

void WallpaperEngineCapture::setRenderSize(QSize pixels)
{
    const QSize normalized = pixels.expandedTo(QSize(640, 360));
    const bool changed = m_renderSize != normalized;
    if (changed) {
        m_renderSize = normalized;
        resetConvertedFrames();
        m_frames->reset(m_requested && !m_paused);
    }
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(m_nativeWindow);
    if (isNamedRenderer(handle, m_windowName)) {
        RECT actual{};
        if (!changed && GetClientRect(handle, &actual)
            && actual.right == m_renderSize.width() && actual.bottom == m_renderSize.height())
            return;
        if (m_waitingForCaptureItem && !setRendererOpacity(handle, 0)) {
            fail(QStringLiteral("无法隐藏正在准备的壁纸窗口，已停止背景连接。"));
            return;
        }
        if (!resizeRenderer(handle, m_renderSize)) {
            fail(QStringLiteral("无法调整壁纸渲染窗口尺寸，已停止背景连接。"));
            return;
        }
        if (!changed && m_waitingForCaptureItem && !parkWindow()) {
            fail(QStringLiteral("无法恢复壁纸捕获准备状态，已停止背景连接。"));
            return;
        }
    }
#endif
    if (changed && m_usingNativeCapture && m_requested && !m_paused) {
        // A new mailbox and cancellation token prevent an in-flight readback
        // from publishing pixels from the previous native surface size.
        if (!startNativeFallback())
            fail(QStringLiteral("无法调整壁纸兼容捕获尺寸，请重新选择背景。"));
    }
}

void WallpaperEngineCapture::setFrameInterval(int milliseconds)
{
    setFrameRate(1000.0 / qBound(1, milliseconds, 34));
}

void WallpaperEngineCapture::setFrameRate(qreal framesPerSecond)
{
    const qint64 now = m_frameClock.isValid() ? m_frameClock.nsecsElapsed() : 0;
    m_frameSchedule.setRefreshRate(framesPerSecond, now);
    if (m_frameTimer.isActive())
        m_frameTimer.start(m_frameSchedule.delayMs(now));
    else
        m_frameTimer.setInterval(m_frameSchedule.delayMs(now));
}

bool WallpaperEngineCapture::isRunning() const
{
    return m_requested && m_announced;
}

bool WallpaperEngineCapture::muteWallpaper()
{
    return m_requested && controlNamedWallpaper(m_executable, m_windowName, true);
}

void WallpaperEngineCapture::stop()
{
    ++m_captureGeneration;
    m_requested = false;
    m_announced = false;
    m_captureErrorPending = false;
    m_captureRecoveryAttempts = 0;
    m_usingNativeCapture = false;
    m_nativeWithoutOwner = false;
    m_nativeCreateBeforeOpacity = false;
    m_waitingForCaptureItem = false;
    m_captureDetail.clear();
    m_nativeTargetAttempts = {};
    m_failureReport.clear();
    m_statusText = QStringLiteral("已停止动态壁纸连接");
    resetCaptureCandidate();
    resetConvertedFrames();
    m_frames->reset(false);
    m_findTimer.stop();
    m_frameTimer.stop();
    m_frameClock.invalidate();
    m_guardTimer.stop();
    m_timeout.stop();
    m_capture->stop();
    m_nativeCapture->stop();
    drainSinkFrames();
    m_frames->reset(false);
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(m_nativeWindow);
    if (isNamedRenderer(handle, m_windowName)) {
        ShowWindow(handle, SW_HIDE);
        PostMessageW(handle, WM_CLOSE, 0, 0);
    }
#endif
    // Also closes a pop-out that was created just before cancellation and not
    // discovered yet. The unique name can only refer to this object's renderer.
    if (!m_windowName.isEmpty() && !m_executable.isEmpty()) {
        const QString executable = m_executable, name = m_windowName;
        controlNamedWallpaper(executable, name, false);
        // The open command can reach WE after the first close on quick cancel.
        // Capture values only: retries remain safe after this object is deleted.
        for (int delay : {250, 700, 1600, 3000, 5000, 10000, 18000}) {
            QTimer::singleShot(delay, QCoreApplication::instance(), [executable, name] {
                controlNamedWallpaper(executable, name, false);
            });
        }
    }
    m_nativeWindow = 0;
#ifdef Q_OS_WIN
    if (m_hiddenOwner)
        DestroyWindow(reinterpret_cast<HWND>(m_hiddenOwner));
#endif
    m_hiddenOwner = 0;
    m_windowName.clear();
    const QString directory = m_mutedProjectDirectory;
    m_mutedProjectDirectory.clear();
    if (!directory.isEmpty()) {
        QTimer::singleShot(20000, QCoreApplication::instance(), [directory] {
            removeMutedProject(directory);
        });
    }
}

void WallpaperEngineCapture::fail(const QString &message)
{
    const QString report = buildDiagnosticReport(message);
    stop();
    m_statusText = message;
    m_failureReport = report;
    const auto callback = onError;
    if (callback)
        callback(message);
}

void WallpaperEngineCapture::updateStatus(const QString &status)
{
    if (m_statusText == status)
        return;
    m_statusText = status;
    const auto callback = onStatusChanged;
    if (callback)
        callback(status);
}

QString WallpaperEngineCapture::diagnosticReport() const
{
    if (!m_failureReport.isEmpty() && !m_requested)
        return m_failureReport;
    return buildDiagnosticReport();
}

QString WallpaperEngineCapture::buildDiagnosticReport(const QString &error) const
{
    const auto version = QOperatingSystemVersion::current();
    QJsonObject report{
        {QStringLiteral("application_version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("windows_version"), QStringLiteral("%1.%2.%3")
            .arg(version.majorVersion()).arg(version.minorVersion()).arg(version.microVersion())},
        {QStringLiteral("system"), QSysInfo::prettyProductName()},
        {QStringLiteral("status"), error.isEmpty() ? m_statusText : error},
        {QStringLiteral("connection_phase_before_error"), m_statusText},
        {QStringLiteral("requested"), error.isEmpty() && m_requested},
        {QStringLiteral("paused"), m_paused},
        {QStringLiteral("capture_active"), error.isEmpty() && (m_usingNativeCapture
            ? m_nativeCapture->isActive() : m_capture->isActive())},
        {QStringLiteral("native_fallback"), m_usingNativeCapture},
        {QStringLiteral("native_target_profile"), m_nativeCreateBeforeOpacity
            ? QStringLiteral("prepare_before_opacity") : m_nativeWithoutOwner
                ? QStringLiteral("standalone") : QStringLiteral("owned")},
        {QStringLiteral("waiting_for_capture_item"), m_waitingForCaptureItem},
        {QStringLiteral("native_target_attempts"), m_nativeTargetAttempts},
        {QStringLiteral("timeout_remaining_ms"), m_timeout.remainingTime()},
        {QStringLiteral("recovery_attempts"), m_captureRecoveryAttempts},
        {QStringLiteral("last_error"), error},
        {QStringLiteral("qt_capture_error_detail"), m_captureDetail},
        {QStringLiteral("renderer_found"), false},
        {QStringLiteral("renderer_visible"), false},
        {QStringLiteral("renderer_client_width"), 0},
        {QStringLiteral("renderer_client_height"), 0},
    };
    const QString requestedBackend = qEnvironmentVariable("QT_WINDOW_CAPTURE_BACKEND");
    report.insert(QStringLiteral("qt_window_capture_backend_requested"),
        requestedBackend.isEmpty() ? QStringLiteral("automatic")
        : QStringList{QStringLiteral("uwp"), QStringLiteral("gdi"), QStringLiteral("grabwindow")}.contains(requestedBackend)
            ? requestedBackend : QStringLiteral("custom"));
#ifdef Q_OS_WIN
    const HWND renderer = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    if (isNamedRenderer(renderer, m_windowName)) {
        RECT bounds{};
        report.insert(QStringLiteral("renderer_found"), true);
        report.insert(QStringLiteral("renderer_visible"), bool(IsWindowVisible(renderer)));
        report.insert(QStringLiteral("renderer_minimized"), bool(IsIconic(renderer)));
        if (GetClientRect(renderer, &bounds)) {
            report.insert(QStringLiteral("renderer_client_width"), int(bounds.right - bounds.left));
            report.insert(QStringLiteral("renderer_client_height"), int(bounds.bottom - bounds.top));
        }
        report.insert(QStringLiteral("renderer_style"), QString::number(GetWindowLongPtrW(renderer, GWL_STYLE), 16));
        report.insert(QStringLiteral("renderer_ex_style"), QString::number(GetWindowLongPtrW(renderer, GWL_EXSTYLE), 16));
        report.insert(QStringLiteral("renderer_class_style"), QString::number(GetClassLongPtrW(renderer, GCL_STYLE), 16));
        report.insert(QStringLiteral("renderer_root_is_self"), GetAncestor(renderer, GA_ROOT) == renderer);
        const HWND owner = GetWindow(renderer, GW_OWNER);
        report.insert(QStringLiteral("renderer_has_owner"), owner != nullptr);
        report.insert(QStringLiteral("renderer_owner_visible"), owner && IsWindowVisible(owner));
        DWORD cloaked = 0;
        const HRESULT cloakResult = DwmGetWindowAttribute(renderer, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        report.insert(QStringLiteral("renderer_cloaked_query_hresult"), QStringLiteral("0x%1")
            .arg(quint32(cloakResult), 8, 16, QLatin1Char('0')));
        if (SUCCEEDED(cloakResult))
            report.insert(QStringLiteral("renderer_cloaked"), int(cloaked));
        DWORD affinity = 0;
        SetLastError(0);
        const bool affinityKnown = GetWindowDisplayAffinity(renderer, &affinity);
        const DWORD affinityError = affinityKnown ? ERROR_SUCCESS : GetLastError();
        report.insert(QStringLiteral("renderer_display_affinity_known"), affinityKnown);
        report.insert(QStringLiteral("renderer_display_affinity_query_error"), int(affinityError));
        if (affinityKnown)
            report.insert(QStringLiteral("renderer_display_affinity"), int(affinity));
        COLORREF key{}; BYTE alpha{}; DWORD flags{};
        if (GetLayeredWindowAttributes(renderer, &key, &alpha, &flags))
            report.insert(QStringLiteral("renderer_opacity"), int(alpha));
    }
    QJsonArray adapters;
    for (DWORD index = 0; index < 8; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0))
            break;
        if (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
            adapters.append(QString::fromWCharArray(device.DeviceString));
    }
    report.insert(QStringLiteral("display_adapters"), adapters);
#endif
    if (m_usingNativeCapture) {
        report.insert(QStringLiteral("native_capture"), QJsonObject{
            {QStringLiteral("steps"), m_nativeCapture->diagnosticReport()}});
    }
    return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
}
