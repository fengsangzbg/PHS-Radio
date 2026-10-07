#include "aero_surface.h"
#include "aero_widgets.h"
#include "liquid_backdrop.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QAudioOutput>
#include <QEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>

namespace {
QImage blurBackground(const QImage &input, bool horizontal)
{
    constexpr int radius = 3, divisor = radius * 2 + 1;
    QImage output(input.size(), QImage::Format_RGB32);
    const int lines = horizontal ? input.height() : input.width();
    const int length = horizontal ? input.width() : input.height();
    for (int line = 0; line < lines; ++line) {
        const auto pixel = [&](int position) {
            position = std::clamp(position, 0, length - 1);
            const int x = horizontal ? position : line;
            const int y = horizontal ? line : position;
            return reinterpret_cast<const QRgb *>(input.constScanLine(y))[x];
        };
        int red = 0, green = 0, blue = 0;
        const auto add = [&](QRgb value, int sign) {
            red += qRed(value) * sign;
            green += qGreen(value) * sign;
            blue += qBlue(value) * sign;
        };
        for (int position = -radius; position <= radius; ++position)
            add(pixel(position), 1);
        for (int position = 0; position < length; ++position) {
            const int x = horizontal ? position : line;
            const int y = horizontal ? line : position;
            reinterpret_cast<QRgb *>(output.scanLine(y))[x] = qRgb(red / divisor, green / divisor, blue / divisor);
            add(pixel(position - radius), -1);
            add(pixel(position + radius + 1), 1);
        }
    }
    return output;
}
} // namespace

AeroSurface::AeroSurface(QWidget *parent)
    : QWidget(parent), m_accent(Aero::defaultAccent()), m_window(window()),
      m_motionTimer(new QTimer(this))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    m_motionTimer->setTimerType(Qt::PreciseTimer);
    m_motionTimer->setInterval(Aero::animationInterval(this));
    connect(m_motionTimer, &QTimer::timeout, this, [this] { advanceWater(); });
    if (m_window)
        m_window->installEventFilter(this);
}

AeroSurface::~AeroSurface()
{
    m_motionTimer->stop();
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
    if (theme.kind != BackgroundKind::Liquid && !QFileInfo(theme.sourcePath).isFile())
        return fail(QStringLiteral("背景文件已移动或不存在，请重新选择。"));
    const bool sameSource = m_background.kind == theme.kind && m_background.sourcePath == theme.sourcePath;
    if (sameSource && !m_backgroundFailed) {
        m_background = theme;
        m_background.dimming = std::clamp(theme.dimming, 0, 85);
        ++m_revision;
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
            connect(m_backgroundSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
                if (m_background.kind != BackgroundKind::Video || !frame.isValid())
                    return;
                // Preserve the decoder's real source cadence. QWidget update
                // requests are coalesced and only the latest image is retained.
                setBackgroundFrame(frame.toImage());
            });
            connect(m_backgroundPlayer, &QMediaPlayer::errorOccurred, this,
                    [this](QMediaPlayer::Error code, const QString &message) {
                if (code == QMediaPlayer::NoError || m_background.kind != BackgroundKind::Video)
                    return;
                m_backgroundFailed = true;
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
    if (frame.isNull() || m_background.kind == BackgroundKind::Liquid)
        return;
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

bool AeroSurface::paintCustomBackground(QPainter &painter, const QRectF &bounds,
                                       const QPointF &worldOffset) const
{
    if (m_background.kind == BackgroundKind::Liquid || m_backgroundFrame.isNull() || m_backgroundFailed)
        return false;
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (m_backgroundTexture.size() != pixels
        || !qFuzzyCompare(m_backgroundTextureDpr, dpr)
        || m_backgroundTextureFrameRevision != m_backgroundFrameRevision) {
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
            // Wallpaper Engine already produces the window's physical size:
            // share that frame directly, avoiding another full-frame copy.
            m_backgroundTexture = m_backgroundFrame;
        } else {
            m_backgroundTexture = QImage(pixels, QImage::Format_RGB32);
            m_backgroundTexture.fill(QColor(7, 10, 15));
            QPainter background(&m_backgroundTexture);
            background.scale(dpr, dpr);
            background.setRenderHint(QPainter::SmoothPixmapTransform);
            background.drawImage(rect(), m_backgroundFrame, source);
        }
        m_backgroundTextureDpr = dpr;
        m_backgroundTextureFrameRevision = m_backgroundFrameRevision;
    }
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.fillRect(bounds, QColor(7, 10, 15));
    const QRectF world = bounds.translated(worldOffset);
    painter.drawImage(bounds, m_backgroundTexture,
                      QRectF(world.topLeft() * dpr, world.size() * dpr));
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
    if (m_background.kind == BackgroundKind::Liquid || m_backgroundFrame.isNull() || m_backgroundFailed) {
        paintWater(painter, bounds, worldOffset);
        return;
    }
    // The clear texture and this small blur are shared by all visible glass
    // overlays. A changing movie never recaptures the account table's contents.
    paintCustomBackground(painter, bounds, worldOffset);
    const QSize blurSize(std::max(1, width() / 6), std::max(1, height() / 6));
    if (m_backgroundBlurred.size() != blurSize
        || m_backgroundBlurredFrameRevision != m_backgroundFrameRevision) {
        m_backgroundBlurred = m_backgroundTexture.scaled(blurSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int pass = 0; pass < 2; ++pass) {
            m_backgroundBlurred = blurBackground(m_backgroundBlurred, true);
            m_backgroundBlurred = blurBackground(m_backgroundBlurred, false);
        }
        m_backgroundBlurredFrameRevision = m_backgroundFrameRevision;
    }
    const qreal scaleX = qreal(blurSize.width()) / width();
    const qreal scaleY = qreal(blurSize.height()) / height();
    const QRectF world = bounds.translated(worldOffset);
    const QRectF source(world.x() * scaleX, world.y() * scaleY,
                        world.width() * scaleX, world.height() * scaleY);
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(bounds, m_backgroundBlurred, source);
    if (m_background.dimming > 0)
        painter.fillRect(bounds, QColor(0, 0, 0, m_background.dimming * 255 / 100));
    painter.restore();
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
    // Soft fields need neither device-resolution rasterization nor 144 full
    // gradient renders per second. UI motion remains at the screen's refresh
    // rate, while all cells sample a shared half-resolution 30 Hz water frame.
    constexpr qreal scale = .5;
    const QSize frameSize(std::max(1, int(std::ceil(width() * scale))),
                          std::max(1, int(std::ceil(height() * scale))));
    const qreal framePhase = std::floor(m_phase * 30.0) / 30.0;
    if (m_waterFrame.size() != frameSize || m_waterFramePhase != framePhase
        || m_waterFrameRevision != m_revision) {
        if (m_waterFrame.size() != frameSize)
            m_waterFrame = QImage(frameSize, QImage::Format_ARGB32_Premultiplied);
        QPainter framePainter(&m_waterFrame);
        framePainter.scale(scale, scale);
        paintWaterFrame(framePainter, rect(), {});
        m_waterFramePhase = framePhase;
        m_waterFrameRevision = m_revision;
        framePainter.end();
        // Upsample once per water frame, not once for every table cell/panel.
        // Static row transforms can then use inexpensive unscaled image copies.
        m_waterTexture = m_waterFrame.scaled(size(), Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
    }
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.fillRect(bounds, QColor(7, 10, 15));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(bounds, m_waterTexture, bounds.translated(worldOffset));
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
    painter.fillRect(worldBounds, QColor(7, 10, 15));

    // Broad, dim fields and slow folds give depth without drawing literal bubbles.
    QRadialGradient cyan(QPointF(w * (.19 + .07 * std::sin(m_phase * .13)),
                                h * (.34 + .10 * std::cos(m_phase * .11))), w * .65);
    QColor glow = m_accent;
    glow.setAlpha(39);
    cyan.setColorAt(0, glow);
    glow.setAlpha(15);
    cyan.setColorAt(.35, glow);
    glow.setAlpha(0);
    cyan.setColorAt(1, glow);
    painter.fillRect(worldBounds, cyan);

    QRadialGradient blue(QPointF(w * (.81 + .06 * std::cos(m_phase * .09)),
                                h * (.72 + .09 * std::sin(m_phase * .12))), w * .60);
    blue.setColorAt(0, QColor(70, 112, 194, 28));
    blue.setColorAt(.40, QColor(40, 94, 147, 10));
    blue.setColorAt(1, QColor(40, 94, 147, 0));
    painter.fillRect(worldBounds, blue);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int fold = 0; fold < 3; ++fold) {
        const qreal phase = m_phase * (.14 + fold * .018) + fold * 1.7;
        const qreal y = h * (.25 + fold * .24) + std::sin(phase) * h * .055;
        QPainterPath wave;
        wave.moveTo(-w * .18, y + h * .10);
        wave.cubicTo(w * .17, y - h * (.20 + std::sin(phase) * .025),
                     w * .58, y + h * .20, w * 1.15, y - h * .13);
        QColor haze = m_accent;
        haze.setAlpha(6);
        painter.setPen(QPen(haze, 24 + fold * 8));
        painter.drawPath(wave);
        painter.setPen(QPen(QColor(120, 205, 229, 12), 1.1));
        painter.drawPath(wave);
    }
    painter.restore();
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
        if (active)
            m_backgroundPlayer->play();
        else
            m_backgroundPlayer->pause();
    }
    if (m_backgroundMovie) {
        if (active && m_backgroundMovie->state() == QMovie::NotRunning)
            m_backgroundMovie->start();
        m_backgroundMovie->setPaused(!active);
    }
    if (!active || (m_background.kind != BackgroundKind::Liquid && !m_backgroundFailed)) {
        m_motionTimer->stop();
        return;
    }
    if (!m_motionTimer->isActive()) {
        m_motionTimer->setInterval(Aero::animationInterval(this));
        m_frameClock.start();
        m_motionTimer->start();
    }
}

bool AeroSurface::backgroundIsActive() const
{
    QWidget *currentWindow = window();
    QWidget *activeWindow = QApplication::activeWindow();
    const bool ownedDialogActive = activeWindow && activeWindow != currentWindow
        && currentWindow && currentWindow->isAncestorOf(activeWindow);
    return isVisible() && currentWindow && !currentWindow->isMinimized()
        && ((!m_windowDeactivated && currentWindow->isActiveWindow()) || ownedDialogActive);
}

void AeroSurface::advanceWater()
{
    if (!backgroundIsActive()) {
        m_motionTimer->stop();
        return;
    }
    m_phase += std::clamp(m_frameClock.restart() / 1000.0, 0.0, .10);
    repaintBackground();
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
        QTimer::singleShot(0, this, [this] { updateAnimationState(); });
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
            QTimer::singleShot(0, this, [this] { updateAnimationState(); });
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
