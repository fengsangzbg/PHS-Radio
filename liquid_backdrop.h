#pragma once

#include <QPointF>
#include <QRectF>

class QPainter;
class QWidget;

namespace Liquid {

// Samples application content beneath an overlay, excluding other overlays.
// It does not capture the desktop or request system-level window translucency.
void paintBackdrop(QPainter &painter, QWidget *panel, qreal radius = 24);

// Custom content widgets can invalidate after changes not represented by a
// model, scrollbar or input event. Animated surface paints do not invalidate.
void invalidateBackdrop(QWidget *content);

// Analytic water only: safe inside a table delegate without rendering the table.
// offset is a small sampling displacement in surface coordinates.
void paintSurfaceBackdrop(QPainter &painter, QWidget *viewport, const QRectF &bounds,
                          qreal radius, const QPointF &offset = {});

} // namespace Liquid
