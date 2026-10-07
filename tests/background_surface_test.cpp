#include <QtWidgets>
#include <QtMultimedia>
#include <functional>
#define private public
#include "../aero_surface.h"
#undef private
#include "../liquid_backdrop.h"

#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
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
    qInfo().nospace() << "Background painting only: " << frames << " frames in " << clock.elapsed()
                     << "ms; physical=" << pixels << ", DPR=" << dpr
                     << ", original-frame sharing=" << (surface.m_backgroundTexture.constBits() == first.constBits());
    check(surface.m_backgroundTexture.size() == pixels,
          "The clear background cache must cover physical screen pixels instead of enlarging a logical-resolution screenshot.");
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
    reportFramePainting(surface, projectPath);
    return 0;
}
