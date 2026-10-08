#include <QtWidgets>
#include <QtMultimedia>
#include <functional>
#define private public
#include "../aero_surface.h"
#undef private
#include "../liquid_backdrop.h"
#include "../latest_video_frame.h"

#include <cstdlib>
#include <cstdio>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        qCritical("%s", message);
        std::abort();
    }
}

void events(int duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

bool until(const std::function<bool()> &predicate, int timeout = 6000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout)
        events(10);
    return predicate();
}

QByteArray chunk(const char *tag, const QByteArray &payload)
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(tag, 4);
    stream << quint32(payload.size());
    stream.writeRawData(payload.constData(), payload.size());
    if (payload.size() % 2)
        stream << quint8(0);
    return result;
}

QByteArray list(const char *type, const QByteArray &payload)
{
    return chunk("LIST", QByteArray(type, 4) + payload);
}

// A tiny, owned Motion-JPEG AVI exercises actual decoder output and looping.
// No downloaded video, external encoder, account or installed wallpaper is used.
void writeMovie(const QString &path)
{
    constexpr int width = 64, height = 48, count = 8, rate = 8;
    QByteArray frames, index;
    QDataStream indices(&index, QIODevice::WriteOnly);
    indices.setByteOrder(QDataStream::LittleEndian);
    for (int frame = 0; frame < count; ++frame) {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(frame % 2 ? QColor(20, 40, 220) : QColor(220, 30, 20));
        QByteArray jpeg;
        QBuffer output(&jpeg);
        check(output.open(QIODevice::WriteOnly) && image.save(&output, "JPEG"),
              "The local animated fixture requires the bundled JPEG image plugin.");
        indices.writeRawData("00dc", 4);
        indices << quint32(0x10) << quint32(4 + frames.size()) << quint32(jpeg.size());
        frames += chunk("00dc", jpeg);
    }
    QByteArray avih, strh, strf;
    QDataStream header(&avih, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(1000000 / rate) << quint32(0) << quint32(0) << quint32(0x10)
           << quint32(count) << quint32(0) << quint32(1) << quint32(64 * 48 * 3)
           << quint32(width) << quint32(height) << quint32(0) << quint32(0) << quint32(0) << quint32(0);
    QDataStream stream(&strh, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("vidsMJPG", 8);
    stream << quint32(0) << quint16(0) << quint16(0) << quint32(0) << quint32(1) << quint32(rate)
           << quint32(0) << quint32(count) << quint32(64 * 48 * 3) << quint32(0xffffffff)
           << quint32(0) << qint16(0) << qint16(0) << qint16(width) << qint16(height);
    QDataStream format(&strf, QIODevice::WriteOnly);
    format.setByteOrder(QDataStream::LittleEndian);
    format << quint32(40) << qint32(width) << qint32(height) << quint16(1) << quint16(24);
    format.writeRawData("MJPG", 4);
    format << quint32(width * height * 3) << qint32(0) << qint32(0) << quint32(0) << quint32(0);
    const QByteArray contents = QByteArray("AVI ", 4)
        + list("hdrl", chunk("avih", avih) + list("strl", chunk("strh", strh) + chunk("strf", strf)))
        + list("movi", frames) + chunk("idx1", index);
    QFile file(path);
    const QByteArray riff = chunk("RIFF", contents);
    check(file.open(QIODevice::WriteOnly) && file.write(riff) == riff.size(), "The movie fixture must be writable.");
}

QImage sample(AeroSurface &surface, const QRect &bounds)
{
    QImage result(bounds.size(), QImage::Format_RGB32);
    QPainter painter(&result);
    surface.paintWater(painter, result.rect(), bounds.topLeft());
    return result;
}

class CountingContent final : public QWidget {
public:
    using QWidget::QWidget;
    int paints = 0;
protected:
    void paintEvent(QPaintEvent *) override
    {
        ++paints;
        QPainter painter(this);
        painter.fillRect(QRect(10, 10, 30, 30), Qt::white);
    }
};

void sampleGlass(QWidget &panel)
{
    QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    Liquid::paintBackdrop(painter, &panel, 12);
}

QImage sampleBlur(AeroSurface &surface)
{
    QImage result(surface.size(), QImage::Format_RGB32);
    QPainter painter(&result);
    surface.paintBlurredBackground(painter, surface.rect());
    return result;
}

void checkFractionalFrameSharing(const QTemporaryDir &directory)
{
    const QString projectPath = directory.filePath(QStringLiteral("pixel-project.json"));
    QFile project(projectPath);
    check(project.open(QIODevice::WriteOnly) && project.write("{}") == 2,
          "The fractional-DPI fixture requires its own project file.");
    project.close();
    AeroSurface surface;
    BackgroundTheme theme;
    theme.kind = BackgroundKind::WallpaperEngine;
    theme.sourcePath = projectPath;
    theme.dimming = 0;
    check(surface.setBackground(theme), "The pixel-sharing fixture must accept its owned project.");
    check(qFuzzyCompare(surface.devicePixelRatioF(), qreal(2.25)),
          "The isolated pixel-sharing fixture must run at fractional high DPI.");
    for (const QSize logical : {QSize(319, 197), QSize(641, 359), QSize(997, 613)}) {
        surface.resize(logical);
        const qreal dpr = surface.devicePixelRatioF();
        const QSize physical(qCeil(logical.width() * dpr), qCeil(logical.height() * dpr));
        check(!qFuzzyCompare(qreal(physical.width()) / physical.height(),
                             qreal(logical.width()) / logical.height()),
              "The sharing fixture must exercise unequal logical and rounded physical aspect ratios.");
        QImage frame(physical, QImage::Format_RGB32);
        frame.fill(QColor(21, 140, 210));
        frame.setPixelColor(0, 0, Qt::red);
        frame.setPixelColor(physical.width() - 1, physical.height() - 1, Qt::green);
        frame.setPixelColor(physical.width() / 2, physical.height() / 2, Qt::blue);
        surface.setBackgroundFrame(frame);
        surface.ensureBackgroundTexture();
        check(surface.m_backgroundTexture.size() == physical
            && surface.m_backgroundTexture.constBits() == frame.constBits()
            && surface.m_backgroundTexture == frame,
              "An exact physical-size WE frame must share all original pixels without logical-aspect rescaling.");
        check(qFuzzyCompare(surface.m_backgroundTextureDpr, dpr)
            && surface.m_backgroundTextureFrameRevision == surface.m_backgroundFrameRevision,
              "The shared frame must retain current DPI and revision cache metadata.");
        QImage next(physical, QImage::Format_RGB32);
        next.fill(Qt::yellow);
        surface.setBackgroundFrame(next);
        surface.ensureBackgroundTexture();
        check(surface.m_backgroundTexture.constBits() == next.constBits()
            && frame.pixelColor(0, 0) == QColor(Qt::red),
              "A following shared frame must replace the texture without mutating the retained previous image.");
    }

    surface.resize(319, 197);
    for (const bool wide : {true, false}) {
        QImage imported(wide ? QSize(1200, 240) : QSize(240, 1200), QImage::Format_RGB32);
        imported.fill(Qt::red);
        {
            QPainter painter(&imported);
            painter.fillRect(wide ? QRect(400, 0, 400, 240) : QRect(0, 400, 240, 400), Qt::green);
            painter.fillRect(wide ? QRect(400, 0, 200, 240) : QRect(0, 400, 240, 200), Qt::blue);
        }
        const QString path = directory.filePath(wide ? QStringLiteral("wide.png") : QStringLiteral("tall.png"));
        check(imported.save(path), "The center-crop fixture must be writable.");
        for (const BackgroundKind kind : {BackgroundKind::Image, BackgroundKind::Video}) {
            theme.kind = BackgroundKind::Image;
            theme.sourcePath = path;
            check(surface.setBackground(theme), "A differently sized import must be accepted.");
            // Only texture construction is under test here. The movie fixture
            // below separately exercises real decoder output and its lifecycle.
            surface.m_background.kind = kind;
            surface.setBackgroundFrame(imported);
            surface.ensureBackgroundTexture();
            const QImage &texture = surface.m_backgroundTexture;
            const qreal dpr = surface.devicePixelRatioF();
            check(texture.size() == QSize(qCeil(surface.width() * dpr), qCeil(surface.height() * dpr))
                && texture.constBits() != imported.constBits(),
                  "A differently sized image/video must create the correct physical-size center-cropped texture.");
            const QPoint first = wide ? QPoint(texture.width() / 4, texture.height() / 2)
                                      : QPoint(texture.width() / 2, texture.height() / 4);
            const QPoint second = wide ? QPoint(texture.width() * 3 / 4, texture.height() / 2)
                                       : QPoint(texture.width() / 2, texture.height() * 3 / 4);
            check(texture.pixelColor(first) == QColor(Qt::blue)
                && texture.pixelColor(second) == QColor(Qt::green)
                && texture.pixelColor(1, 1).red() == 0
                && texture.pixelColor(texture.width() - 2, texture.height() - 2).red() == 0,
                  "Image/video imports must crop outer red bands while retaining the central blue/green orientation.");
        }
    }
}

void checkFractionalFrameSharingProcess()
{
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QT_SCALE_FACTOR"), QStringLiteral("2.25"));
    environment.remove(QStringLiteral("QT_SCREEN_SCALE_FACTORS"));
    environment.remove(QStringLiteral("QT_DEVICE_PIXEL_RATIO"));
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--fractional-frame-sharing")});
    check(child.waitForStarted(5000) && child.waitForFinished(6000),
          "The isolated fractional-DPI regression check must complete.");
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
        std::fprintf(stderr, "%s", child.readAllStandardError().constData());
    check(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
          "Exact-size sharing and differently sized center-crops must pass at fractional high DPI.");
}

void checkDynamicBlur(const QString &projectPath, const QString &imagePath)
{
    AeroSurface surface;
    surface.resize(320, 200);
    BackgroundTheme theme;
    theme.kind = BackgroundKind::WallpaperEngine;
    theme.sourcePath = projectPath;
    theme.dimming = 0;
    check(surface.setBackground(theme), "The asynchronous blur fixture must accept its owned project.");
    QImage frame(surface.size(), QImage::Format_RGB32);
    frame.fill(Qt::red);
    surface.setBackgroundFrame(frame);
    check(sampleBlur(surface).pixelColor(100, 100).red() > 200,
          "The first wallpaper blur must be ready immediately.");
    frame.fill(Qt::blue);
    surface.setBackgroundFrame(frame);
    sampleBlur(surface);
    check(surface.m_backgroundBlurBusy, "Later dynamic frames must schedule an asynchronous blur.");
    const quint64 firstJob = surface.m_backgroundBlurJobFrameRevision;
    for (int index = 0; index < 80; ++index) {
        frame.fill(index % 2 ? Qt::blue : Qt::red);
        surface.setBackgroundFrame(frame);
        sampleBlur(surface);
        check(surface.m_backgroundBlurBusy && surface.m_backgroundBlurJobFrameRevision == firstJob,
              "A decoder burst must retain only one in-flight blur job instead of queuing every frame.");
    }
    frame.fill(Qt::green);
    surface.setBackgroundFrame(frame);
    sampleBlur(surface);
    check(until([&] { return surface.m_backgroundBlurredFrameRevision == surface.m_backgroundFrameRevision
        && !surface.m_backgroundBlurBusy; }), "The pending blur must converge to the newest frame after a decoder burst.");
    check(sampleBlur(surface).pixelColor(100, 100).green() > 200,
          "Discarding intermediate work must still display the final wallpaper frame in glass.");

    frame.fill(Qt::blue);
    surface.m_backgroundBlurClock.invalidate();
    surface.setBackgroundFrame(frame);
    sampleBlur(surface);
    check(surface.m_backgroundBlurBusy, "The resize fixture must start its background job.");
    surface.resize(600, 400);
    frame.fill(Qt::red);
    surface.setBackgroundFrame(frame);
    sampleBlur(surface);
    check(until([&] { return !surface.m_backgroundBlurBusy; }), "The superseded resize job must finish safely.");
    check(surface.m_backgroundBlurred.size() == QSize(100, 66)
        && sampleBlur(surface).pixelColor(100, 100).red() > 200,
          "A late worker result must not replace a new-size glass texture with the previous size.");

    frame.fill(Qt::blue);
    surface.m_backgroundBlurClock.invalidate();
    surface.setBackgroundFrame(frame);
    sampleBlur(surface);
    check(surface.m_backgroundBlurBusy, "The theme-switch fixture must start its worker.");
    BackgroundTheme replacement;
    replacement.kind = BackgroundKind::Image;
    replacement.sourcePath = imagePath;
    replacement.dimming = 0;
    check(surface.setBackground(replacement), "A theme switch must accept its valid static replacement.");
    const QImage immediate = sampleBlur(surface);
    check(until([&] { return !surface.m_backgroundBlurBusy; }), "A superseded theme worker must finish safely.");
    check(sampleBlur(surface) == immediate,
          "A late wallpaper result must never overwrite the immediately rendered replacement theme.");

    auto *temporary = new AeroSurface;
    temporary->resize(640, 400);
    check(temporary->setBackground(theme), "The disposal fixture must accept its owned project.");
    temporary->setBackgroundFrame(frame);
    sampleBlur(*temporary);
    frame.fill(Qt::green);
    temporary->setBackgroundFrame(frame);
    sampleBlur(*temporary);
    check(temporary->m_backgroundBlurBusy, "The disposal fixture must delete a surface with work in flight.");
    delete temporary;
    check(QThreadPool::globalInstance()->waitForDone(6000), "Orphaned image work must complete without referencing a destroyed widget.");
    events(20);
}

void checkBlurConsumerCadence(const QString &projectPath)
{
    AeroSurface surface;
    surface.resize(640, 320);
    BackgroundTheme theme;
    theme.kind = BackgroundKind::WallpaperEngine;
    theme.sourcePath = projectPath;
    theme.dimming = 0;
    check(surface.setBackground(theme), "The consumer cadence fixture must accept its owned project.");
    CountingContent clear(&surface), blurred(&surface);
    clear.setGeometry(10, 10, 280, 280);
    blurred.setGeometry(340, 10, 280, 280);
    surface.registerBackgroundConsumer(&clear);
    surface.registerBlurredBackgroundConsumer(&blurred);
    clear.show();
    blurred.show();
    surface.show();
    surface.activateWindow();
    QImage frame(surface.size(), QImage::Format_RGB32);
    frame.fill(Qt::red);
    surface.setBackgroundFrame(frame);
    sampleBlur(surface);
    events(40);
    frame.fill(Qt::blue);
    surface.setBackgroundFrame(frame);
    // These counting widgets do not initiate a blur themselves. Flush the
    // legitimate source-frame paints first, then start the image worker.
    events(20);
    const int clearPaints = clear.paints, blurredPaints = blurred.paints;
    sampleBlur(surface);
    check(surface.m_backgroundBlurBusy, "The consumer cadence fixture must start an asynchronous blur.");
    check(until([&] { return !surface.m_backgroundBlurBusy; }), "The consumer cadence worker must finish.");
    check(until([&] { return blurred.paints > blurredPaints; }),
          "The final source frame must present its completed blur even without another source frame.");
    check(clear.paints == clearPaints,
          "An asynchronous blur completion must not repaint clear-only song-card viewports.");
    check(blurred.paints > blurredPaints,
          "Panels sampling blurred wallpaper must repaint when its asynchronous result becomes available.");
}

void checkBlurPresentationLifecycle(const QString &projectPath, const QString &imagePath)
{
    BackgroundTheme theme;
    theme.kind = BackgroundKind::WallpaperEngine;
    theme.sourcePath = projectPath;
    theme.dimming = 0;
    const auto prepare = [&theme](AeroSurface &surface) {
        surface.resize(320, 200);
        check(surface.setBackground(theme), "The presentation lifecycle fixture must accept its owned project.");
        surface.show();
        surface.activateWindow();
        events(20);
        QImage frame(surface.size(), QImage::Format_RGB32);
        frame.fill(Qt::red);
        surface.setBackgroundFrame(frame);
        sampleBlur(surface);
        frame.fill(Qt::blue);
        surface.setBackgroundFrame(frame);
        sampleBlur(surface);
        check(until([&] { return !surface.m_backgroundBlurBusy; }), "The presentation fixture's image work must finish.");
        check(surface.m_backgroundBlurPresentTimer->isActive()
            && surface.m_backgroundBlurPresentTimer->interval() == 32,
            "A completed final blur must arm its bounded 32 ms presentation fallback.");
    };
    {
        AeroSurface surface;
        prepare(surface);
        QImage frame(surface.size(), QImage::Format_RGB32);
        frame.fill(Qt::green);
        surface.setBackgroundFrame(frame);
        check(!surface.m_backgroundBlurPresentTimer->isActive(),
              "A new source frame must consume the ready blur without an additional fallback repaint.");
    }
    {
        AeroSurface surface;
        prepare(surface);
        BackgroundTheme image;
        image.kind = BackgroundKind::Image;
        image.sourcePath = imagePath;
        image.dimming = 0;
        check(surface.setBackground(image), "The pending fallback must allow a source replacement.");
        const QImage expected = sampleBlur(surface);
        check(!surface.m_backgroundBlurPresentTimer->isActive(),
              "Switching background sources must cancel a previous source's delayed presentation.");
        events(50);
        check(sampleBlur(surface) == expected,
              "A retired source's fallback must not change the replacement image.");
    }
    {
        AeroSurface surface;
        prepare(surface);
        surface.clearBackgroundFrame();
        check(!surface.m_backgroundBlurPresentTimer->isActive(),
              "Clearing a captured frame must cancel delayed presentation as well.");
    }
    {
        AeroSurface surface;
        prepare(surface);
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&surface, &deactivate);
        check(!surface.m_backgroundBlurPresentTimer->isActive(),
              "Deactivation must cancel delayed presentation and keep inactive backgrounds idle.");
    }
    {
        auto *surface = new AeroSurface;
        prepare(*surface);
        const QPointer<QTimer> timer(surface->m_backgroundBlurPresentTimer);
        delete surface;
        check(timer.isNull(), "Surface destruction must destroy its pending presentation timer.");
        events(50);
    }
}

void checkBackgroundActivitySynchronization()
{
    QWidget player;
    player.resize(320, 200);
    AeroSurface surface(&player);
    surface.setGeometry(player.rect());
    QDialog settings(&player);
    settings.resize(160, 100);
    QDialog fileDialog(&settings);
    fileDialog.resize(120, 80);
    check(settings.isWindow() && settings.parentWidget() == &player
              && !player.isAncestorOf(&settings)
              && fileDialog.isWindow() && fileDialog.parentWidget() == &settings,
          "The dialog fixtures must exercise ownership across top-level window boundaries rather than ordinary widget ancestry.");
    QWidget outsidePlayer;
    outsidePlayer.resize(160, 100);
    bool backendPaused = true;
    int activityChanges = 0;
    surface.onBackgroundActivityChanged = [&](bool active) {
        backendPaused = !active;
        ++activityChanges;
    };
    const auto activate = [](QWidget &widget) {
        widget.show();
        widget.raise();
        widget.activateWindow();
        check(until([&] { return QApplication::activeWindow() == &widget; }),
              "The activity fixture must focus its own requested window.");
    };

    activate(player);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; })
              && surface.animationRunning(),
          "An active player must synchronize its backend and animate its visible background.");
    activate(settings);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; })
              && surface.backgroundIsActive(),
          "An owned settings dialog must keep the player's background active.");

    // The main window is already deactivated. Subsequent dialog-to-window
    // focus changes do not send it another activation/deactivation event.
    activate(outsidePlayer);
    check(until([&] { return !surface.m_backgroundActive && backendPaused; })
              && !surface.backgroundIsActive() && !surface.animationRunning(),
          "Moving from an owned dialog to another window must pause the player's backend and rendering.");
    activate(settings);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; })
              && surface.backgroundIsActive() && surface.animationRunning(),
          "Returning to an owned dialog must resume activity while the main window remains deactivated.");

    activate(fileDialog);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; })
              && surface.backgroundIsActive(),
          "A nested owned file dialog must preserve background activity.");
    fileDialog.close();
    activate(settings);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; }),
          "Closing a file dialog and returning to settings must retain synchronized activity.");

    // Reproduce the transient active-window state seen when a file picker
    // returns before the deferred focus notification has been processed.
    // Both switches leave the main window deactivated, so its event filter
    // cannot synchronize the background on its own.
    surface.synchronizeBackgroundActivity();
    QApplication::setActiveWindow(&outsidePlayer);
    check(!surface.backgroundIsActive() && surface.m_backgroundActive,
          "The fixture must expose actual inactivity before the cached state is synchronized.");
    BackgroundTheme sameTheme = surface.backgroundTheme();
    sameTheme.dimming = 20;
    check(surface.setBackground(sameTheme) && !surface.m_backgroundActive
              && backendPaused && !surface.animationRunning(),
          "Applying readability changes to the same source must synchronize activity instead of retaining a stale active cache.");
    QApplication::setActiveWindow(&settings);
    check(surface.backgroundIsActive() && !surface.m_backgroundActive,
          "Returning to an owned dialog must expose the inverse cached-state mismatch before its deferred notification.");
    check(surface.synchronizeBackgroundActivity() && !backendPaused
              && surface.animationRunning(),
          "Explicit backend synchronization must restore a paused source immediately after foreground activity returns.");
    const int synchronizedChanges = activityChanges;
    check(surface.synchronizeBackgroundActivity() && activityChanges == synchronizedChanges,
          "Repeated synchronization must not emit duplicate activity changes for an unchanged state.");

    settings.close();
    activate(player);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; })
              && surface.backgroundIsActive(),
          "Closing settings must return to an active player with its backend resumed.");
    QDialog modalSettings(&player);
    modalSettings.setModal(true);
    modalSettings.resize(160, 100);
    activate(modalSettings);
    check(modalSettings.isModal()
              && until([&] { return surface.m_backgroundActive && !backendPaused; })
              && surface.backgroundIsActive(),
          "A modal owned settings window must keep the player's background active while its main window is disabled.");
    modalSettings.close();
    activate(player);
    check(until([&] { return surface.m_backgroundActive && !backendPaused; }),
          "Closing modal settings must preserve resumed background activity.");
    player.hide();
    check(!surface.synchronizeBackgroundActivity() && backendPaused
              && !surface.animationRunning(),
          "Hiding the player must pause its backend and stop rendering even after owned-dialog transitions.");
    surface.onBackgroundActivityChanged = {};
}

void reportFramePainting(AeroSurface &surface, const QString &projectPath)
{
    surface.resize(1100, 700);
    BackgroundTheme theme;
    theme.kind = BackgroundKind::WallpaperEngine;
    theme.sourcePath = projectPath;
    theme.dimming = 38;
    check(surface.setBackground(theme), "The performance fixture must accept an owned background frame.");
    const qreal dpr = surface.devicePixelRatioF();
    const QSize pixels(qCeil(surface.width() * dpr), qCeil(surface.height() * dpr));
    QImage first(pixels, QImage::Format_RGB32), second(pixels, QImage::Format_RGB32);
    first.fill(QColor(20, 100, 170));
    second.fill(QColor(170, 70, 20));
    QImage canvas(pixels, QImage::Format_RGB32);
    canvas.setDevicePixelRatio(dpr);
    QElapsedTimer clock;
    clock.start();
    constexpr int frames = 120;
    for (int frame = 0; frame < frames; ++frame) {
        surface.setBackgroundFrame(frame % 2 ? first : second);
        QPainter painter(&canvas);
        surface.paintWater(painter, surface.rect());
        surface.paintBlurredBackground(painter, QRectF(30, 50, 340, 440));
        surface.paintBlurredBackground(painter, QRectF(460, 470, 580, 170));
        for (int row = 0; row < 8; ++row) {
            const QRectF bounds(380, 90 + row * 48, 650, 44);
            for (int cell = 0; cell < 4; ++cell) {
                painter.save();
                painter.setClipRect(QRectF(380 + cell * 162, bounds.y(), 162, bounds.height()));
                surface.paintWater(painter, bounds);
                painter.restore();
            }
        }
    }
    const qint64 foregroundElapsed = clock.elapsed();
    check(until([&] { return surface.m_backgroundBlurredFrameRevision == surface.m_backgroundFrameRevision
        && !surface.m_backgroundBlurBusy; }), "The painting burst's final blur must finish rather than retain the first frame.");
    std::fprintf(stderr, "Background foreground painting burst: %d frames in %lldms; blur catch-up=%lldms; physical=%dx%d, DPR=%.2f, original-frame sharing=%d\n",
                 frames, static_cast<long long>(foregroundElapsed), static_cast<long long>(clock.elapsed() - foregroundElapsed),
                 pixels.width(), pixels.height(), double(dpr),
                 surface.m_backgroundTexture.constBits() == first.constBits());
    check(surface.m_backgroundTexture.size() == pixels,
          "The clear background cache must cover physical screen pixels instead of enlarging a logical-resolution screenshot.");
    QVector<qint64> foregroundCosts;
    int caughtUp = 0;
    constexpr int pacedFrames = 60;
    for (int frame = 0; frame < pacedFrames; ++frame) {
        QElapsedTimer paintClock;
        paintClock.start();
        surface.setBackgroundFrame(frame % 2 ? first : second);
        {
            QPainter painter(&canvas);
            surface.paintWater(painter, surface.rect());
            surface.paintBlurredBackground(painter, QRectF(30, 50, 340, 440));
            surface.paintBlurredBackground(painter, QRectF(460, 470, 580, 170));
        }
        foregroundCosts.append(paintClock.nsecsElapsed());
        events(16);
        if (surface.m_backgroundBlurredFrameRevision == surface.m_backgroundFrameRevision)
            ++caughtUp;
    }
    std::sort(foregroundCosts.begin(), foregroundCosts.end());
    std::fprintf(stderr, "Background paced inputs: %d at 16ms event intervals; foreground median=%.3fms, p95=%.3fms; current blur after interval=%d/%d (paint costs exclude worker CPU)\n",
                 pacedFrames, foregroundCosts[pacedFrames / 2] / 1000000.0,
                 foregroundCosts[pacedFrames * 95 / 100] / 1000000.0, caughtUp, pacedFrames);
    check(until([&] { return surface.m_backgroundBlurredFrameRevision == surface.m_backgroundFrameRevision
        && !surface.m_backgroundBlurBusy; }), "The paced background's final blur must also finish.");
}
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "Background fixtures require a temporary directory.");
    if (application.arguments().contains(QStringLiteral("--fractional-frame-sharing"))) {
        checkFractionalFrameSharing(directory);
        return 0;
    }
    checkFractionalFrameSharingProcess();
    checkBackgroundActivitySynchronization();
    QImage image(320, 200, QImage::Format_RGB32);
    image.fill(QColor(220, 30, 20));
    { QPainter painter(&image); painter.fillRect(QRect(160, 0, 160, 200), QColor(20, 40, 220)); }
    const QString imagePath = directory.filePath(QStringLiteral("split.png"));
    check(image.save(imagePath), "The static fixture must be writable.");
    AeroSurface surface;
    surface.resize(image.size());
    surface.show();
    surface.activateWindow();
    events(40);
    BackgroundTheme theme;
    theme.kind = BackgroundKind::Image;
    theme.sourcePath = imagePath;
    theme.dimming = 0;
    QString error;
    check(surface.setBackground(theme, &error), "A valid user image must be displayable.");
    QImage rendered = sample(surface, surface.rect());
    check(rendered.pixelColor(30, 100).red() > 200 && rendered.pixelColor(290, 100).blue() > 200,
          "The image must be displayed rather than the original analytic water.");
    const QImage region = sample(surface, QRect(220, 50, 50, 50));
    check(region.pixelColor(10, 10) == rendered.pixelColor(230, 60),
          "Glass sampling must use the same world coordinates as the full imported background.");
    QImage blurred(surface.size(), QImage::Format_RGB32);
    {
        QPainter painter(&blurred);
        surface.paintBlurredBackground(painter, surface.rect());
    }
    const QColor blended = blurred.pixelColor(160, 100);
    check(blended.red() > 70 && blended.blue() > 70,
          "Glass must blur actual imported background edges instead of reusing a clear screenshot.");
    QImage replacement(image.size(), QImage::Format_RGB32);
    replacement.fill(QColor(15, 220, 30));
    const QString replacementPath = directory.filePath(QStringLiteral("replacement.png"));
    check(replacement.save(replacementPath), "The replacement image fixture must be writable.");
    BackgroundTheme replacementTheme = theme;
    replacementTheme.sourcePath = replacementPath;
    check(surface.setBackground(replacementTheme, &error), "A same-size image replacement must be accepted.");
    {
        QPainter painter(&blurred);
        surface.paintBlurredBackground(painter, surface.rect());
    }
    check(blurred.pixelColor(160, 100).green() > 200 && blurred.pixelColor(160, 100).red() < 40,
          "Changing same-size static images must invalidate the glass blur as well as the clear texture.");
    check(surface.setBackground(theme, &error), "The original static fixture must be restorable.");
    theme.dimming = 50;
    check(surface.setBackground(theme, &error), "Readability controls must preserve the current background.");
    rendered = sample(surface, surface.rect());
    check(rendered.pixelColor(30, 100).red() > 90 && rendered.pixelColor(30, 100).red() < 130,
          "The darkening control must change actual background pixels.");
    BackgroundTheme missing = theme;
    missing.sourcePath = directory.filePath(QStringLiteral("absent.png"));
    check(!surface.setBackground(missing, &error) && !error.isEmpty()
              && surface.backgroundTheme().sourcePath == imagePath,
          "An unreadable replacement must report its error without erasing the last valid background.");

    const QString projectPath = directory.filePath(QStringLiteral("project.json"));
    QFile project(projectPath);
    check(project.open(QIODevice::WriteOnly), "External background fixtures must be writable.");
    project.write("{}");
    project.close();
    check(surface.m_motionTimer->isSingleShot() && surface.m_videoFrameTimer->isSingleShot(),
          "Background producers must use elapsed deadline scheduling rather than a fixed repeating timer.");
    surface.updateDisplayRate();
    check(qFuzzyCompare(surface.m_motionSchedule.refreshRate(), Aero::displayRefreshRate(&surface))
        && qFuzzyCompare(surface.m_videoSchedule.refreshRate(), Aero::displayRefreshRate(&surface)),
          "A monitor change must immediately update both water and decoded-video frame budgets.");
    // A timer notification that arrives before its nanosecond deadline must
    // not invent another background frame (important on 240/360 Hz screens).
    surface.m_motionTimer->stop();
    check(surface.backgroundIsActive(), "The early-tick fixture must exercise an actually active surface.");
    surface.m_motionSchedule.reset(surface.m_presentationClock.nsecsElapsed() + 1000000000);
    const qreal phaseBeforeEarlyTick = surface.m_phase;
    surface.advanceWater();
    check(surface.m_phase == phaseBeforeEarlyTick,
          "Early background timer notifications must preserve the current frame instead of over-rendering.");
    surface.m_motionTimer->stop();
    checkDynamicBlur(projectPath, imagePath);
    checkBlurConsumerCadence(projectPath);
    checkBlurPresentationLifecycle(projectPath, imagePath);
    theme.kind = BackgroundKind::WallpaperEngine;
    theme.sourcePath = projectPath;
    theme.dimming = 0;
    check(surface.setBackground(theme, &error), "The engine mode must accept frames supplied by its owned capture bridge.");
    QImage frame(320, 200, QImage::Format_RGB32);
    frame.fill(Qt::green);
    surface.setBackgroundFrame(frame);
    const QImage firstEngineFrame = sample(surface, surface.rect());
    frame.fill(Qt::blue);
    surface.setBackgroundFrame(frame);
    check(firstEngineFrame != sample(surface, surface.rect())
              && sample(surface, surface.rect()).pixelColor(100, 100).blue() > 200,
          "Distinct captured engine frames must change the rendered background and glass samples.");
    QImage highResolution(3000, 1700, QImage::Format_RGB32);
    highResolution.fill(Qt::blue);
    surface.setBackgroundFrame(highResolution);
    check(surface.m_backgroundFrame.size() == highResolution.size(),
          "High-resolution captures must retain their original pixels rather than silently shrink to 2560x1440.");
    surface.resize(240, 160);
    check(sample(surface, surface.rect()).pixelColor(100, 100).blue() > 200,
          "Resizing must rebuild the background frame cache without dropping external video content.");
    {
        CountingContent content(&surface);
        content.setGeometry(surface.rect());
        content.show();
        QWidget panel(&surface);
        panel.setProperty("liquidOverlay", true);
        panel.setGeometry(20, 20, 160, 100);
        panel.show();
        events(30);
        sampleGlass(panel);
        const int captured = content.paints;
        for (int index = 0; index < 4; ++index) {
            frame.fill(index % 2 ? Qt::green : Qt::blue);
            surface.setBackgroundFrame(frame);
            sampleGlass(panel);
        }
        check(content.paints == captured,
              "Dynamic background frames must reuse static content blur snapshots instead of recapturing the table each frame.");
    }

    const QString moviePath = directory.filePath(QStringLiteral("animation.avi"));
    writeMovie(moviePath);
    theme.kind = BackgroundKind::Video;
    theme.sourcePath = moviePath;
    check(surface.setBackground(theme, &error), "The local movie must be accepted for real playback.");
    surface.activateWindow();
    check(until([&] { return !surface.m_backgroundFrame.isNull()
        && surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PlayingState; }),
          "The actual media backend must decode the background movie to frames.");
    const QColor first = sample(surface, surface.rect()).pixelColor(100, 100);
    check(until([&] { return sample(surface, surface.rect()).pixelColor(100, 100) != first; }),
          "A background video must visibly change over time; a thumbnail does not satisfy dynamic playback.");
    check(surface.m_backgroundAudio->isMuted() && surface.m_backgroundAudio->volume() == 0,
          "Background media must remain silent independently of music playback.");
    events(1300);
    check(surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PlayingState,
          "Background media must keep playing beyond the fixture duration by looping.");
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&surface, &deactivate);
    check(until([&] { return surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PausedState; }),
          "Deactivation must pause video decoding even when Qt's active-window pointer still names the old window.");
    check(!surface.m_videoFrameTimer->isActive() && !surface.m_videoFrames->take().isValid(),
          "Inactive video must stop frame presentation and release pending decoder frames.");
    events(50);
    check(surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PausedState,
          "A delayed activation recheck must not restart an explicitly inactive window.");
    surface.activateWindow();
    QEvent activate(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&surface, &activate);
    check(until([&] { return surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PlayingState; }),
          "Returning to the active window must resume actual background video playback.");
    surface.showMinimized();
    check(until([&] { return surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PausedState; }),
          "Minimized windows must pause background video decoding.");
    surface.showNormal();
    surface.activateWindow();
    check(until([&] { return surface.m_backgroundPlayer->playbackState() == QMediaPlayer::PlayingState; }),
          "Restoring and activating a minimized window must resume its video.");
    surface.hide();
    events(30);
    check(surface.m_backgroundPlayer->playbackState() != QMediaPlayer::PlayingState,
          "Hidden windows must pause background decoding rather than continue consuming resources.");
    check(surface.setBackground(BackgroundTheme(), &error)
              && surface.m_backgroundPlayer->source().isEmpty(),
          "Restoring the built-in theme must release the video source.");
    check(!surface.m_videoFrameTimer->isActive() && !surface.m_videoFrames->take().isValid(),
          "Switching away from video must flush its latest-frame mailbox and stop its GUI timer.");
    reportFramePainting(surface, projectPath);
    return 0;
}
