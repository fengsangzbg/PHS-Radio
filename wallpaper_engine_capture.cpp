#include "wallpaper_engine_capture.h"

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
#include <QTemporaryDir>
#include <QUuid>
#include <QVideoSink>
#include <QWindowCapture>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
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
    m_sink = new QVideoSink(this);
    m_command = new QProcess(this);
    m_command->setStandardOutputFile(QProcess::nullDevice());
    m_command->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    m_command->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_session->setWindowCapture(m_capture);
    m_session->setVideoSink(m_sink);
    m_findTimer.setInterval(120);
    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_timeout.setSingleShot(true);
    connect(&m_findTimer, &QTimer::timeout, this, [this] { findWindow(); });
    connect(&m_frameTimer, &QTimer::timeout, this, [this] { publishFrame(); });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("Wallpaper Engine 未输出动态画面，请先启动它，再重新连接背景。"));
    });
    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_requested && !m_paused && frame.isValid()) {
            m_latestFrame = frame;
            m_hasFrame = true;
        }
    });
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
    onError = {};
    onStatusChanged = {};
    onFrame = {};
    stop();
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
    m_command->setArguments({QStringLiteral("-control"), QStringLiteral("openWallpaper"),
        QStringLiteral("-file"), wrapper, QStringLiteral("-playInWindow"), m_windowName,
        QStringLiteral("-width"), QString::number(size.width()), QStringLiteral("-height"), QString::number(size.height()),
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
    for (const auto &window : QWindowCapture::capturableWindows()) {
        if (window.isValid() && window.description() == m_windowName) {
            m_findTimer.stop();
            if (!muteWallpaper()) {
                fail(QStringLiteral("无法设置本软件专属 Wallpaper Engine 窗口的静音。"));
                return;
            }
            parkWindow();
            m_capture->setWindow(window);
            if (!m_paused)
                m_capture->start();
            if (!m_paused)
                m_frameTimer.start();
            return;
        }
    }
}

void WallpaperEngineCapture::parkWindow()
{
#ifdef Q_OS_WIN
    const HWND handle = FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(m_windowName.utf16()));
    if (!handle)
        return;
    m_nativeWindow = reinterpret_cast<quintptr>(handle);
    const auto exStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    // Tool windows are rejected by Qt's Windows capture backend ("No tooltips").
    // Ownership keeps the renderer out of Alt+Tab without that window style.
    SetWindowLongPtrW(handle, GWL_EXSTYLE, (exStyle | WS_EX_NOACTIVATE) & ~(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW));
    // An invisible helper owner keeps the renderer behind the player. Directly
    // owning it by the main window would force the wallpaper above that window.
    if (!m_hiddenOwner)
        m_hiddenOwner = reinterpret_cast<quintptr>(CreateWindowExW(WS_EX_TOOLWINDOW,
            L"STATIC", L"PHSRadioBackgroundOwner", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
    if (m_hiddenOwner)
        SetWindowLongPtrW(handle, GWLP_HWNDPARENT, static_cast<LONG_PTR>(m_hiddenOwner));
    // Keep the renderer visible for Graphics Capture, below application windows.
    SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    setRenderSize(m_renderSize);
#endif
}

void WallpaperEngineCapture::publishFrame()
{
    if (!m_hasFrame || m_paused || !m_requested)
        return;
    m_hasFrame = false;
    QImage image = m_latestFrame.toImage();
    m_latestFrame = {};
    if (image.isNull())
        return;
    m_timeout.stop();
    if (!m_announced) {
        m_announced = true;
        if (onStatusChanged)
            onStatusChanged(QStringLiteral("Wallpaper Engine 动态背景已连接"));
    }
    if (onFrame)
        onFrame(image);
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
    if (m_requested && !m_findTimer.isActive())
        m_capture->setActive(!paused);
    if (paused) {
        m_frameTimer.stop();
        m_timeout.stop();
        m_latestFrame = {};
        m_hasFrame = false;
    } else if (m_requested) {
        m_frameTimer.start();
        if (!m_announced)
            m_timeout.start(18000);
    }
}

void WallpaperEngineCapture::setRenderSize(QSize pixels)
{
    m_renderSize = pixels.expandedTo(QSize(640, 360));
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(m_nativeWindow);
    if (handle && IsWindow(handle)) {
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
    m_frameTimer.setInterval(qBound(7, milliseconds, 33));
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
    m_findTimer.stop();
    m_frameTimer.stop();
    m_timeout.stop();
    m_capture->stop();
    m_latestFrame = {};
    m_hasFrame = false;
#ifdef Q_OS_WIN
    const HWND handle = reinterpret_cast<HWND>(m_nativeWindow);
    if (handle && IsWindow(handle)) {
        wchar_t title[256] = {};
        GetWindowTextW(handle, title, 256);
        if (QString::fromWCharArray(title) == m_windowName)
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
