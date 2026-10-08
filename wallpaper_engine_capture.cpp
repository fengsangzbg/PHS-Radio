#include "wallpaper_engine_capture.h"
#include "video_frame_image.h"

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
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
        fail(QStringLiteral("Wallpaper Engine 未输出动态画面，请先启动它，再重新连接背景。"));
    });
    // Backend frames replace a single buffer directly. A stalled GUI must not
    // accumulate queued callbacks and retained capture textures for old frames.
    connect(m_sink, &QVideoSink::videoFrameChanged, this,
            [frames = m_frames](const QVideoFrame &frame) { frames->offer(frame); },
            Qt::DirectConnection);
    connect(m_capture, &QWindowCapture::errorOccurred, this,
            [this](QWindowCapture::Error error, const QString &detail) {
        if (m_requested && error != QWindowCapture::NoError)
            fail(QStringLiteral("无法读取 Wallpaper Engine 动态窗口：%1").arg(detail));
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
        if (onError)
            onError(QStringLiteral("请选择已安装的 Wallpaper Engine 和有效的壁纸工程。"));
        return;
    }
    QString wrapperError;
    const QString wrapper = mutedProject(project.absoluteFilePath(), m_mutedProjectDirectory, wrapperError);
    if (wrapper.isEmpty()) {
        if (onError) onError(wrapperError);
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
    if (onStatusChanged)
        onStatusChanged(QStringLiteral("正在连接 Wallpaper Engine 动态壁纸…"));
}

void WallpaperEngineCapture::findWindow()
{
    if (!m_requested)
        return;
#ifdef Q_OS_WIN
    // Launch offscreen, apply zero opacity before moving to a capture-capable
    // monitor, then enumerate. An offscreen cold start does not produce frames.
    const HWND renderer = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    if (!renderer)
        return;
    if (!parkWindow()) {
        fail(QStringLiteral("无法隐藏 Wallpaper Engine 专属渲染窗口，已停止背景连接。"));
        return;
    }
#endif
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
            return;
        }
    }
}

bool WallpaperEngineCapture::parkWindow()
{
#ifdef Q_OS_WIN
    const HWND handle = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    if (!isNamedRenderer(handle, m_windowName))
        return false;
    m_nativeWindow = reinterpret_cast<quintptr>(handle);
    const auto exStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
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
    COLORREF key{}; BYTE alpha{}; DWORD flags{};
    if (!GetLayeredWindowAttributes(handle, &key, &alpha, &flags)
        || alpha != 0 || !(flags & LWA_ALPHA)) {
        if (!SetLayeredWindowAttributes(handle, 0, 0, LWA_ALPHA)) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
    }
    // An invisible helper owner keeps the renderer behind the player. Directly
    // owning it by the main window would force the wallpaper above that window.
    if (!m_hiddenOwner)
        m_hiddenOwner = reinterpret_cast<quintptr>(CreateWindowExW(WS_EX_TOOLWINDOW,
            L"STATIC", L"PHSRadioBackgroundOwner", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (!m_hiddenOwner) {
        ShowWindow(handle, SW_HIDE);
        return false;
    }
    if (GetWindowLongPtrW(handle, GWLP_HWNDPARENT) != static_cast<LONG_PTR>(m_hiddenOwner)) {
        SetLastError(0);
        if (!SetWindowLongPtrW(handle, GWLP_HWNDPARENT, static_cast<LONG_PTR>(m_hiddenOwner)) && GetLastError() != 0) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
    }
    const QPoint position = rendererCapturePosition(m_ownerWindow);
    RECT geometry{};
    GetWindowRect(handle, &geometry);
    if (geometry.left != position.x() || geometry.top != position.y() || desiredStyle != exStyle) {
        if (!SetWindowPos(handle, HWND_BOTTOM, position.x(), position.y(), 0, 0,
                          SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
            ShowWindow(handle, SW_HIDE);
            return false;
        }
    }
    setRenderSize(m_renderSize);
#endif
    return true;
}

void WallpaperEngineCapture::publishFrame()
{
    if (m_paused || !m_requested)
        return;
    const qint64 now = m_frameClock.isValid() ? m_frameClock.nsecsElapsed() : 0;
    if (!m_frameSchedule.advance(now)) {
        m_frameTimer.start(m_frameSchedule.delayMs(now));
        return;
    }
    // Arm before callbacks: a callback that pauses/stops the backend cancels
    // this timer, and the single latest-frame mailbox cannot grow while busy.
    m_frameTimer.start(m_frameSchedule.delayMs(now));
    QImage image = std::exchange(m_pendingImage, QImage{});
    if (!m_rgbConversionBusy) {
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
        const auto status = onStatusChanged;
        if (status)
            status(QStringLiteral("Wallpaper Engine 动态背景已连接"));
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
    if (m_paused == paused)
        return;
    if (!paused && m_requested && !muteWallpaper()) {
        fail(QStringLiteral("无法保持本软件专属 Wallpaper Engine 窗口静音。"));
        return;
    }
    m_paused = paused;
    resetConvertedFrames();
    m_frames->reset(m_requested && !paused);
    if (paused) {
        m_capture->stop();
        drainSinkFrames();
        m_frames->reset(false);
    }
    if (m_requested && !m_findTimer.isActive()) {
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
        if (!paused)
            m_capture->start();
    }
    if (paused) {
        m_frameTimer.stop();
        m_timeout.stop();
    } else if (m_requested) {
        m_frameClock.start();
        m_frameSchedule.reset(0);
        m_frameTimer.start(m_frameSchedule.delayMs(0));
        if (!m_announced)
            m_timeout.start(18000);
    }
}

void WallpaperEngineCapture::setRenderSize(QSize pixels)
{
    const QSize normalized = pixels.expandedTo(QSize(640, 360));
    if (m_renderSize != normalized) {
        m_renderSize = normalized;
        resetConvertedFrames();
        m_frames->reset(m_requested && !m_paused);
    }
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(m_nativeWindow);
    if (isNamedRenderer(handle, m_windowName)) {
        RECT rect{};
        GetClientRect(handle, &rect);
        if (rect.right == m_renderSize.width() && rect.bottom == m_renderSize.height())
            return;
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_EXSTYLE));
        RECT desired{0, 0, m_renderSize.width(), m_renderSize.height()};
        AdjustWindowRectEx(&desired, style, FALSE, exStyle);
        SetWindowPos(handle, nullptr, 0, 0, desired.right - desired.left,
            desired.bottom - desired.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
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
    m_requested = false;
    m_announced = false;
    resetConvertedFrames();
    m_frames->reset(false);
    m_findTimer.stop();
    m_frameTimer.stop();
    m_frameClock.invalidate();
    m_guardTimer.stop();
    m_timeout.stop();
    m_capture->stop();
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
    stop();
    if (onError)
        onError(message);
}
