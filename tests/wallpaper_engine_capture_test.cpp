#include <QtCore>
#include <QtWidgets>
#include <QtMultimedia>
#include <functional>
#include <stdexcept>
#define private public
#include "../wallpaper_engine_capture.h"
#undef private
// Keep the descriptor helpers testable without launching an external engine.
#include "../wallpaper_engine_capture.cpp"

namespace {
void check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void events(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool until(const std::function<bool()> &condition, int timeout = 12000)
{
    QElapsedTimer time;
    time.start();
    while (!condition() && time.elapsed() < timeout)
        events(10);
    return condition();
}

void descriptorTest()
{
    QTemporaryDir source;
    check(source.isValid(), "The private fixture directory must be available.");
    const QString asset = source.filePath(QStringLiteral("scene.pkg"));
    QFile payload(asset);
    check(payload.open(QIODevice::WriteOnly), "The owned fixture asset must be writable.");
    payload.write("fixture");
    payload.close();
    const QByteArray original = R"({"type":"scene","file":"scene.pkg","presetproperties":{"volume":75,"speed":2}})";
    const QString descriptor = source.filePath(QStringLiteral("project.json"));
    QFile file(descriptor);
    check(file.open(QIODevice::WriteOnly), "The owned descriptor must be writable.");
    file.write(original);
    file.close();
    QString directory, error;
    const QString wrapper = mutedProject(descriptor, directory, error);
    check(!wrapper.isEmpty() && error.isEmpty(), "A scoped muted descriptor must be created.");
    QFile cloned(wrapper);
    check(cloned.open(QIODevice::ReadOnly), "The cloned descriptor must be readable.");
    const QJsonObject document = QJsonDocument::fromJson(cloned.readAll()).object();
    cloned.close();
    check(document.value(QStringLiteral("presetproperties")).toObject().value(QStringLiteral("volume")).toInt(-1) == 0,
          "The private renderer must be muted before initialization.");
    check(document.value(QStringLiteral("presetproperties")).toObject().value(QStringLiteral("speed")).toInt() == 2,
          "Unrelated wallpaper properties must remain intact.");
    check(document.value(QStringLiteral("file")).toString() == QDir::fromNativeSeparators(asset),
          "The original high-quality scene asset must be referenced directly.");
    check(file.open(QIODevice::ReadOnly) && file.readAll() == original,
          "The user's source project must never be edited to change volume.");
    file.close();
    removeMutedProject(directory);
    check(!QFileInfo::exists(wrapper), "Only the owned cloned descriptor must be removed.");
}

#ifdef Q_OS_WIN
class NativeFixture final {
public:
    HWND window = nullptr;
    explicit NativeFixture(const QString &title)
    {
        const QPoint position = rendererParkingPosition();
        window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC",
            reinterpret_cast<LPCWSTR>(title.utf16()), WS_POPUP | WS_VISIBLE,
            position.x(), position.y(), 900, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(window != nullptr, "The owned offscreen native fixture must be created.");
    }
    ~NativeFixture() { if (IsWindow(window)) DestroyWindow(window); }
};

bool fullyTransparent(HWND window)
{
    COLORREF color{}; BYTE alpha = 255; DWORD flags{};
    return GetLayeredWindowAttributes(window, &color, &alpha, &flags)
        && alpha == 0 && (flags & LWA_ALPHA);
}

QSize nativeSize(HWND window)
{
    RECT bounds{};
    GetClientRect(window, &bounds);
    return QSize(bounds.right - bounds.left, bounds.bottom - bounds.top);
}

void nativeIsolationTest()
{
    const QString name = QStringLiteral("PHSRadioWallpaper_%1_%2")
        .arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::Id128));
    NativeFixture renderer(name);
    WallpaperEngineCapture capture;
    capture.m_windowName = name;
    capture.m_renderSize = QSize(1920, 1080);
    check(capture.parkWindow(), "The named renderer must be made invisible without hiding its active surface.");
    check(IsWindowVisible(renderer.window) && fullyTransparent(renderer.window),
          "An active renderer must have zero desktop opacity while remaining capture-compatible.");
    check(nativeSize(renderer.window) == QSize(1920, 1080),
          "Invisibility must preserve full device-pixel rendering resolution.");
    const auto style = GetWindowLongPtrW(renderer.window, GWL_EXSTYLE);
    check((style & WS_EX_NOACTIVATE) && (style & WS_EX_TRANSPARENT) && !(style & WS_EX_TOOLWINDOW),
          "The renderer must neither take focus nor intercept input nor use Qt-incompatible tool style.");
    const HWND owner = reinterpret_cast<HWND>(capture.m_hiddenOwner);
    check(owner && !IsWindowVisible(owner) && GetWindow(renderer.window, GW_OWNER) == owner,
          "The renderer must have its own hidden owner instead of becoming an application-owned popup.");

    // Simulate a renderer/display reconfiguration: the live guard must restore
    // invisibility and placement without opening or touching another wallpaper.
    const QPoint outside = rendererParkingPosition();
    SetWindowPos(renderer.window, HWND_BOTTOM, outside.x(), outside.y(), 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    check(SetLayeredWindowAttributes(renderer.window, 0, 255, LWA_ALPHA), "The owned fixture's opacity must be adjustable.");
    capture.m_requested = true;
    capture.m_guardTimer.start();
    check(until([&] { return fullyTransparent(renderer.window); }, 1500),
          "The visibility guard must recover zero opacity after native reconfiguration.");
    capture.setRenderSize(QSize(2048, 1152));
    check(nativeSize(renderer.window) == QSize(2048, 1152) && fullyTransparent(renderer.window),
          "A physical-pixel resize must retain sharp resolution and zero opacity.");
    capture.setPaused(true);
    check(!IsWindowVisible(renderer.window) && !capture.m_frameTimer.isActive(),
          "Inactive/minimized capture must hide its renderer and stop publishing frames.");
    capture.stop();
    check(!IsWindowVisible(renderer.window) && !capture.m_guardTimer.isActive(),
          "Stopping must hide the owned renderer before asynchronous engine cleanup.");
    events(20);

    NativeFixture foreign(QStringLiteral("UnrelatedWallpaperFixture_%1").arg(QCoreApplication::applicationPid()));
    const QSize foreignSize = nativeSize(foreign.window);
    WallpaperEngineCapture stale;
    stale.m_windowName = name;
    stale.m_nativeWindow = reinterpret_cast<quintptr>(foreign.window);
    stale.setRenderSize(QSize(2800, 1600));
    stale.stop();
    events(20);
    check(IsWindow(foreign.window) && IsWindowVisible(foreign.window) && nativeSize(foreign.window) == foreignSize,
          "A stale HWND must not hide, resize or close an unrelated window.");
}

void liveEngineTest(const QString &executable, const QString &project)
{
    NativeFixture independent(QStringLiteral("UnrelatedWallpaperLive_%1").arg(QCoreApplication::applicationPid()));
    WallpaperEngineCapture capture;
    QSet<QByteArray> frames;
    int count = 0;
    bool resized = false;
    bool opaque = false;
    QSize first;
    QString error;
    QElapsedTimer time;
    time.start();
    capture.onError = [&](const QString &message) {error = message;};
    capture.onFrame = [&](const QImage &image) {
        if (!count) first = image.size();
        const QImage small = image.scaled(80, 45).convertToFormat(QImage::Format_ARGB32);
        frames.insert(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(small.constBits()), small.sizeInBytes()), QCryptographicHash::Sha256));
        opaque |= qAlpha(image.pixel(image.width() / 2, image.height() / 2)) == 255;
        resized |= image.size() == QSize(2048, 1152);
        ++count;
    };
    capture.start(executable, project, QSize(1920, 1080));
    check(until([&] {return count >= 20 || !error.isEmpty();}), "A fully transparent cold start must deliver dynamic frames.");
    check(error.isEmpty() && first == QSize(1920, 1080) && frames.size() >= 10 && opaque,
          "Zero desktop opacity must retain sharp opaque original frames, rather than black or static images.");
    const HWND window = reinterpret_cast<HWND>(capture.m_nativeWindow);
    check(fullyTransparent(window) && IsWindowVisible(window), "The actual engine renderer must be completely transparent while active.");
    capture.setPaused(true);
    check(!IsWindowVisible(window), "Pausing/minimizing must genuinely hide the actual engine renderer.");
    const int pausedCount = count;
    events(350);
    check(count == pausedCount, "No background frames may be published while inactive.");
    capture.setRenderSize(QSize(2048, 1152));
    capture.setPaused(false);
    check(until([&] {return (count >= pausedCount + 30 && resized) || !error.isEmpty();}),
          "Hidden pause/resume and a high-DPI resize must restore moving frames.");
    check(error.isEmpty() && fullyTransparent(window) && nativeSize(window) == QSize(2048, 1152),
          "Resuming must keep the external renderer invisible at full resolution.");
    const QString name = capture.windowName();
    capture.stop();
    check(!IsWindowVisible(window), "Stop must conceal the renderer immediately, before engine close completes.");
    check(until([&] {return FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(name.utf16())) == nullptr;}, 5000),
          "Only the unique engine popout must close on stop.");
    check(IsWindow(independent.window) && IsWindowVisible(independent.window),
          "Stopping actual capture must leave unrelated windows intact.");
    qInfo("Actual WE capture: frames=%d distinct=%lld first=%dx%d resized=%d opaque=%d elapsed_ms=%lld",
          count, qlonglong(frames.size()), first.width(), first.height(), resized, opaque, qlonglong(time.elapsed()));
}
#endif
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication application(argc, argv);
    try {
        descriptorTest();
#ifdef Q_OS_WIN
        if (application.arguments().size() == 4 && application.arguments().at(1) == QStringLiteral("--live"))
            liveEngineTest(application.arguments().at(2), application.arguments().at(3));
        else
            nativeIsolationTest();
#endif
        qInfo("Wallpaper Engine capture regressions passed.");
        return 0;
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
}
