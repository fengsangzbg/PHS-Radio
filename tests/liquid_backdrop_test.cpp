#include <QtWidgets>
#define private public
#include "../aero_surface.h"
#undef private
#include "../liquid_backdrop.h"

#include <algorithm>
#include <cstdlib>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

void waitForEvents(int duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

class Pattern final : public QWidget {
public:
    using QWidget::QWidget;
    int paints = 0;
    bool showPatch = true;
protected:
    void paintEvent(QPaintEvent *) override
    {
        ++paints;
        QPainter painter(this);
        painter.fillRect(rect(), QColor(25, 40, 180));
        painter.fillRect(QRect(0, 0, 120, height()), QColor(180, 30, 20));
        if (showPatch)
            painter.fillRect(QRect(250, 70, 80, 80), QColor(15, 180, 25));
    }
};

class Overlay final : public QWidget {
public:
    using QWidget::QWidget;
    int paints = 0;
protected:
    void paintEvent(QPaintEvent *) override
    {
        ++paints;
        QPainter painter(this);
        painter.fillRect(rect(), QColor(255, 255, 255));
    }
};

QImage backdrop(QWidget &panel)
{
    QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    Liquid::paintBackdrop(painter, &panel, 12);
    return image;
}

QImage water(QWidget &viewport, QRectF bounds, QPointF offset = {})
{
    QImage image(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    Liquid::paintSurfaceBackdrop(painter, &viewport, bounds, 0, offset);
    return image;
}

void verifyBackdropSampling()
{
    AeroSurface host;
    host.resize(520, 360);
    Pattern content(&host);
    content.setGeometry(30, 40, 380, 260);
    QWidget panel(&host);
    panel.setObjectName("playbackDock");
    panel.setGeometry(120, 50, 160, 140);
    Overlay drawer(&host);
    drawer.setObjectName("playlistDrawerPanel");
    drawer.setGeometry(panel.geometry());
    Overlay edge(&host);
    edge.setObjectName("drawerEdgeHandle");
    edge.setGeometry(panel.geometry());
    Overlay tagged(&host);
    tagged.setProperty("liquidOverlay", true);
    tagged.setGeometry(panel.geometry());
    host.show();
    host.activateWindow();
    waitForEvents(30);
    host.m_motionTimer->stop();
    content.paints = drawer.paints = edge.paints = tagged.paints = 0;

    const QImage image = backdrop(panel);
    check(content.paints == 1, "An overlay must capture its nearby application content once.");
    check(drawer.paints == 0 && edge.paints == 0 && tagged.paints == 0,
          "Sampling must exclude floating overlays and avoid recursive capture.");
    const QColor left = image.pixelColor(12, 65);
    const QColor right = image.pixelColor(95, 65);
    check(left.red() > left.blue() + 60 && right.blue() > right.red() + 80,
          "A nonzero child origin and clipped source region must preserve underlying coordinates.");
    const QColor transition = image.pixelColor(30, 65);
    check(transition.red() > 40 && transition.blue() > 40,
          "The sampled application content must be blurred across its sharp color boundary.");
    check(image.pixelColor(0, 0).alpha() == 0 && image.pixelColor(95, 65).alpha() > 240,
          "The blur must respect a rounded transparent edge while retaining real background content.");
    backdrop(panel);
    check(content.paints == 1, "Repeated paints must reuse unchanged blurred content.");

    panel.move(270, 110);
    const QImage moved = backdrop(panel);
    check(content.paints == 2, "Moving beyond the cached capture must refresh its source region.");
    const QColor patch = moved.pixelColor(50, 40);
    check(patch.green() > patch.red() + 70 && patch.green() > patch.blue() + 70,
          "Moving a panel must sample its new background rather than dragging the old texture.");
    host.resize(560, 380);
    backdrop(panel);
    check(content.paints == 3, "Resizing the surface must invalidate the cached backdrop.");
    host.setAccentColor(QColor(225, 100, 160));
    backdrop(panel);
    check(content.paints == 4, "Changing the surface tint must invalidate the cached backdrop.");
    content.showPatch = false;
    Liquid::invalidateBackdrop(&host);
    const QColor updated = backdrop(panel).pixelColor(50, 40);
    check(content.paints == 5 && updated.blue() > updated.green() + 70,
          "Explicit content invalidation must refresh custom content even without a model signal.");
    host.m_phase += 1;
    backdrop(panel);
    check(content.paints == 5,
          "Advancing animated water must not recapture unchanged table or content widgets.");
}

void verifyAnalyticWorldCoordinates()
{
    QWidget plain;
    plain.resize(180, 120);
    const QImage unsupported = water(plain, plain.rect());
    check(unsupported.pixelColor(80, 60).alpha() == 0,
          "A reusable delegate without a water surface must retain its transparent background.");

    AeroSurface host;
    host.resize(500, 330);
    Pattern content(&host);
    content.setGeometry(host.rect());
    QWidget viewport(&host);
    viewport.setGeometry(110, 75, 180, 120);
    QWidget nested(&viewport);
    nested.setGeometry(25, 20, 90, 65);
    host.show();
    waitForEvents(20);
    host.m_motionTimer->stop();
    content.paints = 0;
    QImage whole(host.size(), QImage::Format_ARGB32_Premultiplied);
    whole.fill(Qt::transparent);
    {
        QPainter painter(&whole);
        host.paintWater(painter, host.rect());
    }
    const QImage local = water(viewport, viewport.rect());
    const QImage inner = water(nested, nested.rect());
    check(local.pixelColor(80, 50) == whole.pixelColor(190, 125)
              && inner.pixelColor(40, 30) == whole.pixelColor(175, 125),
          "Every viewport and nested row must sample a continuous world-space water field.");
    const QImage refracted = water(viewport, viewport.rect(), QPointF(7, 4));
    check(refracted.pixelColor(80, 50) == whole.pixelColor(197, 129),
          "Micro-refraction must shift only the sampled background coordinates.");
    check(content.paints == 0, "Painting song water must never recapture or render a table/content widget.");
    const QColor dark = local.pixelColor(80, 50);
    check(qMax(dark.red(), qMax(dark.green(), dark.blue())) < 60,
          "The water background must remain dark rather than fill with blue-white plastic.");
    const QImage unchanged = water(viewport, viewport.rect());
    check(unchanged == local, "A frozen water frame must be deterministic and reusable across cell paints.");
    host.m_phase += 3;
    const QImage flowing = water(viewport, viewport.rect());
    check(flowing != local, "Advancing the water phase must change the soft reflections.");
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&host, &deactivate);
    check(!host.animationRunning(), "An inactive window must stop its water timer.");
    host.activateWindow();
    QEvent activate(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&host, &activate);
    check(host.animationRunning(), "Returning to an active visible window must restart the water timer.");
    const qreal phase = host.m_phase;
    waitForEvents(80);
    check(host.m_phase > phase, "The active water timer must actually advance its reflections.");
    host.hide();
    check(!host.animationRunning(), "A hidden surface must stop its water timer.");
}

class CountingDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *viewport = nullptr;
    mutable QSet<int> rows;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        rows.insert(index.row());
        Liquid::paintSurfaceBackdrop(*painter, viewport, option.rect, 10);
    }
};

void verifyLargeLibraryVisibleCapture()
{
    AeroSurface host;
    host.resize(700, 440);
    QTableWidget table(&host);
    table.setGeometry(15, 20, 660, 395);
    table.setColumnCount(4);
    table.setRowCount(12043);
    table.verticalHeader()->setDefaultSectionSize(54);
    table.setStyleSheet("QTableWidget { background: transparent; }");
    auto *delegate = new CountingDelegate(&table);
    delegate->viewport = table.viewport();
    table.setItemDelegate(delegate);
    QWidget panel(&host);
    panel.setObjectName("playbackDock");
    panel.setGeometry(60, 275, 540, 145);
    host.show();
    waitForEvents(20);
    host.m_motionTimer->stop();
    delegate->rows.clear();
    const QImage image = backdrop(panel);
    check(!image.isNull() && !delegate->rows.isEmpty(),
          "The blurred panel must include visible song cards underneath it.");
    check(delegate->rows.size() < 20 && *std::max_element(delegate->rows.begin(), delegate->rows.end()) < 20,
          "Capturing a 12043-song library must paint its visible region rather than scan all songs.");
    delegate->rows.clear();
    table.setItem(5, 1, new QTableWidgetItem(QStringLiteral("New metadata")));
    backdrop(panel);
    check(!delegate->rows.isEmpty(), "Changing model data must refresh the real blurred song content.");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    verifyBackdropSampling();
    verifyAnalyticWorldCoordinates();
    verifyLargeLibraryVisibleCapture();
    qInfo("Liquid backdrop tests passed.");
    return 0;
}
