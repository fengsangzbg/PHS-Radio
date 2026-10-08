#include "song_glass_delegate.h"

#include "aero_widgets.h"
#include "liquid_backdrop.h"
#include "smooth_scroll.h"

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
#include <algorithm>
#include <cmath>

SongGlassDelegate::SongGlassDelegate(QTableWidget *table)
    : QStyledItemDelegate(table), m_table(table), m_viewport(table->viewport()), m_accent(Aero::defaultAccent()),
      m_pressAnimation(new QVariantAnimation(this))
{
    table->setMouseTracking(true);
    table->viewport()->installEventFilter(this);
    m_scrollController = SmoothScrollController::attach(table);
    m_motionClock.start();
    const auto detach = [this] {
        // A QTableWidget destroys its model before QObject deletes child
        // widgets. Stop observing the viewport before those teardown events.
        if (m_viewport)
            m_viewport->removeEventFilter(this);
        m_table = nullptr;
        m_motionTimer.stop();
        m_scrollAnimating = false;
        m_pressAnimation->stop();
    };
    connect(table->model(), &QObject::destroyed, this, detach);
    connect(table, &QObject::destroyed, this, detach);
    connect(table->model(), &QAbstractItemModel::modelAboutToBeReset,
            this, [this] { stopScrollMotion(); });
    connect(table->model(), &QAbstractItemModel::modelReset, this, [this] {
        stopScrollMotion();
        m_textLayouts.clear();
        if (m_table)
            m_previousScroll = m_table->verticalScrollBar()->value();
    });
    m_previousScroll = table->verticalScrollBar()->value();
    m_motionTimer.setTimerType(Qt::PreciseTimer);
    m_motionTimer.setSingleShot(true);
    m_motionSchedule.setRefreshRate(Aero::displayRefreshRate(table), m_motionClock.nsecsElapsed());
    m_refreshObserver = Aero::observeDisplayRefresh(table, this, [this] {
        if (!m_table)
            return;
        const qint64 now = m_motionClock.nsecsElapsed();
        m_motionSchedule.setRefreshRate(Aero::displayRefreshRate(m_table), now);
        scheduleScrollMotion();
    });
    connect(table->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) { beginScrollMotion(value); });
    connect(&m_motionTimer, &QTimer::timeout, this, [this] {
        if (!m_table || !m_table->isVisible()) {
            stopScrollMotion();
            return;
        }
        if (m_scrollController && m_scrollController->isAnimating()) {
            m_motionTimer.stop();
            return;
        }
        const qint64 now = m_motionClock.nsecsElapsed();
        m_motionSchedule.setRefreshRate(Aero::displayRefreshRate(m_table), now);
        if (!m_motionSchedule.advance(now)) {
            m_motionTimer.start(m_motionSchedule.delayMs(now));
            return;
        }
        advanceScrollMotion();
        scheduleScrollMotion();
    });
    m_scrollController->observeFrames(this, [this](bool continuing, bool cancelled) {
        if (cancelled) {
            const bool wasMoving = m_scrollAnimating;
            stopScrollMotion();
            if (wasMoving && m_table && m_table->isVisible())
                m_table->viewport()->update(m_table->viewport()->visibleRegion());
            return;
        }
        if (!m_table || !m_scrollAnimating)
            return;
        // Scroll position and jelly state reach Qt's backing store in the same
        // event-loop turn, instead of painting again on a separate phase.
        advanceScrollMotion();
        if (!continuing) {
            m_motionSchedule.reset(m_motionClock.nsecsElapsed());
            scheduleScrollMotion();
        }
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
    delete m_refreshObserver;
    m_refreshObserver = nullptr;
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
    // Typed keys avoid constructing several temporary strings per visible cell.
    // Accent changes clear the cache, so its color need not be stored in the key.
    const GlassKey key{qRound(size.width() * 16), qRound(size.height() * 16),
                       qRound(dpr * 1000), selected};
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
    const CoverKey key{icon.cacheKey(), side, qRound(dpr * 1000), enabled};
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

const QPixmap &SongGlassDelegate::textLayer(const QModelIndex &index, const QFont &font,
                                           int width, qreal dpr, const QColor &ink) const
{
    textLayout(index, font, width);
    TextLayout *layout = m_textLayouts.object(index);
    if (layout->ink != ink) {
        layout->densityPixels = {};
        layout->densityClock = 0;
        layout->ink = ink;
    }
    for (auto &density : layout->densityPixels) {
        if (!density.pixels.isNull() && density.dpr == dpr) {
            density.lastUsed = ++layout->densityClock;
            return density.pixels;
        }
    }
    auto *density = &layout->densityPixels.front();
    for (auto &candidate : layout->densityPixels) {
        if (candidate.pixels.isNull()) {
            density = &candidate;
            break;
        }
        if (candidate.lastUsed < density->lastUsed)
            density = &candidate;
    }
    // Rasterize at the actual device density once. Applying a new shear/scale
    // to QStaticText on Qt's raster engine recalculates text layout every frame.
    const QSizeF size = layout->text.size();
    density->pixels = QPixmap(qMax(1, qCeil(size.width() * dpr)),
                              qMax(1, qCeil(size.height() * dpr)));
    density->pixels.setDevicePixelRatio(dpr);
    density->pixels.fill(Qt::transparent);
    QPainter painter(&density->pixels);
    painter.setFont(font);
    painter.setPen(ink);
    painter.drawStaticText(QPointF(), layout->text);
    painter.end();
    density->dpr = dpr;
    density->lastUsed = ++layout->densityClock;
    return density->pixels;
}

void SongGlassDelegate::advanceScrollMotion()
{
    if (!m_table || !m_scrollAnimating)
        return;
    const qint64 now = m_motionClock.nsecsElapsed();
    const qreal dt = std::max<qreal>(0, (now - m_previousMotionFrameNs) / 1000000000.0);
    m_previousMotionFrameNs = now;
    m_motionAge = now / 1000000000.0;
    const qreal inputAge = (now - m_previousScrollNs) / 1000000000.0;
    const qreal target = inputAge < .045 ? m_scrollTarget : 0;
    const qreal response = target == 0 ? 18.0 : 28.0;
    m_scrollImpulse += (target - m_scrollImpulse) * (1.0 - std::exp(-response * dt));
    if ((inputAge > .22 && std::abs(m_scrollImpulse) < .008) || inputAge > .38)
        stopScrollMotion();
    m_table->viewport()->update(m_table->viewport()->visibleRegion());
}

void SongGlassDelegate::scheduleScrollMotion()
{
    if (m_scrollController && m_scrollController->isAnimating()) {
        m_motionTimer.stop();
        return;
    }
    if (m_scrollAnimating)
        m_motionTimer.start(m_motionSchedule.delayMs(m_motionClock.nsecsElapsed()));
}

bool SongGlassDelegate::scrollMotionActive() const
{
    return m_scrollAnimating;
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
        stopScrollMotion();
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
    if (!m_table->isVisible()) {
        stopScrollMotion();
        return;
    }
    if (!delta)
        return;
    if (m_scrollController && m_scrollController->isAnimating())
        m_motionTimer.stop();
    if (m_hoveredRow >= 0)
        m_hoveredRow = m_table->rowAt(qRound(m_pointer.y()));
    // Scrollbar changes drive a bounded velocity response. The phase clock is
    // continuous: retargeting each wheel/touchpad tick never restarts the wave.
    const qint64 now = m_motionClock.nsecsElapsed();
    const bool fresh = !m_scrollAnimating || now - m_previousScrollNs > 80000000;
    const qreal dt = fresh ? Aero::animationInterval(m_table) / 1000.0
        : std::max<qreal>(.001, (now - m_previousScrollNs) / 1000000000.0);
    m_scrollTarget = qBound(-1.0, delta / dt / 1600.0, 1.0);
    m_previousScrollNs = now;
    m_motionAge = now / 1000000000.0;
    m_motionEnvelope = 1;
    if (!m_scrollAnimating) {
        m_scrollAnimating = true;
        m_previousMotionFrameNs = now;
        m_motionSchedule.setRefreshRate(Aero::displayRefreshRate(m_table), now);
        m_motionSchedule.reset(now);
        scheduleScrollMotion();
        // Native scrolling can reuse/move old viewport pixels. Repaint the
        // initially visible cards once so their sampled wallpaper stays fixed
        // in world coordinates; subsequent motion is paced by the frame budget.
        if (!m_scrollController || !m_scrollController->isAnimating())
            m_table->viewport()->update(m_table->viewport()->visibleRegion());
    }
    // No row/model scan: only the visible viewport participates in the motion.
}

void SongGlassDelegate::stopScrollMotion()
{
    m_motionTimer.stop();
    m_scrollAnimating = false;
    m_scrollTarget = 0;
    m_scrollImpulse = 0;
    m_motionEnvelope = 0;
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
    // Sample only the analytic backdrop surface, never the table's own painted
    // contents. A shared row transform keeps the four column clips seamless.
    Liquid::paintSurfaceBackdrop(*painter, m_table->viewport(), bounds, 18,
                                 QPointF(pointerX * 2.8 - displacement.x() * 0.45,
                                         pointerY * 1.5 - displacement.y() * 0.45), deformation);
    painter->setTransform(deformation, true);
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
        QColor ink = index.column() == 1 ? QColor(QStringLiteral("#eef4fa")) : QColor(QStringLiteral("#939eae"));
        if (!option.state.testFlag(QStyle::State_Enabled))
            ink.setAlpha(145);
        const QPixmap &text = textLayer(index, font, int(content.width()), dpr, ink);
        painter->drawPixmap(QPointF(content.left(), content.center().y()
                            - text.deviceIndependentSize().height() / 2), text);
    }

    painter->restore();
}
