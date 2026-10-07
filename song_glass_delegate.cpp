#include "song_glass_delegate.h"

#include "aero_widgets.h"
#include "liquid_backdrop.h"

#include <QEasingCurve>
#include <QIcon>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTableWidget>
#include <QTransform>
#include <QVariantAnimation>
#include <cmath>

SongGlassDelegate::SongGlassDelegate(QTableWidget *table)
    : QStyledItemDelegate(table), m_table(table), m_viewport(table->viewport()), m_accent(Aero::defaultAccent()),
      m_pressAnimation(new QVariantAnimation(this))
{
    table->setMouseTracking(true);
    table->viewport()->installEventFilter(this);
    const auto detach = [this] {
        // A QTableWidget destroys its model before QObject deletes child
        // widgets. Stop observing the viewport before those teardown events.
        if (m_viewport)
            m_viewport->removeEventFilter(this);
        m_table = nullptr;
        m_motionTimer.stop();
        m_pressAnimation->stop();
    };
    connect(table->model(), &QObject::destroyed, this, detach);
    connect(table, &QObject::destroyed, this, detach);
    m_previousScroll = table->verticalScrollBar()->value();
    m_motionTimer.setTimerType(Qt::PreciseTimer);
    m_motionTimer.setInterval(Aero::animationInterval(table));
    connect(table->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) { beginScrollMotion(value); });
    connect(&m_motionTimer, &QTimer::timeout, this, [this] {
        if (!m_table) {
            m_motionTimer.stop();
            return;
        }
        m_motionAge = m_motionClock.elapsed() / 1000.0;
        if (m_motionAge >= 0.46) {
            m_motionTimer.stop();
            m_motionEnvelope = 0;
        } else {
            m_motionEnvelope = std::exp(-m_motionAge * 8.0);
        }
        m_table->viewport()->update();
    });
    connect(m_pressAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_pressDepth = qBound(-0.25, value.toReal(), 1.05);
        updateRow(m_pressedRow);
    });
    connect(m_pressAnimation, &QVariantAnimation::finished, this, [this] {
        if (!m_pointerHeld) {
            const int releasedRow = m_pressedRow;
            m_pressedRow = -1;
            m_pressDepth = 0;
            updateRow(releasedRow);
        }
    });
}

SongGlassDelegate::~SongGlassDelegate()
{
    if (m_viewport)
        m_viewport->removeEventFilter(this);
    m_motionTimer.stop();
    m_pressAnimation->stop();
}

void SongGlassDelegate::setAccentColor(const QColor &accent)
{
    if (!accent.isValid() || accent == m_accent)
        return;
    m_accent = accent;
    m_glassLayers.clear();
    if (m_table)
        m_table->viewport()->update();
}

void SongGlassDelegate::updateRow(int row)
{
    if (!m_table || !dynamic_cast<QTableWidget *>(static_cast<QWidget *>(m_table))
        || !m_table->model() || row < 0 || row >= m_table->rowCount() || m_table->columnCount() == 0)
        return;
    const QRect left = m_table->visualRect(m_table->model()->index(row, 0));
    const QRect right = m_table->visualRect(m_table->model()->index(row, m_table->columnCount() - 1));
    const QRect dirty = left.united(right).adjusted(-7, -5, 7, 5)
        .intersected(m_table->viewport()->rect());
    if (!dirty.isEmpty())
        m_table->viewport()->update(dirty);
}

const QPixmap &SongGlassDelegate::glassLayer(const QSizeF &size, qreal dpr, bool selected) const
{
    const QString key = QStringLiteral("%1:%2:%3:%4:%5")
        .arg(qRound(size.width() * 16)).arg(qRound(size.height() * 16))
        .arg(qRound(dpr * 1000)).arg(m_accent.rgba()).arg(selected);
    if (QPixmap *cached = m_glassLayers.object(key))
        return *cached;

    constexpr qreal padding = 6;
    auto *layer = new QPixmap(qCeil((size.width() + padding * 2) * dpr),
                              qCeil((size.height() + padding * 2) * dpr));
    layer->setDevicePixelRatio(dpr);
    layer->fill(Qt::transparent);
    QPainter painter(layer);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(QPointF(padding, padding), size);
    Aero::paintGlassBase(painter, bounds, m_accent, 18);
    if (selected) {
        QColor lowGlow = m_accent;
        lowGlow.setAlpha(20);
        QColor clearGlow = m_accent;
        clearGlow.setAlpha(0);
        QLinearGradient glow(bounds.topLeft(), bounds.bottomLeft());
        glow.setColorAt(0, clearGlow);
        glow.setColorAt(1, lowGlow);
        QPainterPath silhouette;
        silhouette.addRoundedRect(bounds, 18, 18);
        painter.fillPath(silhouette, glow);
        painter.setPen(QPen(QColor(232, 242, 252, 160), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 17, 17);
        const QRectF stripe(bounds.left() + 3.5, bounds.top() + 13,
                            2.0, qMax(4.0, bounds.height() - 26));
        QColor accentGlow = m_accent;
        accentGlow.setAlpha(135);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accentGlow);
        painter.drawRoundedRect(stripe, 1, 1);
    }
    painter.end();
    m_glassLayers.insert(key, layer);
    return *layer;
}

const QPixmap &SongGlassDelegate::coverLayer(const QIcon &icon, int side, qreal dpr, bool enabled) const
{
    const QString key = QStringLiteral("%1:%2:%3:%4")
        .arg(icon.cacheKey()).arg(side).arg(qRound(dpr * 1000)).arg(enabled);
    if (QPixmap *cached = m_coverLayers.object(key))
        return *cached;
    auto *layer = new QPixmap(qCeil(side * dpr), qCeil(side * dpr));
    layer->setDevicePixelRatio(dpr);
    layer->fill(Qt::transparent);
    QPainter painter(layer);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(0, 0, side, side);
    QPainterPath clip;
    clip.addRoundedRect(bounds, 12, 12);
    painter.setClipPath(clip);
    painter.drawPixmap(bounds.topLeft(), icon.pixmap(QSize(side, side), dpr,
                        enabled ? QIcon::Normal : QIcon::Disabled));
    painter.setClipping(false);
    painter.setPen(QPen(QColor(255, 255, 255, 90), 0.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    painter.end();
    m_coverLayers.insert(key, layer);
    return *layer;
}

const QStaticText &SongGlassDelegate::textLayout(const QModelIndex &index, const QFont &font, int width) const
{
    const QString source = index.data(Qt::DisplayRole).toString();
    if (TextLayout *cached = m_textLayouts.object(index))
        if (cached->source == source && cached->font == font && cached->width == width)
            return cached->text;
    auto *layout = new TextLayout;
    layout->source = source;
    layout->font = font;
    layout->width = width;
    layout->text.setTextFormat(Qt::PlainText);
    layout->text.setText(QFontMetrics(font).elidedText(source, Qt::ElideRight, qMax(0, width)));
    layout->text.setPerformanceHint(QStaticText::AggressiveCaching);
    layout->text.prepare(QTransform(), font);
    m_textLayouts.insert(index, layout);
    return layout->text;
}

bool SongGlassDelegate::scrollMotionActive() const
{
    return m_motionTimer.isActive();
}

bool SongGlassDelegate::pressMotionActive() const
{
    return m_pointerHeld || m_pressAnimation->state() == QAbstractAnimation::Running;
}

void SongGlassDelegate::animatePress(qreal target, bool releasing)
{
    m_pressAnimation->stop();
    m_pressAnimation->setStartValue(m_pressDepth);
    m_pressAnimation->setEndValue(target);
    m_pressAnimation->setDuration(releasing ? 460 : 110);
    m_pressAnimation->setEasingCurve(releasing ? QEasingCurve::OutElastic : QEasingCurve::OutCubic);
    m_pressAnimation->start();
}

bool SongGlassDelegate::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_table || !dynamic_cast<QTableWidget *>(static_cast<QWidget *>(m_table)))
        return false;
    if (watched != m_viewport)
        return QStyledItemDelegate::eventFilter(watched, event);

    if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const int oldHover = m_hoveredRow;
        m_pointer = mouse->position();
        m_hoveredRow = m_table->viewport()->rect().contains(m_pointer.toPoint())
            ? m_table->rowAt(qRound(m_pointer.y())) : -1;
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton
            && m_hoveredRow >= 0) {
            const int oldPress = m_pressedRow;
            m_pressedRow = m_hoveredRow;
            m_pointerHeld = true;
            m_pressDepth = 0;
            animatePress(1, false);
            if (oldPress != m_pressedRow)
                updateRow(oldPress);
        } else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton
                   && m_pointerHeld) {
            m_pointerHeld = false;
            animatePress(0, true);
        }
        if (oldHover != m_hoveredRow)
            updateRow(oldHover);
        updateRow(m_hoveredRow);
    } else if (event->type() == QEvent::Leave) {
        const int oldHover = m_hoveredRow;
        m_hoveredRow = -1;
        updateRow(oldHover);
    } else if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate) {
        const int oldHover = m_hoveredRow;
        m_hoveredRow = -1;
        updateRow(oldHover);
        if (m_pointerHeld) {
            m_pointerHeld = false;
            animatePress(0, true);
        }
    }
    // Selection, keyboard navigation and hit testing always remain the view's
    // responsibility. A visual compression never consumes the input event.
    return false;
}

void SongGlassDelegate::beginScrollMotion(int value)
{
    if (!m_table || !dynamic_cast<QTableWidget *>(static_cast<QWidget *>(m_table)))
        return;
    const int delta = value - m_previousScroll;
    m_previousScroll = value;
    if (!delta)
        return;
    if (m_hoveredRow >= 0)
        m_hoveredRow = m_table->rowAt(qRound(m_pointer.y()));
    // A scrollbar jump or a freshly rebuilt large library must never create a
    // large deformation. Small wheel scrolls use the same bounded animation.
    m_scrollImpulse = qBound(-1.0, delta / 80.0, 1.0);
    m_motionAge = 0;
    m_motionEnvelope = 1;
    m_motionClock.restart();
    m_motionTimer.setInterval(Aero::animationInterval(m_table));
    m_motionTimer.start();
    m_table->viewport()->update();
}

void SongGlassDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    if (!m_table || !dynamic_cast<QTableWidget *>(static_cast<QWidget *>(m_table))
        || !index.isValid() || m_table->columnCount() < 1)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setClipRect(option.rect, Qt::IntersectClip);

    const int row = index.row();
    const QRect first = m_table->visualRect(index.siblingAtColumn(0));
    const QRect last = m_table->visualRect(index.siblingAtColumn(m_table->columnCount() - 1));
    const QRectF bounds = QRectF(first.united(last)).adjusted(7, 5, -7, -5);
    // The same row-dependent phase is used for every column, so there is no
    // seam where the table invokes paint separately for adjoining cells.
    const qreal phase = row * 0.73 + m_motionAge * 23.0;
    const qreal strength = m_scrollImpulse * m_motionEnvelope;
    const QPointF displacement(std::sin(phase) * strength * 4.0,
                               std::sin(phase + 0.4) * strength * 2.0);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = m_hoveredRow >= 0 && m_table->rowAt(qRound(m_pointer.y())) == row;
    const qreal pointerX = hovered
        ? qBound(-1.0, (m_pointer.x() - bounds.center().x()) / qMax(1.0, bounds.width() / 2), 1.0) : 0;
    const qreal pointerY = hovered
        ? qBound(-1.0, (m_pointer.y() - bounds.center().y()) / qMax(1.0, bounds.height() / 2), 1.0) : 0;
    const qreal press = m_pressedRow == row ? m_pressDepth : 0;
    const qreal squash = std::sin(phase) * strength * 0.021;
    QTransform deformation;
    deformation.translate(bounds.center().x() + displacement.x(),
                          bounds.center().y() + displacement.y());
    deformation.shear(std::cos(phase) * strength * 0.010 + pointerY * 0.003,
                      pointerX * 0.0007);
    deformation.scale(1.0 + squash * 0.13 + press * 0.0025,
                      1.0 - squash - press * 0.038);
    deformation.translate(-bounds.center().x(), -bounds.center().y());
    painter->setTransform(deformation, true);

    // Sample only the analytic backdrop surface, never the table's own painted
    // contents. A shared row transform keeps the four column clips seamless.
    Liquid::paintSurfaceBackdrop(*painter, m_table->viewport(), bounds, 18,
                                 QPointF(pointerX * 2.8 - displacement.x() * 0.45,
                                         pointerY * 1.5 - displacement.y() * 0.45));
    const qreal dpr = painter->device()->devicePixelRatioF();
    painter->drawPixmap(bounds.topLeft() - QPointF(6, 6), glassLayer(bounds.size(), dpr, selected));
    if (hovered) {
        QPainterPath silhouette;
        silhouette.addRoundedRect(bounds, 18, 18);
        const QPointF light = bounds.center()
            + QPointF(pointerX * bounds.width() * 0.30, pointerY * bounds.height() * 0.25);
        QRadialGradient reflection(light, qMax(70.0, bounds.width() * 0.38));
        reflection.setColorAt(0, QColor(255, 255, 255, 19));
        reflection.setColorAt(1, QColor(255, 255, 255, 0));
        painter->fillPath(silhouette, reflection);
        if (!selected) {
            painter->setPen(QPen(QColor(239, 248, 255, 100), 0.9));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 17, 17);
        }
    }

    QRectF content = QRectF(option.rect);
    if (index.column() == 0) {
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const int side = qMin(54, qMin(int(content.width()) - 18, int(content.height()) - 16));
            if (side <= 0) {
                painter->restore();
                return;
            }
            const QRectF cover(content.center().x() - side / 2.0,
                               content.center().y() - side / 2.0, side, side);
            painter->drawPixmap(cover.topLeft(), coverLayer(icon, side, dpr,
                                option.state.testFlag(QStyle::State_Enabled)));
        }
    } else {
        content.adjust(12, 7, -14, -7);
        QFont font = option.font;
        if (index.column() == 1)
            font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        QColor ink = index.column() == 1 ? QColor(QStringLiteral("#eef4fa")) : QColor(QStringLiteral("#939eae"));
        if (!option.state.testFlag(QStyle::State_Enabled))
            ink.setAlpha(145);
        painter->setPen(ink);
        const QStaticText &text = textLayout(index, font, int(content.width()));
        painter->drawStaticText(QPointF(content.left(), content.center().y() - text.size().height() / 2), text);
    }

    painter->restore();
}
