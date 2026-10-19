#include "meteor_effect.h"
#include "aero_widgets.h"

#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>


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
    if(m_style == style) return;
    m_style = style;
    if(m_running) trigger();
}

qreal PlaybackMeteor::meteorProgress(int index) const
{
    const Meteor &meteor = Meteors.at(index);
    return (m_progress * DurationMs - meteor.delayMs) / meteor.travelMs;
}

qreal PlaybackMeteor::meteorOpacity(int index) const
{
    const qreal progress = meteorProgress(index);
    if (!m_running || progress < 0 || progress >= 1)
        return 0;
    // A complete streak flashes on its first frame, then extinguishes.
    return std::pow(1.0-progress,1.2)*Meteors.at(index).brightness;
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
    const QPointF end = Meteors.at(index).end;
    return QPointF(22+end.x()*std::max(0,width()-44),14+end.y()*std::max(0,height()-32));
}

QPointF PlaybackMeteor::tailPosition(int index) const
{
    const QPointF start = Meteors.at(index).start;
    return QPointF(22+start.x()*std::max(0,width()-44),14+start.y()*std::max(0,height()-32));
}

QPointF PlaybackMeteor::direction(int index) const
{
    const Meteor &meteor = Meteors.at(index);
    QPointF travel((meteor.end.x() - meteor.start.x()) * std::max(0, width() - 44),
                   (meteor.end.y() - meteor.start.y()) * std::max(0, height() - 32));
    qreal length = std::hypot(travel.x(), travel.y());
    return length > 0 ? travel / length : QPointF(-1, 0);
}

QRegion PlaybackMeteor::frameRegion() const
{
    QRegion region;
    for(int index=0;index<int(Meteors.size());++index) {
        if(meteorOpacity(index)<=.001) continue;
        QPainterPath beam;
        beam.moveTo(tailPosition(index));beam.lineTo(headPosition(index));
        QPainterPathStroker outline;outline.setWidth(12);outline.setCapStyle(Qt::RoundCap);
        region |= QRegion(outline.createStroke(beam).toFillPolygon().toPolygon());
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
    const qreal duration = 1000.0;
    m_progress = std::min(1.0, m_clock.elapsed() / duration);
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
    if(!m_running) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath panel;
    panel.addRoundedRect(QRectF(rect()).adjusted(2,2,-2,-2),24,24);
    painter.setClipPath(panel,Qt::IntersectClip);
    const bool fire=m_style==Style::Fire;
    for(int index=int(Meteors.size())-1;index>=0;--index) {
        const qreal opacity=meteorOpacity(index);
        if(opacity<=.001) continue;
        const QPointF upperRight=tailPosition(index),lowerLeft=headPosition(index);
        // Both endpoints and widths stay fixed for the entire flash. Only
        // opacity changes: no moving head, shrinking beam or drifting sparks.
        QLinearGradient glow(upperRight,lowerLeft);
        glow.setColorAt(0,QColor(0,0,0,0));
        glow.setColorAt(.12,fire?QColor(255,119,36,50):QColor(38,255,118,45));
        glow.setColorAt(.65,fire?QColor(255,177,67,125):QColor(64,255,150,125));
        glow.setColorAt(1,fire?QColor(255,218,131,160):QColor(106,255,177,160));
        const qreal depth=Meteors.at(index).headScale;
        // Nested low-opacity fringes and an off-centre highlight give the
        // fixed beam a rounded luminous cross-section without a raster blur.
        painter.setOpacity(opacity*.18);
        painter.setPen(QPen(QBrush(glow),8.0*depth,Qt::SolidLine,Qt::FlatCap));
        painter.drawLine(upperRight,lowerLeft);
        painter.setOpacity(opacity*.45);
        painter.setPen(QPen(QBrush(glow),5.0*depth,Qt::SolidLine,Qt::FlatCap));
        painter.drawLine(upperRight,lowerLeft);
        painter.setOpacity(opacity);
        painter.setPen(QPen(QBrush(glow),3.5*depth,Qt::SolidLine,Qt::FlatCap));
        painter.drawLine(upperRight,lowerLeft);
        glow.setColorAt(.12,fire?QColor(255,186,92,85):QColor(106,255,177,85));
        glow.setColorAt(.65,fire?QColor(255,235,181,220):QColor(172,255,207,220));
        glow.setColorAt(1,fire?QColor(255,250,222,245):QColor(206,255,224,245));
        painter.setPen(QPen(QBrush(glow),std::max(.7,1.3*depth),Qt::SolidLine,Qt::FlatCap));
        painter.drawLine(upperRight,lowerLeft);
        const QPointF travel=direction(index);
        const QPointF highlight(-travel.y()*.35*depth,travel.x()*.35*depth);
        glow.setColorAt(.65,fire?QColor(255,250,219,220):QColor(226,255,237,220));
        glow.setColorAt(1,fire?QColor(255,253,231,250):QColor(235,255,244,250));
        painter.setOpacity(opacity*.65);
        painter.setPen(QPen(QBrush(glow),.45,Qt::SolidLine,Qt::FlatCap));
        painter.drawLine(upperRight+highlight,lowerLeft+highlight);
    }
}
