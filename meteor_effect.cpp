#include "meteor_effect.h"
#include "aero_widgets.h"

#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

// Water dock: long, shallow streaks that follow the reference clip: nearly
// horizontal, spanning most of the panel, launched in a loose overlap so two or
// three are aloft at once without ever becoming an evenly spaced row.
const std::array<PlaybackMeteor::Meteor, 7> PlaybackMeteor::Ribbons = {{
    {{ .98,  .06}, {.20,  .92},    0, 540, 1.00, .00, 1.00, -.010},
    {{ .90, -.04}, {.34,  .82},  110, 520,  .80, .00,  .86,  .012},
    {{1.05,  .14}, {.44, 1.00},  220, 550,  .90, .00,  .94,  .006},
    {{ .78, -.02}, {.10,  .86},  330, 520,  .64, .00,  .72, -.014},
    {{ .88,  .10}, {.28,  .96},  450, 540,  .84, .00,  .88,  .008},
    {{ .68,  .02}, {.06,  .88},  570, 520,  .70, .00,  .80, -.006},
    {{ .82, -.06}, {.18,  .90},  690, 530,  .58, .00,  .68,  .010},
}};

// Fire dock: the original fixed-beam flash, preserved as-is.
const std::array<PlaybackMeteor::Meteor, 7> PlaybackMeteor::FireBeams = {{
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

int PlaybackMeteor::durationMs() const
{
    return m_style == Style::Fire ? FireDurationMs : RibbonDurationMs;
}

int PlaybackMeteor::beamCount() const
{
    return m_style == Style::Fire ? int(FireBeams.size()) : int(Ribbons.size());
}

const PlaybackMeteor::Meteor &PlaybackMeteor::beam(int index) const
{
    return m_style == Style::Fire ? FireBeams.at(index) : Ribbons.at(index);
}

void PlaybackMeteor::trigger()
{
    const QRegion previous = m_previousDirty;
    setGeometry(m_host->rect());
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

void PlaybackMeteor::setStyle(Style style)
{
    if (m_style == style)
        return;
    m_style = style;
    if (m_running)
        trigger();
}

qreal PlaybackMeteor::meteorProgress(int index) const
{
    const Meteor &meteor = beam(index);
    return (m_progress * durationMs() - meteor.delayMs) / meteor.travelMs;
}

qreal PlaybackMeteor::meteorOpacity(int index) const
{
    if (!m_running)
        return 0;
    const qreal travel = meteorProgress(index);
    if (travel < 0 || travel >= 1)
        return 0;
    if (m_style == Style::Fire) {
        // Original behaviour: a complete streak flashes, then extinguishes.
        return std::pow(1.0 - travel, 1.2) * beam(index).brightness;
    }
    // Ribbons materialise, sweep, and are gone: no long lingering fade.
    // A quarter second of ramp is all a fast sweep gets; start it slightly
    // alight so the first frame already shows the streak forming.
    const qreal ignite = .30 + .70 * std::clamp(travel / .18, 0.0, 1.0);
    const qreal burnout = std::clamp((1.0 - travel) / .30, 0.0, 1.0);
    return beam(index).brightness * ignite * burnout;
}

int PlaybackMeteor::visibleMeteorCount() const
{
    int visible = 0;
    for (int index = 0; index < beamCount(); ++index)
        if (meteorOpacity(index) > .02)
            ++visible;
    return visible;
}

QPointF PlaybackMeteor::mapped(const QPointF &normalized) const
{
    return QPointF(22 + normalized.x() * std::max(0, width() - 44),
                   14 + normalized.y() * std::max(0, height() - 32));
}

QPointF PlaybackMeteor::pointAt(int index, qreal travel) const
{
    const Meteor &meteor = beam(index);
    const QPointF start = mapped(meteor.start);
    const QPointF end = mapped(meteor.end);
    const QPointF delta = end - start;
    const QPointF normal(-delta.y(), delta.x());
    const qreal length = std::hypot(normal.x(), normal.y());
    const QPointF control = (start + end) * .5
        + (length > 0 ? normal / length : QPointF()) * (meteor.bend * length);
    // Smoothstep on the travel distance: the ribbon swells out of the edge,
    // glides through the middle and eases off, instead of sliding linearly.
    const qreal s = std::clamp(travel, 0.0, 1.0);
    const qreal t = s * s * (3.0 - 2.0 * s);
    const qreal u = 1.0 - t;
    const QPointF position = start * (u * u) + control * (2 * u * t) + end * (t * t);
    if (travel >= 0)
        return position;
    // Behind the start the path is extrapolated along its tangent, so the tail
    // is already a full ribbon length out of frame when the head first appears
    // instead of the streak growing from a point.
    return start + 2.0 * (control - start) * travel;
}

QPointF PlaybackMeteor::headPosition(int index) const
{
    if (m_style == Style::Fire)
        return mapped(beam(index).end); // Fixed endpoints: the beam flashes in place.
    return pointAt(index, meteorProgress(index));
}

QPointF PlaybackMeteor::tailPosition(int index) const
{
    if (m_style == Style::Fire)
        return mapped(beam(index).start);
    return pointAt(index, meteorProgress(index) - TailSpan);
}

QPointF PlaybackMeteor::direction(int index) const
{
    QPointF delta = headPosition(index) - tailPosition(index);
    qreal length = std::hypot(delta.x(), delta.y());
    if (length < .001) {
        const Meteor &meteor = beam(index);
        delta = meteor.end - meteor.start;
        length = std::hypot(delta.x(), delta.y());
    }
    return length > .001 ? delta / length : QPointF(-1, 0);
}

QRegion PlaybackMeteor::frameRegion() const
{
    QRegion region;
    if (m_style == Style::Fire) {
        for (int index = 0; index < beamCount(); ++index) {
            if (meteorOpacity(index) <= .001)
                continue;
            QPainterPath beamPath;
            beamPath.moveTo(tailPosition(index));
            beamPath.lineTo(headPosition(index));
            QPainterPathStroker outline;
            outline.setWidth(12);
            outline.setCapStyle(Qt::RoundCap);
            region |= QRegion(outline.createStroke(beamPath).toFillPolygon().toPolygon());
        }
        return region.intersected(rect());
    }
    for (int index = 0; index < beamCount(); ++index) {
        if (meteorOpacity(index) <= .01)
            continue;
        const qreal depth = beam(index).headScale;
        const QRectF span(headPosition(index), tailPosition(index));
        region |= QRegion(span.normalized().adjusted(-56 * depth, -56 * depth, 56 * depth,
                                                     56 * depth).toAlignedRect());
    }
    return region.intersected(rect());
}

QRect PlaybackMeteor::frameBounds() const { return frameRegion().boundingRect(); }

void PlaybackMeteor::advance()
{
    if (!m_running)
        return;
    const int interval = Aero::animationInterval(m_host);
    if (m_timer->interval() != interval)
        m_timer->setInterval(interval);
    m_progress = std::min(1.0, m_clock.elapsed() / qreal(durationMs()));
    if (m_progress >= 1) {
        m_running = false;
        m_timer->stop();
        hide();
        m_previousDirty = {};
        return;
    }
    const QRegion current = frameRegion();
    const QRegion dirty = m_previousDirty.united(current);
    m_previousDirty = current;
    if (!dirty.isEmpty())
        update(dirty);
}

bool PlaybackMeteor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host) {
        if (event->type() == QEvent::Resize) {
            const QRegion previous = m_previousDirty;
            setGeometry(m_host->rect());
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
    QPainterPath clip;
    if (m_style == Style::Fire) {
        // Same silhouette and radius the original fire flash was clipped to.
        clip.addRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 24, 24);
    } else {
        clip.addRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 26, 26);
    }
    painter.setClipPath(clip, Qt::IntersectClip);
    if (m_style == Style::Fire)
        paintLegacyFire(painter);
    else
        paintRibbons(painter);
}

void PlaybackMeteor::paintRibbons(QPainter &painter)
{
    // Vector, not a scaled sprite: every edge stays sharp at any length and any
    // device pixel ratio, which is exactly what "clear" needs.
    for (int index = beamCount() - 1; index >= 0; --index) {
        const qreal opacity = meteorOpacity(index);
        if (opacity <= .004)
            continue;
        const qreal depth = beam(index).headScale;
        const QPointF head = headPosition(index);
        const QPointF tail = tailPosition(index);
        const QPointF delta = head - tail;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length < 2.0)
            continue;
        const qreal thickness = 11.0 * depth;

        painter.save();
        painter.translate(tail);
        painter.rotate(qRadiansToDegrees(std::atan2(delta.y(), delta.x())));
        painter.setOpacity(opacity);

        // Local space: x runs 0..length, the body bows around y = 0.
        auto halfAt = [&](qreal t) {
            const qreal shape = std::pow(std::max(0.0, t), .50)
                * std::pow(std::max(0.0, 1.0 - t), .30);
            return .6 + (thickness * .5) * (shape / .58);
        };
        auto centerY = [&](qreal t) { return -std::sin(t * M_PI) * thickness * .30; };

        constexpr int samples = 64;
        QPainterPath lens, core;
        for (int step = 0; step <= samples; ++step) {
            const qreal t = step / qreal(samples);
            const QPointF point(t * length, centerY(t) - halfAt(t));
            if (step == 0)
                lens.moveTo(point);
            else
                lens.lineTo(point);
        }
        for (int step = samples; step >= 0; --step) {
            const qreal t = step / qreal(samples);
            lens.lineTo(QPointF(t * length, centerY(t) + halfAt(t)));
        }
        lens.closeSubpath();

        for (int step = 0; step <= samples; ++step) {
            const qreal t = step / qreal(samples);
            const QPointF point(t * length, centerY(t) - halfAt(t) * .30);
            if (step == 0)
                core.moveTo(point);
            else
                core.lineTo(point);
        }
        for (int step = samples; step >= 0; --step) {
            const qreal t = step / qreal(samples);
            core.lineTo(QPointF(t * length, centerY(t) + halfAt(t) * .30));
        }
        core.closeSubpath();

        QLinearGradient body(0, -thickness * .5, 0, thickness * .5);
        body.setColorAt(0.00, QColor(104, 208, 240, 0));
        body.setColorAt(0.24, QColor(126, 219, 246, 120));
        body.setColorAt(0.44, QColor(206, 246, 255, 215));
        body.setColorAt(0.50, QColor(255, 255, 255, 250));
        body.setColorAt(0.56, QColor(206, 246, 255, 215));
        body.setColorAt(0.76, QColor(126, 219, 246, 120));
        body.setColorAt(1.00, QColor(104, 208, 240, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(body);
        painter.drawPath(lens);

        QLinearGradient filament(0, -thickness * .16, 0, thickness * .16);
        filament.setColorAt(0.0, QColor(255, 255, 255, 90));
        filament.setColorAt(0.5, QColor(255, 255, 255, 255));
        filament.setColorAt(1.0, QColor(255, 255, 255, 90));
        painter.setBrush(filament);
        painter.drawPath(core);

        // Spectral fringe on both rims; only the near streaks carry it, which
        // keeps the far ones from turning into coloured noise.
        if (depth > .74) {
            for (int side = -1; side <= 1; side += 2) {
                for (int band = 0; band < 3; ++band) {
                    const int hues[3] = {268, 200, 318};
                    QColor tint = QColor::fromHsv(hues[band], 175, 255);
                    tint.setAlphaF(.26 - band * .05);
                    QPainterPath fringe;
                    for (int step = 0; step <= samples; ++step) {
                        const qreal t = .04 + .92 * (step / qreal(samples));
                        const QPointF point(
                            t * length,
                            centerY(t) + side * halfAt(t) * (.80 + band * .06));
                        if (step == 0)
                            fringe.moveTo(point);
                        else
                            fringe.lineTo(point);
                    }
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(tint, 1.1, Qt::SolidLine, Qt::RoundCap));
                    painter.drawPath(fringe);
                }
            }
        }
        painter.restore();
    }
}

// Verbatim behaviour of the original fire style: a complete streak flashes on
// its first frame, then extinguishes, with no moving head.
void PlaybackMeteor::paintLegacyFire(QPainter &painter)
{
    for (int index = beamCount() - 1; index >= 0; --index) {
        const qreal opacity = meteorOpacity(index);
        if (opacity <= .001)
            continue;
        const QPointF upperRight = tailPosition(index), lowerLeft = headPosition(index);
        QLinearGradient glow(upperRight, lowerLeft);
        glow.setColorAt(0, QColor(0, 0, 0, 0));
        glow.setColorAt(.12, QColor(255, 119, 36, 50));
        glow.setColorAt(.65, QColor(255, 177, 67, 125));
        glow.setColorAt(1, QColor(255, 218, 131, 160));
        const qreal depth = beam(index).headScale;
        painter.setOpacity(opacity * .18);
        painter.setPen(QPen(QBrush(glow), 8.0 * depth, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(upperRight, lowerLeft);
        painter.setOpacity(opacity * .45);
        painter.setPen(QPen(QBrush(glow), 5.0 * depth, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(upperRight, lowerLeft);
        painter.setOpacity(opacity);
        painter.setPen(QPen(QBrush(glow), 3.5 * depth, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(upperRight, lowerLeft);
        glow.setColorAt(.12, QColor(255, 186, 92, 85));
        glow.setColorAt(.65, QColor(255, 235, 181, 220));
        glow.setColorAt(1, QColor(255, 250, 222, 245));
        painter.setPen(QPen(QBrush(glow), std::max(.7, 1.3 * depth), Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(upperRight, lowerLeft);
        const QPointF travel = direction(index);
        const QPointF highlight(-travel.y() * .35 * depth, travel.x() * .35 * depth);
        glow.setColorAt(.65, QColor(255, 250, 219, 220));
        glow.setColorAt(1, QColor(255, 253, 231, 250));
        painter.setOpacity(opacity * .65);
        painter.setPen(QPen(QBrush(glow), .45, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(upperRight + highlight, lowerLeft + highlight);
    }
}
