#include <QtCore>
#include <QtWidgets>
#include <QtMultimedia>
#include <functional>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <atomic>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAbstractVideoBuffer>
#endif
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

void frameRateConfigurationTest()
{
    QTemporaryDir installation;
    check(installation.isValid(), "The isolated engine configuration fixture must be available.");
    const QString executable = installation.filePath(QStringLiteral("wallpaper64.exe"));
    QFile binary(executable);
    check(binary.open(QIODevice::WriteOnly), "The isolated engine executable fixture must be writable.");
    binary.close();
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = qEnvironmentVariable("USER");
    check(!user.isEmpty(), "The fixture requires the current OS username, never another account's configuration.");
    const auto branch = [](const QJsonValue &fps) {
        return QJsonObject{{QStringLiteral("general"), QJsonObject{
            {QStringLiteral("user"), QJsonObject{{QStringLiteral("fps"), fps}}}}}};
    };
    QJsonObject configuration{{user, branch(59)},
        {QStringLiteral("UnrelatedAccountFixture"), branch(240)}};
    QFile file(installation.filePath(QStringLiteral("config.json")));
    const auto save = [&](const QByteArray &bytes) {
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate)
              && file.write(bytes) == bytes.size(), "The isolated configuration must be writable.");
        file.close();
    };
    const QByteArray original = QJsonDocument(configuration).toJson();
    save(original);
    QString error;
    check(WallpaperEngineCapture::configuredFrameRate(executable, &error) == 59 && error.isEmpty(),
          "The diagnostic must report the current user's actual source limiter, rather than monitor/capture Hz.");
    check(file.open(QIODevice::ReadOnly) && file.readAll() == original,
          "Reading the engine limiter must never rewrite the configuration or other users' settings.");
    file.close();
    configuration.insert(user, branch(144));
    save(QJsonDocument(configuration).toJson());
    check(WallpaperEngineCapture::configuredFrameRate(executable, &error) == 144,
          "An external engine configuration change must be visible on the next read without a stale cache.");
    configuration.remove(user);
    save(QJsonDocument(configuration).toJson());
    check(WallpaperEngineCapture::configuredFrameRate(executable, &error) == 0 && !error.isEmpty(),
          "A missing current-user setting must not be filled with another account's FPS value.");
    configuration.insert(user, branch(QStringLiteral("120")));
    save(QJsonDocument(configuration).toJson());
    check(WallpaperEngineCapture::configuredFrameRate(executable, &error) == 0 && !error.isEmpty(),
          "Malformed limiter types must report unavailable instead of claiming a valid rate.");
    save("{\"partial-write\":");
    check(WallpaperEngineCapture::configuredFrameRate(executable, &error) == 0 && !error.isEmpty(),
          "A concurrently incomplete config write must fail safely without changing the file.");
    save(QByteArray(4 * 1024 * 1024 + 1, ' '));
    check(WallpaperEngineCapture::configuredFrameRate(executable, &error) == 0 && !error.isEmpty(),
          "Unexpectedly large configuration files must be bounded before parsing.");

    WallpaperEngineCapture capture;
    capture.setFrameRate(240);
    check(capture.m_frameTimer.isSingleShot() && capture.m_frameSchedule.refreshRate() == 240
        && capture.m_frameTimer.interval() == 5,
          "Capture must use fractional deadline scheduling at 240 Hz, independently of source limiter metadata.");
    capture.setFrameRate(360);
    check(capture.m_frameSchedule.refreshRate() == 360 && capture.m_frameTimer.interval() == 3,
          "A 360 Hz display must not be limited by the previous fixed 7 ms timer.");
    QVideoFrame frame(QVideoFrameFormat(QSize(8, 8), QVideoFrameFormat::Format_RGBA8888));
    check(frame.map(QVideoFrame::WriteOnly), "The owned high-refresh mailbox fixture must be writable.");
    for (int row = 0; row < 8; ++row)
        std::memset(frame.bits(0) + row * frame.bytesPerLine(0), 255, 8 * 4);
    frame.unmap();
    capture.m_requested = true;
    capture.m_frames->reset(true);
    capture.m_frames->offer(frame);
    capture.m_frameClock.start();
    capture.m_frameSchedule.reset(1000000000);
    int delivered = 0;
    capture.onFrame = [&](const QImage &) { ++delivered; capture.setPaused(true); };
    capture.publishFrame();
    check(delivered == 0 && capture.m_frames->take().isValid(),
          "An early notification must not consume or convert the latest captured frame before its deadline.");
    capture.m_frames->offer(frame);
    capture.m_frameSchedule.reset(-1000000000);
    capture.publishFrame();
    capture.m_frameTimer.stop();
    check(capture.m_rgbConversionBusy && delivered == 0,
          "Safe RGB must convert asynchronously rather than invoke the frame callback on the current GUI tick.");
    check(until([&] { return !capture.m_rgbConversionBusy; })
              && !capture.m_pendingImage.isNull() && delivered == 0,
          "Worker completion must retain its image for the next display tick without publishing immediately.");
    capture.m_frameSchedule.reset(-1000000000);
    capture.publishFrame();
    check(delivered == 1 && !capture.m_frameTimer.isActive() && !capture.m_frames->take().isValid(),
          "A frame callback that pauses capture must cancel the already armed next tick and flush its mailbox.");
    capture.stop();
    check(!capture.m_frameTimer.isActive() && !capture.m_frameClock.isValid(),
          "Stopping capture must release its frame clock and cancel all future presentation ticks.");
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
struct MappingState {
    QSemaphore entered;
    QSemaphore release;
    std::atomic<QThread *> thread{nullptr};
    std::atomic<int> maps{0}, unmaps{0}, destroyed{0};
    bool fail = false;
};

// The gate makes lifecycle and mailbox tests deterministic without using a
// large image, a machine-specific conversion deadline, or a real wallpaper.
class GatedRgbBuffer final : public QAbstractVideoBuffer {
public:
    GatedRgbBuffer(const QColor &color, std::shared_ptr<MappingState> state)
        : m_state(std::move(state)), m_pixels(QSize(12, 8), QImage::Format_RGBX8888)
    { m_pixels.fill(color); }
    ~GatedRgbBuffer() override { ++m_state->destroyed; }
    MapData map(QVideoFrame::MapMode mode) override
    {
        if (mode != QVideoFrame::ReadOnly)
            return {};
        ++m_state->maps;
        m_state->thread.store(QThread::currentThread());
        m_state->entered.release();
        m_state->release.acquire();
        if (m_state->fail)
            return {};
        MapData data;
        data.planeCount = 1;
        data.bytesPerLine[0] = m_pixels.bytesPerLine();
        data.data[0] = m_pixels.bits();
        data.dataSize[0] = int(m_pixels.sizeInBytes());
        return data;
    }
    void unmap() override { ++m_state->unmaps; }
    QVideoFrameFormat format() const override
    { return QVideoFrameFormat(m_pixels.size(), QVideoFrameFormat::Format_RGBX8888); }
private:
    std::shared_ptr<MappingState> m_state;
    QImage m_pixels;
};

struct ReleaseMapping {
    std::shared_ptr<MappingState> state;
    ~ReleaseMapping() { state->release.release(); }
};

QVideoFrame rgbFrame(const QColor &color)
{
    QVideoFrame frame(QVideoFrameFormat(QSize(12, 8), QVideoFrameFormat::Format_RGBA8888));
    check(frame.map(QVideoFrame::WriteOnly), "The owned mailbox color fixture must be writable.");
    QImage pixels(frame.bits(0), frame.width(), frame.height(), frame.bytesPerLine(0), QImage::Format_RGBA8888);
    pixels.fill(color);
    frame.unmap();
    return frame;
}

void enableFixture(WallpaperEngineCapture &capture)
{
    capture.m_requested = true;
    capture.m_frames->reset(true);
    capture.m_frameClock.start();
}

void tick(WallpaperEngineCapture &capture)
{
    capture.m_frameSchedule.reset(-1000000000);
    capture.publishFrame();
    capture.m_frameTimer.stop(); // Keep the test's presentation ticks explicit.
}

void sinkThreadTest()
{
    // These callbacks can also run during capture teardown after a failed
    // assertion. Keep their storage alive until its worker has been joined.
    std::atomic<int> setters{0}, notifications{0};
    std::atomic<QThread *> signalThread{nullptr};
    QSemaphore drained;
    WallpaperEngineCapture capture;
    check(capture.m_sink->parent() == nullptr && capture.m_sinkThread->isRunning()
              && capture.m_sink->thread() == capture.m_sinkThread
              && capture.m_sink->thread() != qApp->thread(),
          "The parentless capture sink must drain upstream frame events outside the GUI thread.");
    for (QObject *child : capture.m_sink->findChildren<QObject *>())
        check(child->thread() == capture.m_sinkThread,
              "Platform sink children must follow their frontend's event thread.");
    enableFixture(capture);
    QObject::connect(capture.m_sink, &QVideoSink::videoFrameChanged, &capture,
        [&](const QVideoFrame &frame) {
            if (frame.isValid()) {
                ++notifications;
                signalThread.store(QThread::currentThread());
            }
        }, Qt::DirectConnection);
    const auto burst = [&](int count, const QColor &color) {
        for (int frame = 0; frame < count; ++frame) {
            check(QMetaObject::invokeMethod(capture.m_sink,
                [sink = capture.m_sink, pixels = rgbFrame(color), &setters] {
                    sink->setVideoFrame(pixels);
                    ++setters;
                }, Qt::QueuedConnection), "The owned upstream burst must be queueable to the sink.");
        }
    };
    burst(500, Qt::green);
    QMetaObject::invokeMethod(capture.m_sink, [&] { drained.release(); }, Qt::QueuedConnection);
    // Deliberately do not pump GUI events: this reproduces a stalled paint loop.
    QThread::msleep(20);
    check(drained.tryAcquire(1, 3000) && setters == 500 && notifications > 0
              && signalThread.load() == capture.m_sinkThread,
          "Queued capture bursts must drain while the GUI is not processing its event queue.");
    const QVideoFrame latest = capture.m_frames->take();
    check(latest.isValid() && VideoFrames::toRgbImage(latest).pixelColor(5, 4) == QColor(Qt::green)
              && !capture.m_frames->take().isValid() && !capture.m_rgbConversionBusy,
          "The sink event thread must retain one latest frame without converting or queueing GUI work.");

    burst(250, Qt::red);
    capture.setPaused(true);
    check(setters == 750 && !capture.m_frames->take().isValid(),
          "Pausing must disable the mailbox and synchronously drain old queued sink frames.");
    capture.m_requested = false; // This owned fixture resumes without an external renderer command.
    capture.setPaused(false);
    enableFixture(capture);
    check(!capture.m_frames->take().isValid(),
          "Resuming must not resurrect any queued frame from the paused source.");
    burst(250, Qt::red);
    capture.stop();
    check(setters == 1000 && !capture.m_frames->take().isValid(),
          "Stopping must drain the sink before the next source can enable the mailbox.");
    enableFixture(capture);
    check(!capture.m_frames->take().isValid(), "A new source must start with no old sink frames.");
    burst(1, Qt::blue);
    QMetaObject::invokeMethod(capture.m_sink, [&] { drained.release(); }, Qt::QueuedConnection);
    check(drained.tryAcquire(1, 3000)
              && VideoFrames::toRgbImage(capture.m_frames->take()).pixelColor(5, 4) == QColor(Qt::blue),
          "The sink worker must remain usable after pause and source stop barriers.");
    QMetaObject::invokeMethod(capture.m_sink, [&] {
        capture.drainSinkFrames();
        drained.release();
    }, Qt::QueuedConnection);
    check(drained.tryAcquire(1, 3000), "A sink-thread drain call must avoid a BlockingQueued self-deadlock.");
    capture.stop();
    capture.m_sinkThread->quit();
    capture.m_sinkThread->wait();
    capture.drainSinkFrames(); // An unstarted/stopped event thread has no blocking barrier.

    for (int attempt = 0; attempt < 4; ++attempt) {
        auto *temporary = new WallpaperEngineCapture;
        QPointer<QVideoSink> sink(temporary->m_sink);
        QPointer<QThread> thread(temporary->m_sinkThread);
        QThread *destructionThread = nullptr;
        QObject::connect(temporary->m_sink, &QObject::destroyed, qApp, [&] {
            destructionThread = QThread::currentThread();
        }, Qt::DirectConnection);
        delete temporary;
        check(sink.isNull() && thread.isNull() && destructionThread == qApp->thread(),
              "Repeated immediate teardown must return/detach/delete the sink on GUI and join its event thread.");
    }
}

void asyncConversionTest()
{
    WallpaperEngineCapture capture;
    enableFixture(capture);
    QVector<QColor> delivered;
    capture.onFrame = [&](const QImage &image) {
        check(QThread::currentThread() == qApp->thread(), "Frame callbacks must remain on the GUI thread.");
        delivered.append(image.pixelColor(5, 4));
    };
    auto blocked = std::make_shared<MappingState>();
    ReleaseMapping release{blocked};
    capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::red, blocked)));
    tick(capture);
    check(capture.m_rgbConversionBusy && until([&] { return blocked->entered.available() > 0; }),
          "The first RGB mapping must run as one background task.");
    check(blocked->thread.load() != qApp->thread(), "RGB mapping must never block the GUI thread.");
    int heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(3);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start();
    auto queued = std::make_shared<MappingState>();
    ReleaseMapping unblockQueued{queued};
    capture.m_frames->offer(rgbFrame(Qt::blue));
    capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::green, queued)));
    for (int attempt = 0; attempt < 4; ++attempt)
        tick(capture);
    events(25);
    heartbeat.stop();
    check(heartbeats > 0 && blocked->maps == 1 && capture.m_rgbConversionBusy && delivered.isEmpty(),
          "An occupied conversion slot must leave input responsive and retain only the latest waiting frame.");
    blocked->release.release();
    check(until([&] { return queued->entered.available() > 0; }) && capture.m_rgbConversionBusy
              && blocked->maps == 1 && queued->maps == 1 && delivered.isEmpty()
              && capture.m_pendingImage.pixelColor(5, 4) == QColor(Qt::red),
          "Completion must immediately start the latest RGB conversion while retaining the finished image for its display tick.");
    tick(capture);
    check(delivered == QVector<QColor>{QColor(Qt::red)} && capture.m_rgbConversionBusy,
          "A display tick must present the completed image while the one next conversion is already running.");
    queued->release.release();
    check(until([&] { return !capture.m_rgbConversionBusy; })
              && delivered == QVector<QColor>{QColor(Qt::red)},
          "The next RGB conversion must finish without invoking an unpaced frame callback.");
    tick(capture);
    check(delivered == QVector<QColor>{QColor(Qt::red), QColor(Qt::green)}
              && capture.m_pendingImage.isNull() && !capture.m_rgbConversionBusy,
          "A busy burst must publish the final green frame and discard the superseded blue frame.");

    const auto invalidateDuringJob = [&](bool pause) {
        const int before = delivered.size();
        auto stale = std::make_shared<MappingState>();
        ReleaseMapping unblock{stale};
        capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::yellow, stale)));
        tick(capture);
        check(until([&] { return stale->entered.available() > 0; }), "The generation fixture must enter mapping.");
        const quint64 oldGeneration = capture.m_conversionGeneration;
        if (pause) {
            capture.setPaused(true);
            capture.m_requested = false; // Resume this owned fixture without an external engine command.
            capture.setPaused(false);
        } else {
            capture.stop();
        }
        enableFixture(capture);
        auto fresh = std::make_shared<MappingState>();
        ReleaseMapping unblockFresh{fresh};
        capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::blue, fresh)));
        tick(capture);
        check(capture.m_conversionGeneration != oldGeneration && capture.m_rgbConversionBusy
                  && capture.m_rgbJobGeneration == oldGeneration && delivered.size() == before,
              "Pause/restart must not launch a second job while an obsolete conversion is still running.");
        stale->release.release();
        check(until([&] { return fresh->entered.available() > 0; }) && capture.m_rgbConversionBusy
                  && capture.m_rgbJobGeneration == capture.m_conversionGeneration
                  && capture.m_pendingImage.isNull() && delivered.size() == before,
              "An obsolete completion must discard its image and immediately start only the new source's waiting RGB frame.");
        fresh->release.release();
        check(until([&] { return !capture.m_rgbConversionBusy; }) && delivered.size() == before,
              "The new generation's RGB frame must finish without an unpaced callback.");
        tick(capture);
        check(delivered.size() == before + 1 && delivered.last() == QColor(Qt::blue),
              "The new generation must remain usable after quarantining its previous worker.");
    };
    invalidateDuringJob(true);
    invalidateDuringJob(false);

    capture.setRenderSize(QSize(1200, 800));
    auto resizing = std::make_shared<MappingState>();
    ReleaseMapping unblockResize{resizing};
    capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::yellow, resizing)));
    tick(capture);
    check(until([&] { return resizing->entered.available() > 0; }), "The resize fixture must enter background mapping.");
    capture.m_frames->offer(rgbFrame(Qt::blue));
    const quint64 beforeResize = capture.m_conversionGeneration;
    capture.setRenderSize(QSize(1200, 800));
    check(capture.m_conversionGeneration == beforeResize && capture.m_frames->take().isValid(),
          "The native visibility guard's unchanged render size must preserve its generation and waiting frame.");
    capture.m_frames->offer(rgbFrame(Qt::blue));
    capture.setRenderSize(QSize(1201, 801));
    check(capture.m_conversionGeneration != beforeResize && capture.m_rgbConversionBusy
              && !capture.m_frames->take().isValid() && capture.m_pendingImage.isNull(),
          "A physical-pixel resize must flush old-size frames without starting a second conversion task.");
    const int beforeResizedFrame = delivered.size();
    auto resized = std::make_shared<MappingState>();
    ReleaseMapping unblockResized{resized};
    capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::green, resized)));
    tick(capture);
    resizing->release.release();
    check(until([&] { return resized->entered.available() > 0; }) && capture.m_rgbConversionBusy
              && capture.m_rgbJobGeneration == capture.m_conversionGeneration && capture.m_pendingImage.isNull()
              && delivered.size() == beforeResizedFrame,
          "A job finishing after resize must quarantine its previous-size image and kick the new-size conversion.");
    resized->release.release();
    check(until([&] { return !capture.m_rgbConversionBusy; }), "A new-size generation must resume conversion.");
    const quint64 resizedGeneration = capture.m_conversionGeneration;
    capture.setRenderSize(QSize(1201, 801));
    check(capture.m_conversionGeneration == resizedGeneration
              && capture.m_pendingImage.pixelColor(5, 4) == QColor(Qt::green),
          "An unchanged render size must preserve an already converted image waiting for presentation.");
    tick(capture);
    check(delivered.size() == beforeResizedFrame + 1 && delivered.last() == QColor(Qt::green),
          "Only the post-resize frame may be presented after the obsolete conversion finishes.");

    auto beforeFallback = std::make_shared<MappingState>();
    ReleaseMapping unblockBeforeFallback{beforeFallback};
    capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::red, beforeFallback)));
    tick(capture);
    check(until([&] { return beforeFallback->entered.available() > 0; }),
          "The incompatible-frame fixture must enter its one RGB conversion.");
    const QVideoFrame nonRgb(QVideoFrameFormat(QSize(12, 8), QVideoFrameFormat::Format_NV12));
    check(nonRgb.isValid() && !VideoFrames::canConvertRgb(nonRgb),
          "The owned non-RGB fixture must require the GUI-only conversion path.");
    capture.m_frames->offer(nonRgb);
    const int beforeFallbackFrame = delivered.size();
    beforeFallback->release.release();
    check(until([&] { return !capture.m_rgbConversionBusy; })
              && capture.m_pendingImage.pixelColor(5, 4) == QColor(Qt::red)
              && !capture.m_frameTimer.isActive() && delivered.size() == beforeFallbackFrame,
          "A completion with a non-RGB waiting frame must neither start its conversion, publish, nor rearm the display timer.");
    check(capture.m_frames->take().pixelFormat() == QVideoFrameFormat::Format_NV12,
          "An incompatible waiting frame must stay in the mailbox for the GUI display tick.");
    tick(capture);
    check(delivered.size() == beforeFallbackFrame + 1 && delivered.last() == QColor(Qt::red),
          "A completed RGB image must remain usable after declining the next frame's worker conversion.");

    auto failed = std::make_shared<MappingState>();
    ReleaseMapping unblockFailed{failed};
    failed->fail = true;
    capture.m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::yellow, failed)));
    tick(capture);
    check(until([&] { return failed->entered.available() > 0; }), "The failed-map fixture must start on the worker.");
    failed->release.release();
    const int beforeFailure = delivered.size();
    check(until([&] { return !capture.m_rgbConversionBusy; }) && capture.m_pendingImage.isNull()
              && failed->maps == 1 && delivered.size() == beforeFailure,
          "A failed RGB mapping must return null without invoking Qt/QRhi fallback or a frame callback.");
    capture.stop();
}

void conversionLifecycleTest()
{
    auto state = std::make_shared<MappingState>();
    ReleaseMapping release{state};
    auto *capture = new WallpaperEngineCapture;
    QPointer<WallpaperEngineCapture> guard(capture);
    enableFixture(*capture);
    int callbacks = 0;
    capture->onFrame = [&](const QImage &) { ++callbacks; };
    capture->m_frames->offer(QVideoFrame(std::make_unique<GatedRgbBuffer>(Qt::blue, state)));
    tick(*capture);
    check(until([&] { return state->entered.available() > 0; }), "The destruction fixture must enter its worker mapping.");
    delete capture;
    check(guard.isNull() && state->unmaps == 0,
          "Capture destruction must return while its independently owned worker is still blocked.");
    state->release.release();
    check(until([&] { return state->unmaps == 1 && state->destroyed == 1; }) && callbacks == 0,
          "A late worker must safely release its frame after receiver destruction without callbacks.");

    WallpaperEngineCapture stopped;
    enableFixture(stopped);
    stopped.m_pendingImage = QImage(12, 8, QImage::Format_RGB32);
    stopped.m_pendingImage.fill(Qt::red);
    stopped.onStatusChanged = [&](const QString &) { stopped.stop(); };
    stopped.onFrame = [&](const QImage &) { ++callbacks; };
    tick(stopped);
    check(callbacks == 0 && !stopped.m_frameTimer.isActive(),
          "A status callback stopping the source must suppress its frame callback and future ticks.");

    capture = new WallpaperEngineCapture;
    guard = capture;
    enableFixture(*capture);
    capture->m_pendingImage = QImage(12, 8, QImage::Format_RGB32);
    capture->m_pendingImage.fill(Qt::blue);
    capture->onFrame = [&](const QImage &) { ++callbacks; delete capture; };
    capture->m_frameSchedule.reset(-1000000000);
    capture->publishFrame();
    check(guard.isNull() && callbacks == 1,
          "A copied frame callback may destroy the receiver without subsequent access or timer rearming.");
}
#endif

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
    explicit NativeFixture(const QString &title, QSize size = QSize(900, 600), bool visible = true)
    {
        const QPoint position = rendererParkingPosition();
        window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC",
            reinterpret_cast<LPCWSTR>(title.utf16()), WS_POPUP | (visible ? WS_VISIBLE : 0),
            position.x(), position.y(), size.width(), size.height(), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
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

QString rendererFixtureName()
{
    return QStringLiteral("PHSRadioWallpaper_%1_%2")
        .arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::Id128));
}

void captureReadinessTest()
{
    const QString name = rendererFixtureName();
    NativeFixture renderer(name, QSize(0, 0));
    NativeFixture unrelated(QStringLiteral("UnrelatedReadinessFixture_%1")
        .arg(QCoreApplication::applicationPid()));
    const QSize foreignSize = nativeSize(unrelated.window);
    WallpaperEngineCapture capture;
    capture.m_requested = true;
    capture.m_windowName = name;
    capture.m_nativeWindow = reinterpret_cast<quintptr>(renderer.window);
    capture.m_renderSize = QSize(900, 600);
    check(!capture.captureWindowReady() && !capture.captureWindowReady()
              && nativeSize(renderer.window) == QSize(0, 0),
          "Readiness must reject a newly created zero-size surface without resizing it into a false ready state.");
    check(SetWindowPos(renderer.window, nullptr, 0, 0, 900, 600,
              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
          "The owned readiness fixture must accept a valid surface size.");
    check(!capture.captureWindowReady() && !capture.captureWindowReady() && capture.captureWindowReady(),
          "A valid named surface must be stable for three discovery polls before it is captured.");
    check(SetWindowPos(renderer.window, nullptr, 0, 0, 901, 601,
              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
          "The owned readiness fixture must accept a renderer-initialization size change.");
    check(!capture.captureWindowReady() && !capture.captureWindowReady() && capture.captureWindowReady(),
          "A surface size change must restart readiness polling rather than inherit another size's stability.");

    ShowWindow(renderer.window, SW_HIDE);
    check(!capture.captureWindowReady() && !capture.captureWindowReady(),
          "A hidden renderer must never be considered ready or be made visible by the readiness probe.");
    ShowWindow(renderer.window, SW_SHOWNOACTIVATE);
    ShowWindow(renderer.window, SW_MINIMIZE);
    check(IsIconic(renderer.window) && !capture.captureWindowReady(),
          "A minimized renderer must be rejected before Qt capture can receive an invalid surface.");
    ShowWindow(renderer.window, SW_RESTORE);
    check(!capture.captureWindowReady() && !capture.captureWindowReady() && capture.captureWindowReady(),
          "Restoring a renderer must require new stable polls instead of using its pre-minimize candidate.");

    ShowWindow(renderer.window, SW_HIDE);
    capture.m_nativeWindow = reinterpret_cast<quintptr>(unrelated.window);
    check(!capture.captureWindowReady() && !capture.m_capture->isActive(),
          "A stale HWND must not capture an unrelated visible window while the exact named renderer is unready.");
    capture.stop();
    events(20);
    check(IsWindow(unrelated.window) && IsWindowVisible(unrelated.window)
              && nativeSize(unrelated.window) == foreignSize,
          "Readiness teardown must leave every window outside this source's unique name intact.");
}

void prepareRecoveryFixture(WallpaperEngineCapture &capture, const QString &name, HWND window)
{
    // The renderer is deliberately hidden. Discovery can inspect only this
    // test-owned HWND, but cannot start a real backend or external WE command.
    capture.m_requested = true;
    capture.m_windowName = name;
    capture.m_nativeWindow = reinterpret_cast<quintptr>(window);
    capture.m_renderSize = QSize(900, 600);
    capture.m_timeout.start(18000);
}

void captureRecoveryTest()
{
    const QString name = rendererFixtureName();
    NativeFixture renderer(name, QSize(900, 600), false);
    NativeFixture unrelated(QStringLiteral("UnrelatedRecoveryFixture_%1")
        .arg(QCoreApplication::applicationPid()));
    const QSize foreignSize = nativeSize(unrelated.window);
    WallpaperEngineCapture capture;
    prepareRecoveryFixture(capture, name, renderer.window);
    int errors = 0;
    QString error;
    capture.onError = [&](const QString &message) { ++errors; error = message; };
    const int originalDeadline = capture.m_timeout.remainingTime();
    const auto inject = [&](QWindowCapture::Error code) {
        capture.m_capture->errorOccurred(code, QStringLiteral("Owned capture fixture: invalid argument"));
    };
    inject(QWindowCapture::CaptureFailed);
    inject(QWindowCapture::InternalError);
    check(capture.m_requested && capture.m_captureErrorPending && errors == 0
              && IsWindow(renderer.window) && !capture.m_capture->isActive(),
          "Synchronous Qt initialization errors must coalesce without closing the named renderer or reentering backend destruction.");
    check(until([&] { return capture.m_captureRecoveryAttempts == 1 && !capture.m_captureErrorPending; }, 1800)
              && capture.m_findTimer.isActive() && IsWindow(renderer.window) && errors == 0,
          "Transient capture initialization failure must defer one rediscovery while retaining the owned renderer.");
    check(capture.m_timeout.isActive() && capture.m_timeout.remainingTime() < originalDeadline,
          "A delayed recovery must retain the original total startup deadline instead of extending it per attempt.");
    for (int attempt = 2; attempt <= 3; ++attempt) {
        inject(attempt == 2 ? QWindowCapture::InternalError : QWindowCapture::CaptureFailed);
        check(until([&] { return capture.m_captureRecoveryAttempts == attempt && !capture.m_captureErrorPending; }, 1800)
                  && capture.m_requested && errors == 0 && IsWindow(renderer.window),
              "Each transient backend failure may perform only one further bounded deferred recovery.");
    }
    const int beforeExhaustion = capture.m_captureRecoveryAttempts;
    inject(QWindowCapture::CaptureFailed);
    check(errors == 0 && IsWindow(renderer.window),
          "Even retry-budget exhaustion must defer terminal cleanup until the Qt error signal has unwound.");
    check(until([&] { return errors == 1; }, 1800) && !capture.m_requested
              && beforeExhaustion == 3 && !error.isEmpty()
              && !capture.m_findTimer.isActive() && !capture.m_timeout.isActive(),
          "Capture recovery must stop after three attempts and report one terminal error, never enter an unbounded retry loop.");
    events(400);
    check(errors == 1 && !capture.m_requested && !IsWindow(renderer.window) && IsWindow(unrelated.window)
              && IsWindowVisible(unrelated.window) && nativeSize(unrelated.window) == foreignSize,
          "Exhausted recovery must not resurrect its renderer or alter any unrelated desktop window.");

    const QString deadlineName = rendererFixtureName();
    NativeFixture deadlineRenderer(deadlineName, QSize(900, 600), false);
    WallpaperEngineCapture deadline;
    prepareRecoveryFixture(deadline, deadlineName, deadlineRenderer.window);
    int deadlineErrors = 0;
    deadline.onError = [&](const QString &) { ++deadlineErrors; };
    deadline.m_timeout.start(40);
    deadline.m_capture->errorOccurred(QWindowCapture::InternalError, QStringLiteral("Owned startup deadline fixture"));
    check(until([&] { return deadlineErrors == 1; }, 900) && !deadline.m_requested,
          "The total startup timeout must cancel a pending recovery rather than wait for or restart its retry delay.");
    events(400);
    check(deadlineErrors == 1 && !deadline.m_findTimer.isActive() && !deadline.m_capture->isActive()
              && !IsWindow(deadlineRenderer.window),
          "A recovery callback arriving after the total deadline must remain cancelled.");
}

void captureRecoveryCancellationTest()
{
    // Cancel once before backend cleanup and once while the longer rediscovery
    // delay is pending. Both callbacks belong to the original source only.
    for (int scenario = 0; scenario < 6; ++scenario) {
        const int deferredStage = scenario / 3;
        const int action = scenario % 3;
        const QString name = rendererFixtureName();
        NativeFixture renderer(name, QSize(900, 600), false);
        const QString nextName = rendererFixtureName();
        NativeFixture nextRenderer(nextName, QSize(900, 600), false);
        WallpaperEngineCapture capture;
        prepareRecoveryFixture(capture, name, renderer.window);
        int errors = 0;
        capture.onError = [&](const QString &) { ++errors; };
        capture.m_capture->errorOccurred(QWindowCapture::CaptureFailed, QStringLiteral("Owned cancellation fixture"));
        if (deferredStage) {
            check(until([&] { return capture.m_captureRecoveryAttempts == 1; }, 200)
                      && capture.m_captureErrorPending,
                  "The cancellation fixture must reach deferred retry after its backend cleanup.");
        } else {
            check(capture.m_captureErrorPending && capture.m_captureRecoveryAttempts == 0,
                  "The cancellation fixture must retain its queued cleanup until the error signal unwinds.");
        }
        const quint64 previousGeneration = capture.m_captureGeneration;
        if (action == 0) {
            capture.stop();
        } else if (action == 1) {
            capture.setPaused(true);
        } else {
            capture.stop();
            prepareRecoveryFixture(capture, nextName, nextRenderer.window);
        }
        check(capture.m_captureGeneration != previousGeneration,
              "Stop, pause and source replacement must invalidate every old deferred capture task.");
        events(450);
        check(errors == 0 && !capture.m_captureErrorPending && !capture.m_findTimer.isActive()
                  && !capture.m_capture->isActive(),
              "An obsolete retry must neither start capture, restart discovery nor report an error in a later source state.");
        if (action == 1) {
            check(capture.m_requested && capture.m_paused && !capture.m_timeout.isActive()
                      && IsWindow(renderer.window) && !IsWindowVisible(renderer.window),
                  "Pausing during recovery must retain the requested source while suspending its deadline and retries.");
            capture.m_capture->errorOccurred(QWindowCapture::InternalError, QStringLiteral("Owned paused-source fixture"));
            events(10);
            check(errors == 0 && !capture.m_captureErrorPending,
                  "Backend errors arriving while paused must not schedule source recovery.");
        } else if (action == 2) {
            check(capture.m_requested && capture.m_windowName == nextName && IsWindow(nextRenderer.window)
                      && !IsWindow(renderer.window),
                  "Cancelling the previous source's recovery must preserve the new named renderer.");
        } else {
            check(!capture.m_requested && !IsWindow(renderer.window),
                  "A stopped source must close only its named renderer and remain stopped after its deferred retry deadline.");
        }
        capture.stop();
    }

    const QString name = rendererFixtureName();
    NativeFixture unsupportedRenderer(name, QSize(900, 600), false);
    WallpaperEngineCapture unsupported;
    prepareRecoveryFixture(unsupported, name, unsupportedRenderer.window);
    int errors = 0;
    unsupported.onError = [&](const QString &) { ++errors; };
    unsupported.m_capture->errorOccurred(QWindowCapture::CapturingNotSupported,
        QStringLiteral("Owned unsupported-backend fixture"));
    check(errors == 0 && unsupported.m_requested && IsWindow(unsupportedRenderer.window),
          "An unsupported backend must defer cleanup even though it cannot be retried.");
    check(until([&] { return errors == 1; }, 900) && !unsupported.m_requested
              && !unsupported.m_findTimer.isActive(),
          "Unsupported window capture must produce one terminal failure without scheduling rediscovery.");
    events(400);
    check(errors == 1 && !unsupported.m_capture->isActive() && !IsWindow(unsupportedRenderer.window),
          "An unsupported backend must remain stopped after the transient-recovery delay.");

    const QString lifetimeName = rendererFixtureName();
    NativeFixture lifetimeRenderer(lifetimeName, QSize(900, 600), false);
    auto *destroyed = new WallpaperEngineCapture;
    QPointer<WallpaperEngineCapture> guard(destroyed);
    prepareRecoveryFixture(*destroyed, lifetimeName, lifetimeRenderer.window);
    int terminalCallbacks = 0;
    destroyed->onError = [&](const QString &) { ++terminalCallbacks; delete destroyed; };
    destroyed->m_capture->errorOccurred(QWindowCapture::CapturingNotSupported,
        QStringLiteral("Owned callback lifetime fixture"));
    check(until([&] { return guard.isNull(); }, 900) && terminalCallbacks == 1,
          "A deferred terminal error callback may destroy capture without accessing its deleted backend afterward.");
    events(400);
    check(terminalCallbacks == 1, "Receiver destruction must cancel every pending deferred recovery callback.");
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
    check(until([&] {return (count >= 20 && frames.size() >= 10) || !error.isEmpty();}),
          "A fully transparent cold start must deliver distinct dynamic frames after initialization.");
    std::fprintf(stderr, "Cold-start capture: frames=%d distinct=%lld first=%dx%d opaque=%d elapsed_ms=%lld error=%s\n",
                 count, static_cast<long long>(frames.size()), first.width(), first.height(), opaque,
                 static_cast<long long>(time.elapsed()), error.toUtf8().constData());
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
        frameRateConfigurationTest();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        sinkThreadTest();
        asyncConversionTest();
        conversionLifecycleTest();
#endif
        descriptorTest();
#ifdef Q_OS_WIN
        if (application.arguments().size() == 4 && application.arguments().at(1) == QStringLiteral("--live"))
            liveEngineTest(application.arguments().at(2), application.arguments().at(3));
        else {
            captureReadinessTest();
            captureRecoveryTest();
            captureRecoveryCancellationTest();
            nativeIsolationTest();
        }
#endif
        qInfo("Wallpaper Engine capture regressions passed.");
        return 0;
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
}
