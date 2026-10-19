#include "aero_surface.h"
#include "aero_widgets.h"
#include "liquid_backdrop.h"
#include "liquid_image_blur.h"
#include "latest_video_frame.h"
#include "video_frame_image.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QAudioOutput>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPromise>
#include <QRadialGradient>
#include <QTimer>
#include <QThreadPool>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
QImage makeBackgroundBlur(const QImage &texture, const QSize &size)
{
    QImage blurred = texture.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB32);
    for (int pass = 0; pass < 2; ++pass) {
        blurred = Liquid::detail::boxBlurPass<3>(blurred, true);
        blurred = Liquid::detail::boxBlurPass<3>(blurred, false);
    }
    return blurred;
}
} // namespace

AeroSurface::AeroSurface(QWidget *parent)
    : QWidget(parent), m_accent(Aero::defaultAccent()), m_window(window()),
      m_motionTimer(new QTimer(this)),
      m_backgroundBlurWatcher(new QFutureWatcher<QImage>(this)),
      m_backgroundBlurTimer(new QTimer(this)),
      m_backgroundBlurPresentTimer(new QTimer(this)),
      m_videoFrameTimer(new QTimer(this)),
      m_videoFrames(std::make_shared<LatestVideoFrame>())
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    m_motionTimer->setTimerType(Qt::PreciseTimer);
    m_motionTimer->setSingleShot(true);
    m_motionTimer->setInterval(Aero::animationInterval(this));
    connect(m_motionTimer, &QTimer::timeout, this, [this] { advanceWater(); });
    m_backgroundBlurTimer->setSingleShot(true);
    m_backgroundBlurTimer->setTimerType(Qt::PreciseTimer);
    connect(m_backgroundBlurTimer, &QTimer::timeout, this, [this] { requestBackgroundBlur(); });
    m_backgroundBlurPresentTimer->setSingleShot(true);
    m_backgroundBlurPresentTimer->setTimerType(Qt::PreciseTimer);
    connect(m_backgroundBlurPresentTimer, &QTimer::timeout, this, [this] { repaintGlassConsumers(); });
    connect(m_backgroundBlurWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
        m_backgroundBlurBusy = false;
        if (m_backgroundBlurJobGeneration == m_backgroundBlurGeneration
            && m_backgroundBlurJobSize == QSize(std::max(1, width() / 6), std::max(1, height() / 6))
            && !m_backgroundFrame.isNull() && !m_backgroundFailed) {
            m_backgroundBlurred = m_backgroundBlurWatcher->result();
            m_backgroundBlurredFrameRevision = m_backgroundBlurJobFrameRevision;
            // The next source frame normally repaints every glass panel within
            // 16 ms. Let it present this completed blur instead of composing a
            // second full glass frame between each source frame. An idle/final
            // source still presents its result through the bounded fallback.
            if (backgroundIsActive())
                m_backgroundBlurPresentTimer->start(32);
        }
        if (m_backgroundBlurPending)
            requestBackgroundBlur();
    });
    m_videoFrameTimer->setTimerType(Qt::PreciseTimer);
    m_videoFrameTimer->setSingleShot(true);
    m_videoFrameTimer->setInterval(Aero::animationInterval(this));
    connect(m_videoFrameTimer, &QTimer::timeout, this, [this] { publishVideoFrame(); });
    m_presentationClock.start();
    m_displayObserver = Aero::observeDisplayRefresh(this, this, [this] { updateDisplayRate(); });
    if (m_window)
        m_window->installEventFilter(this);
    // A settings/file dialog can regain or lose focus while the application
    // window remains deactivated, so its events alone cannot track activity.
    // Qt finishes updating activeWindow after some focus notifications.
    connect(qApp, &QGuiApplication::focusWindowChanged, this,
            [this](QWindow *) { scheduleBackgroundActivitySync(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState) { scheduleBackgroundActivitySync(); });
}

AeroSurface::~AeroSurface()
{
    delete m_displayObserver;
    m_motionTimer->stop();
    m_backgroundBlurTimer->stop();
    m_backgroundBlurPresentTimer->stop();
    m_videoFrameTimer->stop();
    m_videoFrames->reset(false);
    disconnect(m_backgroundBlurWatcher, nullptr, this, nullptr);
    if (m_backgroundSink)
        disconnect(m_backgroundSink, nullptr, this, nullptr);
    if (m_backgroundMovie)
        disconnect(m_backgroundMovie, nullptr, this, nullptr);
    if (m_backgroundPlayer) {
        disconnect(m_backgroundPlayer, nullptr, this, nullptr);
        m_backgroundPlayer->stop();
        m_backgroundPlayer->setVideoSink(nullptr);
    }
}

bool AeroSurface::setBackground(const BackgroundTheme &theme, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (theme.kind != BackgroundKind::Liquid && theme.kind != BackgroundKind::FireMeteor
        && !QFileInfo(theme.sourcePath).isFile())
        return fail(QStringLiteral("背景文件已移动或不存在，请重新选择。"));
    const bool sameSource = m_background.kind == theme.kind && m_background.sourcePath == theme.sourcePath;
    if (sameSource && !m_backgroundFailed) {
        m_background = theme;
        m_background.dimming = std::clamp(theme.dimming, 0, 85);
        ++m_revision;
        synchronizeBackgroundActivity();
        repaintBackground();
        if (error)
            error->clear();
        return true;
    }
    QImage image;
    bool animatedImage = false;
    if (theme.kind == BackgroundKind::Image) {
        QImageReader reader(theme.sourcePath);
        reader.setAutoTransform(true);
        animatedImage = reader.supportsAnimation();
        image = reader.read();
        if (image.isNull())
            return fail(QStringLiteral("无法读取这张背景图片。"));
    }
    m_videoFrames->reset(false);
    m_videoFrameTimer->stop();
    m_background = theme;
    m_background.dimming = std::clamp(theme.dimming, 0, 85);
    if (m_backgroundPlayer) {
        m_backgroundPlayer->stop();
        m_backgroundPlayer->setSource(QUrl());
    }
    if (m_backgroundMovie) {
        delete m_backgroundMovie;
        m_backgroundMovie = nullptr;
    }
    m_backgroundFrame = {};
    m_backgroundTexture = {};
    invalidateBackgroundBlur();
    ++m_backgroundFrameRevision;
    m_backgroundFrameClock.invalidate();
    m_backgroundFailed = false;
    ++m_revision;
    if (theme.kind == BackgroundKind::Image) {
        m_backgroundFrame = image;
        if (animatedImage) {
            m_backgroundMovie = new QMovie(theme.sourcePath, QByteArray(), this);
            m_backgroundMovie->setCacheMode(QMovie::CacheNone);
            connect(m_backgroundMovie, &QMovie::frameChanged, this, [this] {
                setBackgroundFrame(m_backgroundMovie->currentImage());
            });
            connect(m_backgroundMovie, &QMovie::error, this, [this](QImageReader::ImageReaderError) {
                if (onBackgroundError)
                    onBackgroundError(QStringLiteral("动态图解码失败，请尝试其它 GIF 或视频。"));
            });
            m_backgroundMovie->jumpToFrame(0);
        }
    } else if (theme.kind == BackgroundKind::Video) {
        if (!m_backgroundPlayer) {
            m_backgroundPlayer = new QMediaPlayer(this);
            m_backgroundAudio = new QAudioOutput(this);
            m_backgroundAudio->setMuted(true);
            m_backgroundAudio->setVolume(0);
            m_backgroundPlayer->setAudioOutput(m_backgroundAudio);
            m_backgroundSink = new QVideoSink(this);
            m_backgroundPlayer->setVideoSink(m_backgroundSink);
            m_backgroundPlayer->setLoops(QMediaPlayer::Infinite);
            // The backend thread stores one latest frame without posting a
            // separate GUI event for every decoded frame during a UI stall.
            connect(m_backgroundSink, &QVideoSink::videoFrameChanged, this,
                    [frames = m_videoFrames](const QVideoFrame &frame) { frames->offer(frame); },
                    Qt::DirectConnection);
            connect(m_backgroundPlayer, &QMediaPlayer::errorOccurred, this,
                    [this](QMediaPlayer::Error code, const QString &message) {
                if (code == QMediaPlayer::NoError || m_background.kind != BackgroundKind::Video)
                    return;
                m_backgroundFailed = true;
                m_videoFrames->reset(false);
                m_videoFrameTimer->stop();
                m_backgroundPlayer->stop();
                if (onBackgroundError)
                    onBackgroundError(QStringLiteral("背景视频播放失败：%1").arg(message));
                updateAnimationState();
                repaintBackground();
            });
        }
        m_backgroundPlayer->setSource(QUrl::fromLocalFile(theme.sourcePath));
    }
    updateAnimationState();
    repaintBackground();
    if (error)
        error->clear();
    return true;
}

BackgroundTheme AeroSurface::backgroundTheme() const { return m_background; }

void AeroSurface::setBackgroundFrame(const QImage &frame)
{
    if (frame.isNull() || m_background.kind == BackgroundKind::Liquid
        || m_background.kind == BackgroundKind::FireMeteor)
        return;
    m_backgroundBlurPresentTimer->stop();
    // QImage's implicit sharing retains the original pixels without copying or
    // silently reducing high-resolution imports/captured frames.
    m_backgroundFrame = frame;
    m_backgroundFailed = false;
    m_backgroundFrameClock.start();
    ++m_backgroundFrameRevision;
    repaintBackground();
}

void AeroSurface::clearBackgroundFrame()
{
    m_backgroundFrame = {};
    m_backgroundTexture = {};
    invalidateBackgroundBlur();
    m_backgroundFrameClock.invalidate();
    ++m_backgroundFrameRevision;
    repaintBackground();
}

void AeroSurface::registerBackgroundConsumer(QWidget *widget)
{
    if (!widget || widget == this)
        return;
    for (const auto &consumer : m_backgroundConsumers)
        if (consumer == widget)
            return;
    m_backgroundConsumers.append(widget);
}

void AeroSurface::registerBlurredBackgroundConsumer(QWidget *widget)
{
    if (!widget || widget == this)
        return;
    registerBackgroundConsumer(widget);
    for (const auto &consumer : m_backgroundBlurConsumers)
        if (consumer == widget)
            return;
    m_backgroundBlurConsumers.append(widget);
}

void AeroSurface::ensureBackgroundTexture() const
{
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (m_backgroundTexture.size() != pixels
        || !qFuzzyCompare(m_backgroundTextureDpr, dpr)
        || m_backgroundTextureFrameRevision != m_backgroundFrameRevision) {
        if (m_background.kind == BackgroundKind::WallpaperEngine && m_backgroundFrame.size() == pixels) {
            // Match physical pixels before comparing logical aspect ratios.
            // Fractional DPI rounds the renderer's height/width to whole pixels;
            // cropping that subpixel difference resamples the entire frame.
            m_backgroundTexture = m_backgroundFrame;
        } else {
            QRectF source(m_backgroundFrame.rect());
            const qreal sourceRatio = source.width() / source.height();
            const qreal targetRatio = qreal(width()) / std::max(1, height());
            if (sourceRatio > targetRatio) {
                const qreal width = source.height() * targetRatio;
                source.setLeft((source.width() - width) / 2);
                source.setWidth(width);
            } else {
                const qreal height = source.width() / targetRatio;
                source.setTop((source.height() - height) / 2);
                source.setHeight(height);
            }
            if (m_backgroundFrame.size() == pixels && source == QRectF(m_backgroundFrame.rect())) {
                m_backgroundTexture = m_backgroundFrame;
            } else {
                m_backgroundTexture = QImage(pixels, QImage::Format_RGB32);
                m_backgroundTexture.fill(QColor(7, 10, 15));
                QPainter background(&m_backgroundTexture);
                background.scale(dpr, dpr);
                background.setRenderHint(QPainter::SmoothPixmapTransform);
                background.drawImage(rect(), m_backgroundFrame, source);
            }
        }
        m_backgroundTextureDpr = dpr;
        m_backgroundTextureFrameRevision = m_backgroundFrameRevision;
    }
}

bool AeroSurface::paintCustomBackground(QPainter &painter, const QRectF &bounds,
                                       const QPointF &worldOffset) const
{
    if (m_background.kind == BackgroundKind::Liquid || m_background.kind == BackgroundKind::FireMeteor
        || m_backgroundFrame.isNull() || m_backgroundFailed)
        return false;
    ensureBackgroundTexture();
    const qreal dpr = devicePixelRatioF();
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    const QRectF world = bounds.translated(worldOffset);
    const QRectF source(world.topLeft() * dpr, world.size() * dpr);
    // An opaque texture already replaces every pixel in this sample. Clearing
    // it first adds a complete extra fill to every background/card/frame.
    if (m_backgroundTexture.hasAlphaChannel() || !QRectF(m_backgroundTexture.rect()).contains(source))
        painter.fillRect(bounds, QColor(7, 10, 15));
    painter.drawImage(bounds, m_backgroundTexture, source);
    if (m_background.dimming > 0)
        painter.fillRect(bounds, QColor(0, 0, 0, m_background.dimming * 255 / 100));
    painter.restore();
    return true;
}

void AeroSurface::paintBlurredBackground(QPainter &painter, const QRectF &bounds,
                                        const QPointF &worldOffset) const
{
    if (bounds.isEmpty() || size().isEmpty())
        return;
    if (m_background.kind == BackgroundKind::Liquid || m_background.kind == BackgroundKind::FireMeteor
        || m_backgroundFrame.isNull() || m_backgroundFailed) {
        paintWater(painter, bounds, worldOffset);
        return;
    }
    // One texture and one blur are shared by every glass overlay. The blurred
    // RGB image covers this entire sample, so drawing the clear image beneath
    // it only wastes fill bandwidth (especially at a high display scale).
    ensureBackgroundTexture();
    const QSize blurSize(std::max(1, width() / 6), std::max(1, height() / 6));
    const bool dynamic = m_background.kind == BackgroundKind::Video
        || m_background.kind == BackgroundKind::WallpaperEngine || m_backgroundMovie;
    if (m_backgroundBlurred.size() != blurSize || (!dynamic
        && m_backgroundBlurredFrameRevision != m_backgroundFrameRevision)) {
        // The first frame/theme/size is immediate, so the user never sees glass
        // from a previous wallpaper while waiting for a background task.
        m_backgroundBlurred = makeBackgroundBlur(m_backgroundTexture, blurSize);
        m_backgroundBlurredFrameRevision = m_backgroundFrameRevision;
    } else if (m_backgroundBlurredFrameRevision != m_backgroundFrameRevision) {
        const_cast<AeroSurface *>(this)->requestBackgroundBlur();
    }
    const qreal scaleX = qreal(blurSize.width()) / width();
    const qreal scaleY = qreal(blurSize.height()) / height();
    const QRectF world = bounds.translated(worldOffset);
    const QRectF source(world.x() * scaleX, world.y() * scaleY,
                        world.width() * scaleX, world.height() * scaleY);
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(bounds, QColor(7, 10, 15));
    painter.drawImage(bounds, m_backgroundBlurred, source);
    if (m_background.dimming > 0)
        painter.fillRect(bounds, QColor(0, 0, 0, m_background.dimming * 255 / 100));
    painter.restore();
}

void AeroSurface::invalidateBackgroundBlur()
{
    ++m_backgroundBlurGeneration;
    m_backgroundBlurred = {};
    m_backgroundBlurredFrameRevision = 0;
    m_backgroundBlurPending = false;
    m_backgroundBlurTimer->stop();
    m_backgroundBlurPresentTimer->stop();
    m_backgroundBlurClock.invalidate();
}

void AeroSurface::requestBackgroundBlur()
{
    if (m_backgroundFrame.isNull() || m_backgroundFailed
        || m_background.kind == BackgroundKind::Liquid
        || m_backgroundBlurredFrameRevision == m_backgroundFrameRevision) {
        m_backgroundBlurPending = false;
        return;
    }
    m_backgroundBlurPending = true;
    if (m_backgroundBlurBusy)
        return;
    // Expensive glass blur has its own maximum 60 Hz budget; clear wallpaper
    // and scrolling retain the actual display rate. A single in-flight job
    // and one pending latest frame keep
    // decode bursts from turning into a backlog or unbounded image memory.
    const qint64 minimumInterval = qRound64(std::max(
        1000000000.0 / Aero::displayRefreshRate(this), 1000000000.0 / 60.0));
    const qint64 elapsed = m_backgroundBlurClock.isValid() ? m_backgroundBlurClock.nsecsElapsed() : minimumInterval;
    if (elapsed < minimumInterval) {
        m_backgroundBlurTimer->start(int((minimumInterval - elapsed + 999999) / 1000000));
        return;
    }
    ensureBackgroundTexture();
    m_backgroundBlurBusy = true;
    m_backgroundBlurPending = false;
    m_backgroundBlurClock.start();
    m_backgroundBlurJobGeneration = m_backgroundBlurGeneration;
    m_backgroundBlurJobFrameRevision = m_backgroundFrameRevision;
    m_backgroundBlurJobSize = QSize(std::max(1, width() / 6), std::max(1, height() / 6));
    const QImage texture = m_backgroundTexture;
    const QSize blurSize = m_backgroundBlurJobSize;
    auto promise = std::make_shared<QPromise<QImage>>();
    promise->start();
    m_backgroundBlurWatcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, texture, blurSize] {
        // No widget or GUI object is referenced from this worker. The promise
        // owns its result even if the surface/watcher is destroyed meanwhile.
        promise->addResult(makeBackgroundBlur(texture, blurSize));
        promise->finish();
    });
}

void AeroSurface::repaintGlassConsumers()
{
    for (int index = m_backgroundBlurConsumers.size() - 1; index >= 0; --index) {
        QWidget *consumer = m_backgroundBlurConsumers[index];
        if (!consumer) {
            m_backgroundBlurConsumers.removeAt(index);
            continue;
        }
        if (consumer->isVisible() && isAncestorOf(consumer))
            consumer->update(consumer->visibleRegion());
    }
}

void AeroSurface::setAccentColor(const QColor &color)
{
    if (!color.isValid() || color == m_accent)
        return;
    m_accent = color;
    ++m_revision;
    update();
}

quint64 AeroSurface::backdropRevision() const { return m_revision; }
bool AeroSurface::animationRunning() const { return m_motionTimer->isActive(); }

void AeroSurface::paintWater(QPainter &painter, const QRectF &bounds,
                             const QPointF &worldOffset) const
{
    if (bounds.isEmpty() || size().isEmpty())
        return;
    if (paintCustomBackground(painter, bounds, worldOffset))
        return;
    // All glass consumers share one frame; meteor streaks use native pixels and
    // screen cadence, while slow water fields retain their bounded update cost.
    const bool fire = m_background.kind == BackgroundKind::FireMeteor;
    const qreal scale = fire ? devicePixelRatioF() : .75;
    const QSize frameSize(std::max(1, int(std::ceil(width() * scale))),
                          std::max(1, int(std::ceil(height() * scale))));
    const qreal cadence = fire ? Aero::displayRefreshRate(this) : 30.0;
    const qreal framePhase = std::floor(m_phase * cadence) / cadence;
    if (m_waterFrame.size() != frameSize || m_waterFramePhase != framePhase
        || m_waterFrameRevision != m_revision) {
        if (m_waterFrame.size() != frameSize)
            m_waterFrame = QImage(frameSize, QImage::Format_ARGB32_Premultiplied);
        QPainter framePainter(&m_waterFrame);
        framePainter.scale(scale, scale);
        if(fire) paintFireNight(framePainter);
        else paintWaterFrame(framePainter, rect(), {});
        m_waterFramePhase = framePhase;
        m_waterFrameRevision = m_revision;
        framePainter.end();
        // Upsample once per water frame, not once for every table cell/panel.
        // Static row transforms can then use inexpensive unscaled image copies.
        m_waterTexture = fire ? m_waterFrame : m_waterFrame.scaled(size(), Qt::IgnoreAspectRatio,
                                                                  Qt::SmoothTransformation);
    }
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.fillRect(bounds, QColor(7, 10, 15));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF source = bounds.translated(worldOffset);
    painter.drawImage(bounds, m_waterTexture, fire
        ? QRectF(source.topLeft()*scale,source.size()*scale) : source);
    painter.restore();
}

void AeroSurface::paintWaterFrame(QPainter &painter, const QRectF &bounds,
                                  const QPointF &worldOffset) const
{
    if (bounds.isEmpty())
        return;
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.translate(-worldOffset);
    const QRectF worldBounds = bounds.translated(worldOffset);
    const qreal w = std::max(1, width()), h = std::max(1, height());
    QLinearGradient depth(0,0,w,h);
    depth.setColorAt(0,QColor(12,63,78));
    depth.setColorAt(.48,QColor(8,41,59));
    depth.setColorAt(1,QColor(8,26,47));
    painter.fillRect(worldBounds, depth);

    // Broad, dim fields and slow folds give depth without drawing literal bubbles.
    QRadialGradient cyan(QPointF(w * (.19 + .07 * std::sin(m_phase * .13)),
                                h * (.34 + .10 * std::cos(m_phase * .11))), w * .65);
    QColor glow = m_accent;
    glow.setAlpha(88);
    cyan.setColorAt(0, glow);
    glow.setAlpha(30);
    cyan.setColorAt(.35, glow);
    glow.setAlpha(0);
    cyan.setColorAt(1, glow);
    painter.fillRect(worldBounds, cyan);

    QRadialGradient blue(QPointF(w * (.81 + .06 * std::cos(m_phase * .09)),
                                h * (.72 + .09 * std::sin(m_phase * .12))), w * .60);
    blue.setColorAt(0, QColor(44, 139, 217, 65));
    blue.setColorAt(.40, QColor(40, 124, 172, 20));
    blue.setColorAt(1, QColor(40, 94, 147, 0));
    painter.fillRect(worldBounds, blue);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int fold = 0; fold < 6; ++fold) {
        const qreal phase = m_phase * (.14 + fold * .018) + fold * 1.7;
        const qreal y = h * (.12 + fold * .16) + std::sin(phase) * h * .055;
        QPainterPath wave;
        wave.moveTo(-w * .18, y + h * .10);
        wave.cubicTo(w * .17, y - h * (.20 + std::sin(phase) * .025),
                     w * .58, y + h * .20, w * 1.15, y - h * .13);
        QColor haze = m_accent;
        haze.setAlpha(5);
        painter.setPen(QPen(haze, 3 + fold * .4));
        painter.drawPath(wave);
        QLinearGradient caustic(0,y,w,y);
        caustic.setColorAt(0,QColor(136,248,247,0));
        caustic.setColorAt(.28,QColor(157,255,248,fold%2 ? 55 : 90));
        caustic.setColorAt(.65,QColor(113,229,255,28));
        caustic.setColorAt(1,QColor(100,218,255,0));
        painter.setPen(QPen(QBrush(caustic), 1.2));
        painter.drawPath(wave);
    }
    painter.restore();
}

void AeroSurface::paintFireNight(QPainter &painter) const
{
    const qreal w=width(), h=height(), dpr=devicePixelRatioF();
    const QSize pixels(qCeil(w*dpr),qCeil(h*dpr));
    if(m_nightSky.size()!=pixels) {
        m_nightSky=QImage(pixels,QImage::Format_RGB32);
        QPainter sky(&m_nightSky);
        sky.scale(dpr,dpr);
        sky.setRenderHint(QPainter::Antialiasing);
        QLinearGradient night(0,0,w*.3,h);
        night.setColorAt(0,QColor(4,8,26));
        night.setColorAt(.6,QColor(17,23,49));
        night.setColorAt(1,QColor(25,56,65));
        sky.fillRect(rect(),night);
        QRadialGradient summer(QPointF(w*.8,h*.9),w*.7);
        summer.setColorAt(0,QColor(147,91,74,27));
        summer.setColorAt(1,QColor(56,54,110,0));
        sky.fillRect(rect(),summer);
        sky.setPen(Qt::NoPen);
        quint32 seed=7319;
        auto random=[&seed](){seed=seed*1664525u+1013904223u;return (seed>>8)/16777216.0;};
        for(int star=0;star<340;++star) {
            const QPointF point(random()*w,random()*h*.89);
            const qreal radius=.35+random()*.85;
            sky.setBrush(QColor(192+star%50,213+star%35,255,65+star%140));
            sky.drawEllipse(point,radius,radius);
            if(star%41==0) {
                sky.setPen(QPen(QColor(214,232,255,60),.65));
                sky.drawLine(point-QPointF(3,0),point+QPointF(3,0));
                sky.drawLine(point-QPointF(0,3),point+QPointF(0,3));
                sky.setPen(Qt::NoPen);
            }
        }
        QPainterPath horizon;
        horizon.moveTo(0,h);
        horizon.lineTo(0,h*.96);
        horizon.cubicTo(w*.18,h*.86,w*.28,h*.98,w*.42,h*.94);
        horizon.cubicTo(w*.63,h*.84,w*.78,h*.98,w,h*.90);
        horizon.lineTo(w,h);horizon.closeSubpath();
        sky.fillPath(horizon,QColor(5,17,27));
    }
    painter.drawImage(QRectF(rect()),m_nightSky);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto fract=[](qreal x){return x-std::floor(x);};
    for(int index=0;index<64;++index) {
        const qreal phase=fract(m_phase/(3.2+(index%5)*.13)+index*.61803398875);
        if(phase>.085) continue;
        const qreal t=phase/.085;
        const qreal depth=.45+(index%7)*.085;
        const QPointF start(w*(.18+fract(index*.381966)*1.05),h*(-.10+fract(index*.7127)*.77));
        const QPointF travel(-w*(.25+depth*.2),h*(.18+depth*.16));
        const QPointF head=start+travel*.8;
        const QPointF direction=travel/std::hypot(travel.x(),travel.y());
        const qreal length=(160+depth*240)*(1-t*.55);
        const QPointF tail=head-direction*length;
        painter.save();
        painter.setOpacity(std::pow(1-t,1.2)*depth);
        QLinearGradient flame(tail,head);
        flame.setColorAt(0,QColor(255,62,12,0));
        flame.setColorAt(.45,QColor(255,96,24,105));
        flame.setColorAt(1,QColor(255,211,120,230));
        painter.setPen(QPen(QBrush(flame),2*depth,Qt::SolidLine,Qt::RoundCap));
        painter.drawLine(tail,head);
        flame.setColorAt(.6,QColor(255,183,74,190));
        flame.setColorAt(1,QColor(255,253,225,255));
        painter.setPen(QPen(QBrush(flame),.7*depth,Qt::SolidLine,Qt::RoundCap));
        painter.drawLine(tail,head);
        QRadialGradient glow(head,3*depth);
        glow.setColorAt(0,QColor(255,239,175,220));
        glow.setColorAt(.25,QColor(255,134,38,105));
        glow.setColorAt(1,QColor(255,60,12,0));
        painter.setPen(Qt::NoPen);painter.setBrush(glow);
        painter.drawEllipse(head,3*depth,3*depth);
        for(int ember=0;ember<5;++ember) {
            const qreal age=fract(t*2+ember*.19);
            const QPointF normal(-direction.y(),direction.x());
            const QPointF point=head-direction*length*age
                +normal*(std::sin(index+ember*7.4)*3*age);
            painter.setBrush(QColor(255,159+ember*12,52,qRound((1-age)*170)));
            painter.drawEllipse(point,depth,depth*.7);
        }
        painter.restore();
    }
}

void AeroSurface::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintWater(painter, rect());
}

void AeroSurface::updateAnimationState()
{
    QWidget *currentWindow = window();
    if (m_window != currentWindow) {
        if (m_window)
            m_window->removeEventFilter(this);
        m_window = currentWindow;
        m_windowDeactivated = false;
        if (m_window)
            m_window->installEventFilter(this);
    }
    const bool active = backgroundIsActive();
    if (m_backgroundActive != active) {
        m_backgroundActive = active;
        if (onBackgroundActivityChanged)
            onBackgroundActivityChanged(active);
    }
    if (m_backgroundPlayer && m_background.kind == BackgroundKind::Video && !m_backgroundFailed) {
        if (active) {
            if (!m_videoFrameTimer->isActive()) {
                m_videoFrames->reset(true);
                const qint64 now = m_presentationClock.nsecsElapsed();
                m_videoSchedule.setRefreshRate(Aero::displayRefreshRate(this), now);
                m_videoSchedule.reset(now);
                m_videoFrameTimer->start(m_videoSchedule.delayMs(now));
            }
            m_backgroundPlayer->play();
        } else {
            m_videoFrames->reset(false);
            m_videoFrameTimer->stop();
            m_backgroundPlayer->pause();
        }
    }
    if (m_backgroundMovie) {
        if (active && m_backgroundMovie->state() == QMovie::NotRunning)
            m_backgroundMovie->start();
        m_backgroundMovie->setPaused(!active);
    }
    if (!active || (m_background.kind != BackgroundKind::Liquid
                   && m_background.kind != BackgroundKind::FireMeteor && !m_backgroundFailed)) {
        m_motionTimer->stop();
        if (!active) {
            m_backgroundBlurTimer->stop();
            m_backgroundBlurPresentTimer->stop();
            m_backgroundBlurPending = false;
        }
        return;
    }
    if (!m_motionTimer->isActive()) {
        const qint64 now = m_presentationClock.nsecsElapsed();
        m_motionSchedule.setRefreshRate(Aero::displayRefreshRate(this), now);
        m_motionSchedule.reset(now);
        m_frameClock.start();
        m_motionTimer->start(m_motionSchedule.delayMs(now));
    }
}

void AeroSurface::updateDisplayRate()
{
    const qint64 now = m_presentationClock.nsecsElapsed();
    const qreal rate = Aero::displayRefreshRate(this);
    m_motionSchedule.setRefreshRate(rate, now);
    m_videoSchedule.setRefreshRate(rate, now);
    if (m_motionTimer->isActive())
        m_motionTimer->start(m_motionSchedule.delayMs(now));
    if (m_videoFrameTimer->isActive())
        m_videoFrameTimer->start(m_videoSchedule.delayMs(now));
    if (m_backgroundBlurTimer->isActive())
        requestBackgroundBlur();
}

void AeroSurface::publishVideoFrame()
{
    if (!backgroundIsActive() || m_background.kind != BackgroundKind::Video || m_backgroundFailed) {
        m_videoFrames->reset(false);
        return;
    }
    const qint64 now = m_presentationClock.nsecsElapsed();
    m_videoSchedule.setRefreshRate(Aero::displayRefreshRate(this), now);
    if (m_videoSchedule.advance(now)) {
        const QVideoFrame frame = m_videoFrames->take();
        if (frame.isValid())
            setBackgroundFrame(VideoFrames::toImage(frame));
    }
    m_videoFrameTimer->start(m_videoSchedule.delayMs(m_presentationClock.nsecsElapsed()));
}

bool AeroSurface::backgroundIsActive() const
{
    QWidget *currentWindow = window();
    QWidget *activeWindow = QApplication::activeWindow();
    bool ownedDialogActive = false;
    // QWidget::isAncestorOf stops at window boundaries. Owned QDialogs are
    // separate windows, so follow their ownership chain across those boundaries.
    for (QWidget *owner = activeWindow ? activeWindow->parentWidget() : nullptr;
         owner && currentWindow; owner = owner->parentWidget()) {
        if (owner == currentWindow) {
            ownedDialogActive = true;
            break;
        }
    }
    return isVisible() && currentWindow && !currentWindow->isMinimized()
        && ((!m_windowDeactivated && currentWindow->isActiveWindow()) || ownedDialogActive);
}

bool AeroSurface::synchronizeBackgroundActivity()
{
    updateAnimationState();
    return m_backgroundActive;
}

void AeroSurface::scheduleBackgroundActivitySync()
{
    if (m_backgroundActivitySyncPending)
        return;
    m_backgroundActivitySyncPending = true;
    QTimer::singleShot(0, this, [this] {
        m_backgroundActivitySyncPending = false;
        synchronizeBackgroundActivity();
    });
}

void AeroSurface::advanceWater()
{
    if (!backgroundIsActive()) {
        m_motionTimer->stop();
        return;
    }
    const qint64 now = m_presentationClock.nsecsElapsed();
    m_motionSchedule.setRefreshRate(Aero::displayRefreshRate(this), now);
    if (m_motionSchedule.advance(now)) {
        m_phase += std::clamp(m_frameClock.nsecsElapsed() / 1000000000.0, 0.0, .10);
        m_frameClock.restart();
        repaintBackground();
    }
    m_motionTimer->start(m_motionSchedule.delayMs(m_presentationClock.nsecsElapsed()));
}

void AeroSurface::repaintBackground()
{
    QRegion exposed = visibleRegion();
    for (int index = m_backgroundConsumers.size() - 1; index >= 0; --index) {
        QWidget *consumer = m_backgroundConsumers[index];
        if (!consumer) {
            m_backgroundConsumers.removeAt(index);
            continue;
        }
        if (!consumer->isVisible() || !isAncestorOf(consumer))
            continue;
        // Update the actual glass viewport/panel, never a stacked page or its
        // complete table/header hierarchy. Transparent child composition still
        // lets the current background show through the card's rounded edges.
        exposed -= QRect(consumer->mapTo(this, QPoint()), consumer->size());
        consumer->update(consumer->visibleRegion());
    }
    if (!exposed.isEmpty())
        update(exposed);
    // Only visible widgets/viewport repaint; never walk a song model or its rows.
    for (QObject *object : children()) {
        auto *child = qobject_cast<QWidget *>(object);
        if (!child || !child->isVisible())
            continue;
        if (auto *area = qobject_cast<QAbstractScrollArea *>(child))
            area->viewport()->update(area->viewport()->visibleRegion());
        else if (dynamic_cast<AeroPanel *>(child) || child->property("liquidOverlay").toBool()
                 || child->objectName() == "playlistDrawerPanel"
                 || child->objectName() == "playbackDock")
            child->update(child->visibleRegion());
    }
}

bool AeroSurface::event(QEvent *event)
{
    const bool result = QWidget::event(event);
    switch (event->type()) {
    case QEvent::Hide:
    case QEvent::WindowStateChange:
        updateAnimationState();
        break;
    case QEvent::WindowDeactivate:
        m_windowDeactivated = true;
        updateAnimationState();
        // Qt may update activeWindow only after dispatching deactivation.
        // Recheck an owned settings dialog after this event, while preserving
        // the explicit deactivated state of the application window.
        scheduleBackgroundActivitySync();
        break;
    case QEvent::Show:
    case QEvent::ParentChange:
        updateAnimationState();
        break;
    case QEvent::WindowActivate:
        m_windowDeactivated = false;
        updateAnimationState();
        break;
    case QEvent::Resize:
        ++m_revision;
        invalidateBackgroundBlur();
        break;
    case QEvent::DevicePixelRatioChange:
        invalidateBackgroundBlur();
        updateAnimationState();
        break;
    default:
        break;
    }
    return result;
}

bool AeroSurface::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window) {
        if (event->type() == QEvent::WindowDeactivate) {
            m_windowDeactivated = true;
            updateAnimationState();
            scheduleBackgroundActivitySync();
        } else if (event->type() == QEvent::WindowActivate) {
            m_windowDeactivated = false;
            updateAnimationState();
        } else if (event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)
            updateAnimationState();
        else if (event->type() == QEvent::Show)
            updateAnimationState();
    }
    return QWidget::eventFilter(watched, event);
}

AeroPanel::AeroPanel(QWidget *parent) : QWidget(parent), m_accent(Aero::defaultAccent())
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setProperty("liquidOverlay", true);
}

void AeroPanel::setAccentColor(const QColor &color)
{
    if (color.isValid() && color != m_accent) {
        m_accent = color;
        update();
    }
}

void AeroPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    Liquid::paintBackdrop(painter, this, 24);
    Aero::paintGlass(painter, QRectF(rect()).adjusted(2, 2, -2, -2), m_accent, 24);
}
