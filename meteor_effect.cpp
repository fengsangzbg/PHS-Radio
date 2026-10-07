#include "meteor_effect.h"
#include "aero_widgets.h"

#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal Pi = 3.14159265358979323846;
}

const std::array<PlaybackMeteor::Meteor, 7> PlaybackMeteor::Meteors{{
    // Normalized endpoints, staggered departures and different depths keep the
    // smaller companions from looking like copies of the leading meteor.
    {{1.00, .00}, {.00, 1.00},   0, 870, 1.00, 1.00, 1.00,  .000},
    {{ .93, .29}, {.18,  .82},  65, 735,  .68,  .66,  .76,  .024},
    {{ .77, .04}, {.00,  .94}, 140, 810,  .81,  .78,  .87, -.020},
    {{ .97, .41}, {.28,  .98}, 220, 690,  .49,  .47,  .62, -.030},
    {{ .86, .14}, {.10,  .66}, 290, 710,  .72,  .62,  .80,  .028},
    {{ .73, .25}, {.00,  .77}, 360, 650,  .43,  .39,  .57,  .018},
    {{ .98, .10}, {.24,  .93}, 420, 680,  .60,  .54,  .72, -.022},
}};

PlaybackMeteor::PlaybackMeteor(QWidget *host)
    : QWidget(host), m_host(host), m_timer(new QTimer(this))
{
    Q_ASSERT(host);
    setObjectName(QStringLiteral("playbackMeteor"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] { advance(); });
    m_host->installEventFilter(this);
    setGeometry(host->rect());
    hide();
}

void PlaybackMeteor::trigger()
{
    const QRegion previous = m_previousDirty;
    setGeometry(m_host->rect());
    rebuildSprites();
    m_progress = 0;
    m_running = true;
    m_clock.restart();
    m_previousDirty = frameRegion();
    show();
    raise();
    m_timer->start(Aero::animationInterval(m_host));
    update(previous.united(m_previousDirty));
}

bool PlaybackMeteor::isRunning() const { return m_running; }
qreal PlaybackMeteor::progress() const { return m_progress; }

qreal PlaybackMeteor::meteorProgress(int index) const
{
    const Meteor &meteor = Meteors.at(index);
    return (m_progress * DurationMs - meteor.delayMs) / meteor.travelMs;
}

qreal PlaybackMeteor::meteorOpacity(int index) const
{
    const qreal progress = meteorProgress(index);
    if (!m_running || progress <= 0 || progress >= 1)
        return 0;
    const qreal fadeIn = std::min(1.0, progress / .065);
    const qreal fadeOut = progress > .79 ? (1.0 - progress) / .21 : 1.0;
    const qreal shimmer = .95 + .05 * std::sin(progress * 12 + index * 1.7);
    return std::max(0.0, fadeIn * fadeOut * Meteors.at(index).brightness * shimmer);
}

int PlaybackMeteor::visibleMeteorCount() const
{
    int visible = 0;
    for (int index = 0; index < int(Meteors.size()); ++index)
        if (meteorOpacity(index) > .02)
            ++visible;
    return visible;
}

QPointF PlaybackMeteor::headPosition(int index) const
{
    const Meteor &meteor = Meteors.at(index);
    const QSizeF area(std::max(0.0, width() - 44.0), std::max(0.0, height() - 32.0));
    const QPointF start(22 + meteor.start.x() * area.width(),
                        14 + meteor.start.y() * area.height());
    const QPointF end(22 + meteor.end.x() * area.width(),
                      14 + meteor.end.y() * area.height());
    const QPointF travel = end - start;
    const qreal length = std::hypot(travel.x(), travel.y());
    const QPointF normal = length > 0 ? QPointF(-travel.y(), travel.x()) / length : QPointF();
    const qreal progress = std::clamp(meteorProgress(index), 0.0, 1.0);
    return start + travel * progress
        + normal * (std::sin(progress * Pi) * std::min(width(), height()) * meteor.bend);
}

QPointF PlaybackMeteor::direction(int index) const
{
    const Meteor &meteor = Meteors.at(index);
    QPointF travel((meteor.end.x() - meteor.start.x()) * std::max(0, width() - 44),
                   (meteor.end.y() - meteor.start.y()) * std::max(0, height() - 32));
    qreal length = std::hypot(travel.x(), travel.y());
    if (length > 0) {
        const QPointF normal(-travel.y() / length, travel.x() / length);
        travel += normal * (std::cos(std::clamp(meteorProgress(index), 0.0, 1.0) * Pi)
                           * Pi * std::min(width(), height()) * meteor.bend);
        length = std::hypot(travel.x(), travel.y());
    }
    return length > 0 ? travel / length : QPointF(-1, 0);
}

QRegion PlaybackMeteor::frameRegion() const
{
    QRegion region;
    for (int index = 0; index < int(Meteors.size()); ++index) {
        if (meteorOpacity(index) <= .001)
            continue;
        const Meteor &meteor = Meteors.at(index);
        const QPointF head = headPosition(index);
        const QPointF tail = head - direction(index) * (m_tailLength * meteor.tailScale);
        const qreal padding = 18 * meteor.headScale + 2;
        const QRect bounds = QRectF(
            QPointF(std::min(head.x(), tail.x()) - padding, std::min(head.y(), tail.y()) - padding),
            QPointF(std::max(head.x(), tail.x()) + padding, std::max(head.y(), tail.y()) + padding))
            .toAlignedRect().intersected(rect());
        region |= bounds;
    }
    return region;
}

QRect PlaybackMeteor::frameBounds() const { return frameRegion().boundingRect(); }

void PlaybackMeteor::advance()
{
    if (!m_running)
        return;
    rebuildSprites();
    const int interval = Aero::animationInterval(m_host);
    if (m_timer->interval() != interval)
        m_timer->setInterval(interval);
    m_progress = std::min(1.0, m_clock.elapsed() / qreal(DurationMs));
    const QRegion current = frameRegion();
    const QRegion dirty = m_previousDirty.united(current);
    m_previousDirty = current;
    if (m_progress >= 1) {
        m_running = false;
        m_timer->stop();
        hide();
        m_previousDirty = {};
        return;
    }
    if (!dirty.isEmpty())
        update(dirty); // Repaint individual old/new streak strips, not their large bounding box.
}

void PlaybackMeteor::rebuildSprites()
{
    const qreal dpr = devicePixelRatioF();
    const qreal tailLength = std::clamp(width() * .18, 80.0, 170.0);
    if (!m_headSprite.isNull() && qFuzzyCompare(m_cachedDpr, dpr) && qFuzzyCompare(m_tailLength, tailLength))
        return;
    m_cachedDpr = dpr;
    m_tailLength = tailLength;
    m_headSprite = QPixmap(QSize(qCeil(32 * dpr), qCeil(32 * dpr)));
    m_headSprite.setDevicePixelRatio(dpr);
    m_headSprite.fill(Qt::transparent);
    {
        QPainter painter(&m_headSprite);
        painter.setRenderHint(QPainter::Antialiasing);
        QRadialGradient glow(QPointF(16, 16), 16);
        glow.setColorAt(0, QColor(232, 252, 255, 220));
        glow.setColorAt(.12, QColor(127, 227, 255, 178));
        glow.setColorAt(.33, QColor(42, 167, 255, 87));
        glow.setColorAt(.72, QColor(28, 131, 255, 18));
        glow.setColorAt(1, QColor(28, 131, 255, 0));
        painter.setBrush(glow);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(0, 0, 32, 32));
        painter.setBrush(QColor(239, 253, 255, 235));
        painter.drawEllipse(QPointF(16, 16), .8, .8);
    }
    m_tailSprite = QPixmap(QSize(qCeil(tailLength * dpr), qCeil(18 * dpr)));
    m_tailSprite.setDevicePixelRatio(dpr);
    m_tailSprite.fill(Qt::transparent);
    {
        QPainter painter(&m_tailSprite);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient glow(0, 0, tailLength, 0);
        glow.setColorAt(0, QColor(28, 142, 255, 0));
        glow.setColorAt(.55, QColor(43, 168, 255, 13));
        glow.setColorAt(1, QColor(102, 218, 255, 65));
        painter.setPen(QPen(QBrush(glow), 5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(2, 9), QPointF(tailLength - 1, 9));
        glow.setColorAt(.55, QColor(83, 205, 255, 47));
        glow.setColorAt(1, QColor(167, 239, 255, 154));
        painter.setPen(QPen(QBrush(glow), 1.1, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(2, 9), QPointF(tailLength - 1, 9));
    }
}

bool PlaybackMeteor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host) {
        if (event->type() == QEvent::Resize) {
            const QRegion previous = m_previousDirty;
            setGeometry(m_host->rect());
            rebuildSprites();
            if (m_running) {
                m_previousDirty = frameRegion();
                update(previous.united(m_previousDirty));
            }
        } else if (event->type() == QEvent::Hide) {
            m_running = false;
            m_timer->stop();
            m_previousDirty = {};
            hide();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlaybackMeteor::paintEvent(QPaintEvent *)
{
    if (!m_running)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath panel;
    panel.addRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 24, 24);
    painter.setClipPath(panel, Qt::IntersectClip);
    // Paint distant companions first, so the brighter leading head keeps its depth.
    for (int index = int(Meteors.size()) - 1; index >= 0; --index) {
        const qreal opacity = meteorOpacity(index);
        if (opacity <= .001)
            continue;
        const Meteor &meteor = Meteors.at(index);
        const QPointF head = headPosition(index);
        const QPointF travel = direction(index);
        const qreal scale = meteor.headScale;
        const qreal tailLength = m_tailLength * meteor.tailScale;
        painter.save();
        painter.setOpacity(opacity);
        painter.translate(head);
        painter.rotate(qRadiansToDegrees(std::atan2(travel.y(), travel.x())));
        painter.drawPixmap(QRectF(-tailLength, -9 * scale, tailLength, 18 * scale),
                           m_tailSprite, QRectF(m_tailSprite.rect()));
        painter.drawPixmap(QRectF(-16 * scale, -16 * scale, 32 * scale, 32 * scale),
                           m_headSprite, QRectF(m_headSprite.rect()));
        painter.restore();
    }
}
