#include "liquid_backdrop.h"
#include "aero_surface.h"

#include <QCache>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QMap>
#include <QPointer>
#include <QRegion>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
AeroSurface *surfaceFor(QWidget *widget)
{
    for (QWidget *ancestor = widget ? widget->parentWidget() : nullptr;
         ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *surface = dynamic_cast<AeroSurface *>(ancestor))
            return surface;
    }
    return nullptr;
}

QImage blurPass(const QImage &input, int radius, bool horizontal)
{
    QImage result(input.size(), QImage::Format_ARGB32_Premultiplied);
    const int lines = horizontal ? input.height() : input.width();
    const int length = horizontal ? input.width() : input.height();
    const int divisor = radius * 2 + 1;
    for (int line = 0; line < lines; ++line) {
        const auto pixel = [&](int position) {
            position = std::clamp(position, 0, length - 1);
            const int x = horizontal ? position : line;
            const int y = horizontal ? line : position;
            return reinterpret_cast<const QRgb *>(input.constScanLine(y))[x];
        };
        int red = 0, green = 0, blue = 0, alpha = 0;
        const auto add = [&](QRgb color, int sign) {
            red += sign * qRed(color);
            green += sign * qGreen(color);
            blue += sign * qBlue(color);
            alpha += sign * qAlpha(color);
        };
        for (int position = -radius; position <= radius; ++position)
            add(pixel(position), 1);
        for (int position = 0; position < length; ++position) {
            const int x = horizontal ? position : line;
            const int y = horizontal ? line : position;
            reinterpret_cast<QRgb *>(result.scanLine(y))[x] =
                qRgba(red / divisor, green / divisor, blue / divisor, alpha / divisor);
            add(pixel(position - radius), -1);
            add(pixel(position + radius + 1), 1);
        }
    }
    return result;
}

struct Backdrop {
    QPointer<QWidget> panel;
    QPointer<AeroSurface> surface;
    QSize surfaceSize;
    quint64 revision = 0;
    quint64 contentRevision = 0;
    QRect capture;
    QImage blurred;
    QElapsedTimer age;
};

bool excluded(QWidget *child, QWidget *panel);

// A backdrop is a blurred content layer, not a new screenshot every animation
// frame. Models, scrollbars and content events invalidate it; water Paint events
// deliberately do not. No tracks or item-model rows are walked here.
class ContentObserver final : public QObject {
public:
    explicit ContentObserver(AeroSurface *surface) : QObject(surface), surface(surface)
    {
        clock.start();
    }
    QPointer<AeroSurface> surface;
    quint64 revision = 0;
    QElapsedTimer clock;
    qint64 movingUntil = 0;
    QSet<QObject *> observed;

    bool moving() const { return clock.elapsed() < movingUntil; }
    void changed(bool motion = false)
    {
        ++revision;
        if (motion)
            movingUntil = clock.elapsed() + 600;
    }
    void observeContent()
    {
        for (QObject *object : surface->children()) {
            auto *child = qobject_cast<QWidget *>(object);
            if (!child || excluded(child, nullptr) || observed.contains(child))
                continue;
            observe(child);
            for (QWidget *descendant : child->findChildren<QWidget *>())
                observe(descendant);
            if (auto *area = qobject_cast<QAbstractScrollArea *>(child)) {
                connect(area->verticalScrollBar(), &QScrollBar::valueChanged,
                        this, [this] { changed(true); });
                connect(area->horizontalScrollBar(), &QScrollBar::valueChanged,
                        this, [this] { changed(true); });
            }
            if (auto *view = qobject_cast<QAbstractItemView *>(child)) {
                QAbstractItemModel *model = view->model();
                connect(model, &QAbstractItemModel::dataChanged, this, [this] { changed(); });
                connect(model, &QAbstractItemModel::rowsInserted, this, [this] { changed(); });
                connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { changed(); });
                connect(model, &QAbstractItemModel::columnsInserted, this, [this] { changed(); });
                connect(model, &QAbstractItemModel::columnsRemoved, this, [this] { changed(); });
                connect(model, &QAbstractItemModel::modelReset, this, [this] { changed(); });
                connect(model, &QAbstractItemModel::layoutChanged, this, [this] { changed(); });
                connect(view->selectionModel(), &QItemSelectionModel::selectionChanged,
                        this, [this] { changed(); });
            }
            changed();
        }
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Wheel:
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::KeyPress:
        case QEvent::Enter:
        case QEvent::Leave:
            changed(true);
            break;
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::DynamicPropertyChange:
        case QEvent::FontChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
        case QEvent::LayoutRequest:
            changed();
            break;
        default:
            break;
        }
        return false;
    }
private:
    void observe(QWidget *widget)
    {
        if (observed.contains(widget))
            return;
        observed.insert(widget);
        widget->installEventFilter(this);
        connect(widget, &QObject::destroyed, this, [this, widget] {
            observed.remove(widget);
            changed();
        });
    }
};

struct Backdrops {
    QCache<QWidget *, Backdrop> images{16};
    QMap<AeroSurface *, QPointer<ContentObserver>> observers;
    bool sampling = false;
    QElapsedTimer fluidClock;
    Backdrops() { fluidClock.start(); }
};

Backdrops &backdrops()
{
    // Widgets paint on the GUI thread; bounded, guarded entries survive panels
    // being deleted or addresses being reused without retaining any widgets.
    static thread_local Backdrops state;
    return state;
}

ContentObserver *observerFor(AeroSurface *surface, Backdrops &state)
{
    auto &observer = state.observers[surface];
    if (!observer) {
        observer = new ContentObserver(surface);
        QObject::connect(surface, &QObject::destroyed, observer, [surface, &state] {
            state.observers.remove(surface);
        });
    }
    observer->observeContent();
    return observer;
}

bool excluded(QWidget *child, QWidget *panel)
{
    return child == panel || (panel && child->isAncestorOf(panel)) || child->isWindow()
           || child->property("liquidOverlay").toBool()
           || dynamic_cast<AeroPanel *>(child)
           || child->objectName() == "playlistDrawerPanel"
           || child->objectName() == "playbackDock"
           || child->objectName() == "drawerEdgeHandle";
}

Backdrop *sample(QWidget *panel, AeroSurface *surface, const QRect &visiblePanel,
                 Backdrops &state)
{
    ContentObserver *observer = observerFor(surface, state);
    Backdrop *cached = state.images.object(panel);
    if (cached && cached->panel == panel && cached->surface == surface
        && cached->surfaceSize == surface->size()
        && cached->revision == surface->backdropRevision()
        && cached->capture.contains(visiblePanel)
        && ((cached->contentRevision == observer->revision
             && (!observer->moving() || cached->age.elapsed() < 150))
            || (observer->moving() && cached->age.elapsed() < 75)))
        return cached;

    auto replacement = std::make_unique<Backdrop>();
    replacement->panel = panel;
    replacement->surface = surface;
    replacement->surfaceSize = surface->size();
    replacement->revision = surface->backdropRevision();
    replacement->contentRevision = observer->revision;
    replacement->capture = visiblePanel.adjusted(-24, -24, 24, 24).intersected(surface->rect());
    if (replacement->capture.isEmpty())
        return nullptr;
    constexpr qreal scale = 1.0 / 3.0;
    QImage image(QSize(std::max(1, int(std::ceil(replacement->capture.width() * scale))),
                       std::max(1, int(std::ceil(replacement->capture.height() * scale)))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QScopedValueRollback<bool> sampling(state.sampling, true);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        painter.translate(-replacement->capture.topLeft());
        // Render only direct content widgets and the intersection underneath the
        // panel. Qt clips item views to their visible viewport; no model scan.
        for (QObject *object : surface->children()) {
            auto *child = qobject_cast<QWidget *>(object);
            if (!child || !child->isVisible() || excluded(child, panel))
                continue;
            const QRect overlap = child->geometry().intersected(replacement->capture);
            if (overlap.isEmpty())
                continue;
            const QRegion region(overlap.translated(-child->pos()));
            child->render(&painter, overlap.topLeft(), region, QWidget::DrawChildren);
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        image = blurPass(image, 3, true);
        image = blurPass(image, 3, false);
    }
    replacement->blurred = std::move(image);
    replacement->age.start();
    Backdrop *result = replacement.release();
    state.images.insert(panel, result);
    return result;
}
} // namespace

namespace Liquid {
void paintSurfaceBackdrop(QPainter &painter, QWidget *viewport, const QRectF &bounds,
                          qreal radius, const QPointF &offset)
{
    // Capturing a transparent card/text layer must not bake animated, opaque
    // water into it. The current water is composed under that layer afterward.
    if (backdrops().sampling)
        return;
    AeroSurface *surface = surfaceFor(viewport);
    if (!surface || bounds.isEmpty())
        return;
    surface->registerBackgroundConsumer(viewport);
    QPainterPath clip;
    clip.addRoundedRect(bounds, radius, radius);
    painter.save();
    painter.setClipPath(clip, Qt::IntersectClip);
    const QPointF worldOrigin = viewport->mapTo(surface, QPoint(0, 0));
    surface->paintWater(painter, bounds, worldOrigin + offset);
    painter.restore();
}

void paintBackdrop(QPainter &painter, QWidget *panel, qreal radius)
{
    AeroSurface *surface = surfaceFor(panel);
    if (!surface || !panel || panel->size().isEmpty())
        return;
    surface->registerBackgroundConsumer(panel);
    Backdrops &state = backdrops();
    if (state.sampling) {
        return;
    }
    const QPoint origin = panel->mapTo(surface, QPoint(0, 0));
    const QRect worldRect(origin, panel->size());
    const QRect visiblePanel = worldRect.intersected(surface->rect());
    if (visiblePanel.isEmpty())
        return;
    Backdrop *cached = sample(panel, surface, visiblePanel, state);
    if (!cached)
        return;
    constexpr qreal scale = 1.0 / 3.0;
    const qreal time = state.fluidClock.elapsed() / 1000.0;
    const QPointF refraction(.75 * std::sin(time * .24), .55 * std::cos(time * .21));
    const QRectF source((QPointF(origin - cached->capture.topLeft()) + refraction) * scale,
                        QSizeF(panel->size()) * scale);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(panel->rect()).adjusted(2, 2, -2, -2), radius, radius);
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setClipPath(clip, Qt::IntersectClip);
    surface->paintBlurredBackground(painter, panel->rect(), QPointF(origin) + refraction);
    painter.drawImage(QRectF(panel->rect()), cached->blurred, source);
    painter.restore();
}

void invalidateBackdrop(QWidget *content)
{
    AeroSurface *surface = dynamic_cast<AeroSurface *>(content);
    if (!surface)
        surface = surfaceFor(content);
    if (surface)
        observerFor(surface, backdrops())->changed();
}
} // namespace Liquid
