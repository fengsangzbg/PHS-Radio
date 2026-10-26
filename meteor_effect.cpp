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
    {{ .98,  .06}, {.20,  .92},    0, 720, 1.00, .00, 1.00, -.010},
    {{ .90, -.04}, {.34,  .82},  110, 690,  .80, .00,  .86,  .012},
    {{1.05,  .14}, {.44, 1.00},  220, 740,  .90, .00,  .94,  .006},
    {{ .78, -.02}, {.10,  .86},  330, 690,  .64, .00,  .72, -.014},
    {{ .88,  .10}, {.28,  .96},  450, 730,  .84, .00,  .88,  .008},
    {{ .68,  .02}, {.06,  .88},  570, 700,  .70, .00,  .80, -.006},
    {{ .82, -.06}, {.18,  .90},  690, 710,  .58, .00,  .68,  .010},
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
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { advance(); });
    m_host->installEventFilter(this);
    setGeometry(host->rect());
    Aero::observeDisplayRefresh(m_host, this, [this] {
        dropSprites();
        if (m_running && m_clock.isValid()) {
            m_schedule.setRefreshRate(Aero::displayRefreshRate(m_host), m_clock.nsecsElapsed());
            m_timer->start(m_schedule.delayMs(m_clock.nsecsElapsed()));
            update(m_previousDirty);
        }
    });
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
    ensureSprites(); // Warm the light textures before the motion clock starts.
    m_clock.restart();
    m_schedule.setRefreshRate(Aero::displayRefreshRate(m_host), 0);
    m_schedule.reset(0);
    m_previousDirty = frameRegion();
    show();
    raise();
    m_timer->start(m_schedule.delayMs(m_clock.nsecsElapsed()));
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
    // A slight acceleration keeps the meteor gliding through the exit instead
    // of visibly stopping at both ends like a large UI easing animation.
    const qreal s = std::clamp(travel, 0.0, 1.0);
    const qreal t = s * (.88 + .12 * s);
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
        const QPointF head = headPosition(index), tail = tailPosition(index);
        const QPointF tangent = direction(index), normal(-tangent.y(), tangent.x());
        const qreal padding = 22 * depth + 2;
        QPolygon polygon;
        for (const QPointF &point : {tail + normal * padding - tangent * 2,
                                    head + normal * padding + tangent * padding,
                                    head - normal * padding + tangent * padding,
                                    tail - normal * padding - tangent * 2})
            polygon << point.toPoint();
        region |= QRegion(polygon, Qt::WindingFill);
    }
    return region.intersected(rect());
}

QRect PlaybackMeteor::frameBounds() const { return frameRegion().boundingRect(); }

void PlaybackMeteor::advance()
{
    if (!m_running)
        return;
    const qint64 now = m_clock.nsecsElapsed();
    m_schedule.setRefreshRate(Aero::displayRefreshRate(m_host), now);
    if (!m_schedule.advance(now)) {
        m_timer->start(m_schedule.delayMs(now));
        return;
    }
    m_progress = std::min(1.0, now / (durationMs() * 1000000.0));
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
    m_timer->start(m_schedule.delayMs(m_clock.nsecsElapsed()));
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
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    ensureSprites();
    painter.setClipPath(m_style == Style::Fire ? m_fireClip : m_waterClip, Qt::IntersectClip);
    if (m_style == Style::Fire)
        paintLegacyFire(painter);
    else
        paintRibbons(painter);
}

void PlaybackMeteor::dropSprites()
{
    m_spriteDpr = 0;
    m_tailSprite = {};
    m_headSprite = {};
}

void PlaybackMeteor::ensureSprites()
{
    if (m_clipSize != size()) {
        m_clipSize = size();
        m_waterClip = {}; m_fireClip = {};
        m_waterClip.addRoundedRect(QRectF(rect()).adjusted(1.5,1.5,-1.5,-1.5),26,26);
        m_fireClip.addRoundedRect(QRectF(rect()).adjusted(2,2,-2,-2),24,24);
    }
    const qreal dpr = devicePixelRatioF();
    if (m_style == Style::Fire || (qFuzzyCompare(dpr,m_spriteDpr) && !m_tailSprite.isNull()))
        return;
    m_spriteDpr = dpr;
    constexpr qreal width = 1024, height = 32;
    m_tailSprite = QImage(QSize(qCeil(width*dpr),qCeil(height*dpr)),QImage::Format_ARGB32_Premultiplied);
    m_tailSprite.setDevicePixelRatio(dpr);m_tailSprite.fill(Qt::transparent);
    {
        QPainter light(&m_tailSprite);light.setRenderHint(QPainter::Antialiasing);
        light.translate(0,height/2);
        QLinearGradient mist(0,0,width,0);
        mist.setColorAt(0,QColor(46,145,220,0));mist.setColorAt(.28,QColor(75,201,237,8));
        mist.setColorAt(.72,QColor(105,227,245,40));mist.setColorAt(.96,QColor(170,245,255,90));
        mist.setColorAt(1,QColor(220,253,255,130));
        QPainterPath axis;axis.moveTo(0,0);axis.cubicTo(width*.35,-.2,width*.75,-.6,width,0);
        light.setPen(QPen(QBrush(mist),10,Qt::SolidLine,Qt::RoundCap));light.drawPath(axis);
        light.setPen(QPen(QBrush(mist),4,Qt::SolidLine,Qt::RoundCap));light.drawPath(axis);
        QPainterPath film;film.moveTo(0,0);film.cubicTo(width*.28,-.12,width*.8,-1.2,width*.96,-1.7);
        film.quadTo(width,-.7,width,0);film.quadTo(width,.7,width*.96,1.2);
        film.cubicTo(width*.8,.55,width*.28,.08,0,0);film.closeSubpath();
        QLinearGradient water(0,0,width,0);water.setColorAt(0,QColor(25,110,210,0));
        water.setColorAt(.24,QColor(45,169,220,10));water.setColorAt(.70,QColor(113,227,246,80));
        water.setColorAt(.94,QColor(199,249,255,160));water.setColorAt(1,QColor(246,255,255,230));
        light.setPen(Qt::NoPen);light.setBrush(water);light.drawPath(film);
        QLinearGradient core(0,0,width,0);core.setColorAt(0,QColor(174,246,255,0));
        core.setColorAt(.45,QColor(167,241,254,15));core.setColorAt(.88,QColor(231,253,255,140));
        core.setColorAt(1,QColor(255,255,255,245));light.setBrush(Qt::NoBrush);
        light.setPen(QPen(QBrush(core),.7,Qt::SolidLine,Qt::RoundCap));light.drawPath(axis);
    }
    constexpr qreal diameter=40;
    m_headSprite=QImage(QSize(qCeil(diameter*dpr),qCeil(diameter*dpr)),QImage::Format_ARGB32_Premultiplied);
    m_headSprite.setDevicePixelRatio(dpr);m_headSprite.fill(Qt::transparent);
    {
        QPainter light(&m_headSprite);light.setRenderHint(QPainter::Antialiasing);
        QRadialGradient glow(QPointF(20,20),18);glow.setColorAt(0,QColor(228,252,255,190));
        glow.setColorAt(.12,QColor(141,233,255,100));glow.setColorAt(.30,QColor(89,207,250,24));
        glow.setColorAt(.68,QColor(67,175,240,5));glow.setColorAt(1,QColor(38,140,230,0));
        light.setPen(Qt::NoPen);light.setBrush(glow);light.drawEllipse(QPointF(20,20),18,18);
        light.setBrush(QColor(246,255,255,235));light.drawEllipse(QPointF(20,20),1.15,.9);
    }
}

void PlaybackMeteor::paintRibbons(QPainter &painter)
{
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int index=beamCount()-1;index>=0;--index) {
        const qreal opacity=meteorOpacity(index);if(opacity<=.004)continue;
        const QPointF head=headPosition(index),tail=tailPosition(index),delta=head-tail;
        const qreal length=std::hypot(delta.x(),delta.y());if(length<2)continue;
        const qreal depth=beam(index).headScale;
        painter.save();painter.translate(tail);
        painter.rotate(qRadiansToDegrees(std::atan2(delta.y(),delta.x())));painter.setOpacity(opacity);
        painter.drawImage(QRectF(0,-16*depth,length,32*depth),m_tailSprite);
        painter.drawImage(QRectF(length-20*depth,-20*depth,40*depth,40*depth),m_headSprite);
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
