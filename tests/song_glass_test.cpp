#include <QtWidgets>
#include "../aero_widgets.h"
#include "../aero_surface.h"
#include "../liquid_backdrop.h"
#define private public
#include "../song_glass_delegate.h"
#include "../smooth_scroll.h"
#undef private

#include <cstdlib>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

bool until(const std::function<bool()> &predicate, int timeout = 1600)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout)
        waitForEvents(10);
    return predicate();
}

void wheel(QAbstractItemView &view, const QPoint &pixels, const QPoint &angle,
           Qt::ScrollPhase phase = Qt::NoScrollPhase)
{
    const QPointF position(view.viewport()->rect().center());
    QWheelEvent event(position, view.viewport()->mapToGlobal(position.toPoint()),
                      pixels, angle, Qt::NoButton, Qt::NoModifier, phase, false);
    QCoreApplication::sendEvent(view.viewport(), &event);
}

void verifySmoothScrolling()
{
    const int oldLines = QApplication::wheelScrollLines();
    QApplication::setWheelScrollLines(3);
    QTableWidget table(260, 2);
    table.resize(520, 370);
    table.verticalHeader()->setDefaultSectionSize(80);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setCurrentCell(3, 0);
    auto *controller = SmoothScrollController::attach(&table);
    check(controller == SmoothScrollController::attach(&table)
          && table.verticalScrollMode() == QAbstractItemView::ScrollPerPixel,
          "Attaching smooth scrolling twice must reuse one pixel-scroll controller.");
    table.show();
    QCoreApplication::processEvents();
    QScrollBar *bar = table.verticalScrollBar();
    bar->setValue(0);
    bar->setSingleStep(32);
    const int selected = table.currentRow();
    QSet<int> positions;
    const auto connection = QObject::connect(bar, &QScrollBar::valueChanged,
        [&positions](int value) { positions.insert(value); });
    wheel(table, {}, QPoint(0, -120));
    const int target = qRound(controller->targetValue());
    check(controller->isAnimating() && bar->value() == 0 && target == 96,
          "A wheel notch must schedule its native distance without jumping immediately to the target.");
    waitForEvents(75);
    check(bar->value() > 0 && bar->value() < target && positions.size() > 2,
          "A notch must visibly pass through intermediate positions over several rendered frames.");
    check(controller->velocity() > 0 && table.currentRow() == selected,
          "Scrolling inertia must retain positive velocity without changing the song selection.");
    const qint64 clockBefore = controller->m_clock.elapsed();
    wheel(table, {}, QPoint(0, -120));
    check(controller->m_clock.elapsed() >= clockBefore && qRound(controller->targetValue()) == 192,
          "Consecutive notches must accumulate a target without restarting the elapsed-time clock.");
    waitForEvents(90);
    const qreal lateVelocity = std::abs(controller->velocity());
    waitForEvents(80);
    check(std::abs(controller->velocity()) < lateVelocity,
          "Velocity must decay after input stops rather than continue a constant-rate scroll.");
    check(until([&] { return !controller->isAnimating(); }) && bar->value() == 192
          && controller->velocity() == 0,
          "The inertial scroll must settle at the accumulated target and stop its timer.");

    // Native navigation supersedes the wheel target, including callers that
    // deliberately suppress scrollbar signals while rebuilding the library.
    wheel(table, {}, QPoint(0, -120));
    waitForEvents(30);
    { QSignalBlocker blocked(bar); bar->setValue(77); }
    waitForEvents(45);
    check(!controller->isAnimating() && bar->value() == 77,
          "An external position change with signals blocked must not be pulled back by stale inertia.");
    wheel(table, {}, QPoint(0, -120));
    bar->setSliderDown(true);
    bar->setValue(115);
    bar->setSliderDown(false);
    waitForEvents(45);
    check(!controller->isAnimating() && bar->value() == 115,
          "Dragging a native scrollbar must cancel the wheel animation and retain the dragged position.");
    wheel(table, {}, QPoint(0, -120));
    QKeyEvent pageDown(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
    QCoreApplication::sendEvent(&table, &pageDown);
    waitForEvents(20);
    const int keyboardPosition = bar->value();
    waitForEvents(70);
    check(!controller->isAnimating() && bar->value() == keyboardPosition,
          "Keyboard navigation must retain its own destination after cancelling inertia.");
    wheel(table, {}, QPoint(0, -120));
    table.scrollTo(table.model()->index(180, 0), QAbstractItemView::PositionAtTop);
    const int navigated = bar->value();
    waitForEvents(45);
    check(!controller->isAnimating() && bar->value() == navigated,
          "Programmatic scrollTo must override an older wheel target.");

    bar->setValue(0);
    wheel(table, QPoint(0, -37), {}, Qt::ScrollUpdate);
    check(bar->value() == 37 && !controller->isAnimating(),
          "Touchpad pixel deltas must follow the finger directly without a second inertial spring.");
    wheel(table, QPoint(0, -18), {}, Qt::ScrollMomentum);
    check(bar->value() == 55 && !controller->isAnimating(),
          "Native touchpad momentum must preserve its exact pixel displacement.");
    bar->setValue(bar->maximum() - 30);
    wheel(table, {}, QPoint(0, -120));
    check(qRound(controller->targetValue()) == bar->maximum(),
          "A wheel destination must clamp to the current scroll range.");
    check(until([&] { return !controller->isAnimating(); }) && bar->value() == bar->maximum(),
          "Scrolling to the bottom must converge without overshooting the native boundary.");
    wheel(table, {}, QPoint(0, -120));
    check(!controller->isAnimating() && bar->value() == bar->maximum(),
          "Repeated input at a settled boundary must not leave a timer spinning.");
    bar->setValue(0);
    wheel(table, {}, QPoint(0, -120));
    table.hide();
    const int hiddenPosition = bar->value();
    waitForEvents(45);
    check(!controller->isAnimating() && bar->value() == hiddenPosition,
          "A hidden view must cancel scrolling and stop background updates.");
    table.show();
    QCoreApplication::processEvents();
    wheel(table, {}, QPoint(0, -120));
    table.clear();
    check(!controller->isAnimating(), "A model reset must cancel the previous scroll target.");
    wheel(table, {}, QPoint(0, -120));
    table.setRowCount(1);
    QCoreApplication::processEvents();
    check(!controller->isAnimating() && bar->value() <= bar->maximum(),
          "Shrinking the model/range must cancel inertia without restoring an obsolete position.");
    QObject::disconnect(connection);

    QListWidget playlists;
    playlists.resize(260, 300);
    for (int row = 0; row < 100; ++row)
        playlists.addItem(QStringLiteral("Playlist %1").arg(row));
    auto *listController = SmoothScrollController::attach(&playlists);
    playlists.show();
    QCoreApplication::processEvents();
    playlists.verticalScrollBar()->setSingleStep(24);
    wheel(playlists, {}, QPoint(0, -120));
    const int listTarget = qRound(listController->targetValue());
    check(listController->isAnimating() && until([&] { return !listController->isAnimating(); })
          && playlists.verticalScrollBar()->value() == listTarget,
          "The same controller must smoothly scroll the playlist QListWidget without row widgets.");
    QApplication::setWheelScrollLines(oldLines);
}

int colorDistance(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

QImage paintRow(QTableWidget &table, SongGlassDelegate &delegate, int row,
                const QColor &background = QColor(3, 7, 12))
{
    QImage image(table.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(background);
    QPainter painter(&image);
    QStyleOptionViewItem option;
    option.initFrom(table.viewport());
    option.font = table.font();
    option.state = QStyle::State_Enabled;
    for (int column = 0; column < table.columnCount(); ++column) {
        const QModelIndex index = table.model()->index(row, column);
        option.rect = table.visualRect(index);
        delegate.paint(&painter, option, index);
    }
    return image;
}

void verifyCardContinuity(QTableWidget &table, SongGlassDelegate &delegate)
{
    table.verticalScrollBar()->setValue(0);
    waitForEvents(520);
    QCursor::setPos(table.viewport()->mapToGlobal(QPoint(-100, -100)));
    const QModelIndex leftIndex = table.model()->index(0, 1);
    const QModelIndex rightIndex = table.model()->index(0, 2);
    const QRect left = table.visualRect(leftIndex);
    const QRect right = table.visualRect(rightIndex);

    const QImage image = paintRow(table, delegate, 0);

    const int x = right.left();
    const int y = left.top() + 14;
    check(colorDistance(image.pixelColor(x - 1, y), image.pixelColor(x, y)) < 12
              && colorDistance(image.pixelColor(x, y), image.pixelColor(x + 1, y)) < 12,
          "A song's glass card must remain continuous across separately painted table columns.");
    check(colorDistance(image.pixelColor(x, left.top() + 1), image.pixelColor(x, y)) > 4,
          "Each song card must leave a visible gap to the next card.");

    const QImage translucent = paintRow(table, delegate, 0, Qt::transparent);
    const int opacity = translucent.pixelColor(x + 20, y + 7).alpha();
    check(opacity > 0 && opacity < 230,
          "The song surface must remain translucent rather than becoming a solid plastic row.");
    const QColor darkBody = image.pixelColor(x + 20, y + 7);
    check(qMax(darkBody.red(), qMax(darkBody.green(), darkBody.blue())) < 100,
          "The unselected glass body must preserve the dark background rather than filling with pale color.");

    delegate.setAccentColor(QColor(240, 155, 195));
    const QImage recolored = paintRow(table, delegate, 0);
    check(image != recolored, "Changing the accent must update the subtle glass tint.");
}

void verifyPressFeedback(QTableWidget &table, SongGlassDelegate &delegate)
{
    table.verticalScrollBar()->setValue(0);
    waitForEvents(520);
    const QModelIndex target = table.model()->index(1, 1);
    const QRect hitTarget = table.visualRect(target);
    const QPointF local = hitTarget.center();
    const QPointF global = table.viewport()->mapToGlobal(local.toPoint());
    QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(table.viewport(), &move);
    const QImage relaxed = paintRow(table, delegate, target.row());
    QMouseEvent press(QEvent::MouseButtonPress, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(table.viewport(), &press);
    waitForEvents(150);
    check(delegate.pressMotionActive(), "Holding a song must produce visual compression feedback.");
    check(paintRow(table, delegate, target.row()) != relaxed,
          "Press feedback must visibly deform the glass rather than only changing internal state.");
    check(table.currentRow() == target.row() && table.visualRect(target) == hitTarget
              && table.indexAt(hitTarget.center()) == target,
          "Press feedback must preserve the song's selection and clickable identity.");
    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(table.viewport(), &release);
    check(delegate.pressMotionActive(), "Releasing a song must start a short elastic recovery.");
    waitForEvents(550);
    check(!delegate.pressMotionActive() && paintRow(table, delegate, target.row()) == relaxed,
          "The song must recover its original glass surface and stop animating after release.");
}

void verifyScrollMotion(QTableWidget &table, SongGlassDelegate &delegate)
{
    table.selectRow(5);
    const int selectedRow = table.currentRow();
    const int rowHeight = table.rowHeight(5);
    table.verticalScrollBar()->setValue(table.verticalScrollBar()->maximum());
    check(delegate.scrollMotionActive(), "Scrolling must begin a short surface animation.");
    QCoreApplication::processEvents();
    const QModelIndex visible = table.indexAt(QPoint(20, table.viewport()->height() - 20));
    const QRect originalHitTarget = table.visualRect(visible);
    waitForEvents(85);
    check(delegate.scrollMotionActive(), "Scroll motion must remain visible briefly after the scroll.");
    const QImage animated = paintRow(table, delegate, visible.row());
    const QRect boundaryCell = table.visualRect(table.model()->index(visible.row(), 2));
    const int seamX = boundaryCell.left();
    const int seamY = boundaryCell.top() + 16;
    check(colorDistance(animated.pixelColor(seamX - 1, seamY), animated.pixelColor(seamX, seamY)) < 12
              && colorDistance(animated.pixelColor(seamX, seamY), animated.pixelColor(seamX + 1, seamY)) < 12,
          "Elastic row deformation must remain continuous across column paint boundaries.");
    check(table.currentRow() == selectedRow && table.rowHeight(5) == rowHeight
              && table.visualRect(visible) == originalHitTarget
              && table.indexAt(originalHitTarget.center()) == visible,
          "Visual scroll feedback must preserve selection, row geometry, and clickable song identity.");
    waitForEvents(520);
    check(!delegate.scrollMotionActive(), "Scroll feedback must stop repainting once motion settles.");
}

void verifyContinuousJellyPhase(QTableWidget &table, SongGlassDelegate &delegate)
{
    QScrollBar *bar = table.verticalScrollBar();
    bar->setValue(0);
    const qreal phase = delegate.m_motionClock.nsecsElapsed() / 1000000000.0;
    for (int tick = 0; tick < 6; ++tick) {
        bar->setValue(bar->value() + 3);
        waitForEvents(20);
    }
    check(delegate.m_motionAge > phase + .07 && std::abs(delegate.m_scrollImpulse) <= 1,
          "Repeated scroll events must preserve a continuous wave phase and bounded velocity response.");
    waitForEvents(700);
    check(!delegate.scrollMotionActive() && delegate.m_scrollImpulse == 0
          && delegate.m_motionEnvelope == 0,
          "The velocity-driven jelly response must decay completely and stop repainting when input settles.");
    bar->setValue(bar->value() + 3);
    check(delegate.scrollMotionActive(), "New movement must resume the settled jelly response.");
    table.hide();
    check(!delegate.scrollMotionActive(), "Hidden song cards must stop their elastic motion timer.");
    table.show();
    QCoreApplication::processEvents();
}

void verifySharedScrollFrames()
{
    const int oldLines = QApplication::wheelScrollLines();
    QApplication::setWheelScrollLines(3);
    QTableWidget table(80, 4);
    table.resize(540, 350);
    table.verticalHeader()->setDefaultSectionSize(80);
    auto *delegate = new SongGlassDelegate(&table);
    table.setItemDelegate(delegate);
    auto *controller = SmoothScrollController::attach(&table);
    table.show();
    QCoreApplication::processEvents();
    QScrollBar *bar = table.verticalScrollBar();
    bar->setValue(0);
    bar->setSingleStep(32);
    delegate->stopScrollMotion();
    QObject observerContext;
    int sharedFrames = 0;
    int settledFrames = 0;
    int cancelledFrames = 0;
    int deadReceiverCalls = 0;
    qreal previousPhase = -1;
    bool phaseAdvanced = false;
    controller->observeFrames(&observerContext, [&](bool continuing, bool cancelled) {
        if (cancelled) {
            ++cancelledFrames;
            check(!delegate->m_motionTimer.isActive() && !delegate->scrollMotionActive(),
                  "Cancelling a shared scroll must stop jelly rather than schedule a recovery tail.");
        } else if (continuing) {
            ++sharedFrames;
            check(!delegate->m_motionTimer.isActive(),
                  "Jelly must share the scrolling tick without a second active viewport animation timer.");
            if (previousPhase >= 0 && delegate->m_motionAge > previousPhase)
                phaseAdvanced = true;
            previousPhase = delegate->m_motionAge;
        } else {
            ++settledFrames;
        }
    });
    auto *deadContext = new QObject;
    controller->observeFrames(deadContext, [&](bool, bool) { ++deadReceiverCalls; });
    delete deadContext;
    wheel(table, {}, QPoint(0, -120));
    check(until([&] { return !controller->isAnimating(); }) && bar->value() == 96,
          "Shared animation must retain the wheel's final position.");
    check(sharedFrames > 1 && phaseAdvanced && settledFrames == 1 && deadReceiverCalls == 0,
          "Shared ticks must advance jelly phase, report one settlement, and skip destroyed receivers.");
    check(until([&] { return !delegate->scrollMotionActive(); }, 450)
              && !delegate->m_motionTimer.isActive() && delegate->m_scrollImpulse == 0,
          "After scrolling settles, its short jelly tail must decay and stop repainting.");

    const auto startMoving = [&] {
        const int before = sharedFrames;
        wheel(table, {}, QPoint(0, -120));
        check(until([&] { return sharedFrames > before && delegate->scrollMotionActive(); }),
              "New wheel movement must start the shared jelly animation.");
    };
    startMoving();
    int cancellations = cancelledFrames;
    controller->cancel();
    waitForEvents(45);
    check(cancelledFrames == cancellations + 1 && !controller->isAnimating()
              && !delegate->scrollMotionActive() && !delegate->m_motionTimer.isActive(),
          "Explicit cancellation must not relaunch an independent jelly timer.");
    startMoving();
    cancellations = cancelledFrames;
    table.hide();
    waitForEvents(45);
    check(cancelledFrames == cancellations + 1 && !controller->isAnimating()
              && !delegate->m_motionTimer.isActive(),
          "Hiding a moving view must cancel the shared frame stream without a tail.");
    bar->setValue(qMin(bar->maximum(), bar->value() + 17));
    check(!controller->isAnimating() && !delegate->scrollMotionActive()
              && !delegate->m_motionTimer.isActive(),
          "An application scroll change after hiding must not restart a jelly timer for an invisible view.");
    waitForEvents(45);
    check(!delegate->scrollMotionActive() && !delegate->m_motionTimer.isActive(),
          "An invisible view must stay idle after a background scroll position change.");
    table.show();
    QCoreApplication::processEvents();
    startMoving();
    cancellations = cancelledFrames;
    table.clear();
    waitForEvents(45);
    check(cancelledFrames == cancellations + 1 && !controller->isAnimating()
              && !delegate->scrollMotionActive() && !delegate->m_motionTimer.isActive(),
          "Resetting the model must cancel both animations without reviving an obsolete tail.");
    QApplication::setWheelScrollLines(oldLines);
}

void verifyRasterInkReuse(QTableWidget &table, SongGlassDelegate &delegate)
{
    table.verticalScrollBar()->setValue(0);
    delegate.stopScrollMotion();
    const QModelIndex index = table.model()->index(0, 1);
    QStyleOptionViewItem option;
    option.initFrom(table.viewport());
    option.font = table.font();
    option.state = QStyle::State_Enabled;
    option.rect = table.visualRect(index);
    QFont font = option.font;
    font.setWeight(QFont::DemiBold);
    const int width = option.rect.width() - 26;
    constexpr qreal dpr = 1.5;
    const QColor ink(QStringLiteral("#eef4fa"));
    const auto paint = [&] {
        QImage image(QSize(qCeil(table.viewport()->width() * dpr),
                           qCeil(table.viewport()->height() * dpr)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        delegate.paint(&painter, option, index);
        return image;
    };
    delegate.m_motionEnvelope = 1;
    delegate.m_scrollImpulse = .6;
    delegate.m_motionAge = .1;
    const QImage firstFrame = paint();
    const qint64 originalKey = delegate.textLayer(index, font, width, dpr, ink).cacheKey();
    delegate.m_motionAge = .3;
    const QImage secondFrame = paint();
    check(firstFrame != secondFrame
              && delegate.textLayer(index, font, width, dpr, ink).cacheKey() == originalKey,
          "Different jelly matrices must deform the frame while reusing the same rasterized text.");
    const QPixmap &density = delegate.textLayer(index, font, width, dpr, ink);
    const QSizeF logical = delegate.m_textLayouts.object(index)->text.size();
    check(density.devicePixelRatioF() == dpr
              && density.width() == qMax(1, qCeil(logical.width() * dpr))
              && density.height() == qMax(1, qCeil(logical.height() * dpr)),
          "Cached text must rasterize at physical device density rather than enlarge a low-resolution bitmap.");
    delegate.m_textLayouts.remove(index);
    qint64 snapshotKey = delegate.textLayer(index, font, width, 1.0, ink).cacheKey();
    qint64 viewportKey = delegate.textLayer(index, font, width, 2.25, ink).cacheKey();
    for (int pass = 0; pass < 8; ++pass) {
        check(delegate.textLayer(index, font, width, 1.0, ink).cacheKey() == snapshotKey
            && delegate.textLayer(index, font, width, 2.25, ink).cacheKey() == viewportKey,
              "Alternating content-capture and native densities must reuse both text rasters.");
    }
    check(delegate.m_textLayouts.object(index)->densityPixels.size() == 2,
          "Each text layout must have a fixed two-density cache budget.");
    // Refresh the snapshot's LRU entry, then request a third density. Only the
    // less recently used viewport entry should be evicted.
    delegate.textLayer(index, font, width, 1.0, ink);
    const qint64 thirdKey = delegate.textLayer(index, font, width, 1.5, ink).cacheKey();
    check(delegate.textLayer(index, font, width, 1.0, ink).cacheKey() == snapshotKey,
          "A third density must retain the recently used raster instead of clearing all variants.");
    check(delegate.textLayer(index, font, width, 1.5, ink).cacheKey() == thirdKey,
          "A stable third density must use its cached raster.");
    const qint64 evictedViewportKey = viewportKey;
    viewportKey = delegate.textLayer(index, font, width, 2.25, ink).cacheKey();
    check(viewportKey != evictedViewportKey,
          "The third density must evict and later rebuild the least recently used viewport variant.");
    snapshotKey = delegate.textLayer(index, font, width, 1.0, ink).cacheKey();
    const auto changed = [&](const QFont &nextFont, int nextWidth, const QColor &nextInk) {
        const qint64 nextSnapshot = delegate.textLayer(index, nextFont, nextWidth, 1.0, nextInk).cacheKey();
        const qint64 nextViewport = delegate.textLayer(index, nextFont, nextWidth, 2.25, nextInk).cacheKey();
        check(nextSnapshot != snapshotKey && nextViewport != viewportKey,
              "Text data, font, width, and ink changes must invalidate both density variants.");
        snapshotKey = nextSnapshot;
        viewportKey = nextViewport;
        check(delegate.textLayer(index, nextFont, nextWidth, 1.0, nextInk).cacheKey() == snapshotKey
            && delegate.textLayer(index, nextFont, nextWidth, 2.25, nextInk).cacheKey() == viewportKey,
              "Rebuilt text variants must remain reusable at both densities.");
    };
    table.item(0, 1)->setText(QStringLiteral("Updated loaded song title with new metadata"));
    changed(font, width, ink);
    QFont larger = font;
    larger.setPointSizeF(QFontInfo(font).pointSizeF() + 2);
    changed(larger, width, ink);
    changed(larger, qMax(1, width - 25), ink);
    QColor dimmed = ink;
    dimmed.setAlpha(145);
    changed(larger, qMax(1, width - 25), dimmed);
    changed(larger, qMax(1, width - 25), ink);
    delegate.stopScrollMotion();
}

void verifyWallpaperMaterial()
{
    QTemporaryDir directory;
    check(directory.isValid(), "The wallpaper material test needs a temporary image directory.");
    QImage wallpaper(2880, 1680, QImage::Format_RGB32);
    {
        QPainter painter(&wallpaper);
        QLinearGradient colors(0, 0, wallpaper.width(), 0);
        colors.setColorAt(0, QColor(38, 190, 220));
        colors.setColorAt(.25, QColor(86, 220, 94));
        colors.setColorAt(.5, QColor(230, 157, 48));
        colors.setColorAt(.75, QColor(193, 82, 219));
        colors.setColorAt(1, QColor(60, 119, 232));
        painter.fillRect(wallpaper.rect(), colors);
    }
    const QString path = directory.filePath(QStringLiteral("hq-color-bands.png"));
    check(wallpaper.save(path), "The high-resolution color-band wallpaper must be readable.");
    AeroSurface surface;
    surface.resize(720, 420);
    BackgroundTheme theme;
    theme.kind = BackgroundKind::Image;
    theme.sourcePath = path;
    theme.dimming = 0;
    check(surface.setBackground(theme), "The real AeroSurface must accept the static wallpaper.");
    QTableWidget table(3, 4, &surface);
    table.setGeometry(16, 38, 688, 290);
    table.verticalHeader()->hide();
    table.horizontalHeader()->hide();
    table.verticalHeader()->setDefaultSectionSize(88);
    table.horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table.setShowGrid(false);
    for (int column = 0; column < 4; ++column) table.setColumnWidth(column, 160);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            table.setItem(row, column, new QTableWidgetItem(QString()));
    auto *delegate = new SongGlassDelegate(&table);
    table.setItemDelegate(delegate);
    surface.show();
    QCoreApplication::processEvents();
    delegate->stopScrollMotion();
    delegate->m_hoveredRow = -1;
    constexpr int row = 1;
    const qreal dpr = table.viewport()->devicePixelRatioF();
    const QSize imageSize(qCeil(table.viewport()->width() * dpr),
                          qCeil(table.viewport()->height() * dpr));
    const auto blank = [&] {
        QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        return image;
    };
    const auto colorAt = [&](const QImage &image, qreal x, qreal y) {
        return image.pixelColor(qBound(0, qFloor(x * dpr), image.width() - 1),
                                qBound(0, qFloor(y * dpr), image.height() - 1));
    };
    const QRect first = table.visualRect(table.model()->index(row, 0));
    const QRect last = table.visualRect(table.model()->index(row, 3));
    const QRectF bounds = QRectF(first.united(last)).adjusted(7, 5, -7, -5);
    QTransform shape;
    shape.translate(bounds.center().x() + 2.4, bounds.center().y() - 1.1);
    shape.shear(.012, .002);
    shape.scale(1.005, .973);
    shape.translate(-bounds.center().x(), -bounds.center().y());
    QPainterPath silhouette;
    silhouette.addRoundedRect(bounds, 18, 18);
    silhouette = shape.map(silhouette);
    QImage expected = blank();
    {
        QPainter painter(&expected);
        surface.paintWater(painter, table.viewport()->rect(),
                           table.viewport()->mapTo(&surface, QPoint()));
    }
    QImage material = blank();
    {
        QPainter painter(&material);
        painter.setRenderHint(QPainter::Antialiasing);
        for (int column = 0; column < 4; ++column) {
            painter.save();
            painter.setClipRect(table.visualRect(table.model()->index(row, column)));
            Liquid::paintSurfaceBackdrop(painter, table.viewport(), bounds, 18, {}, shape);
            painter.restore();
        }
    }
    int interiorSamples = 0;
    for (qreal y = bounds.top() + 8; y < bounds.bottom() - 8; y += 5)
        for (qreal x = bounds.left() + 8; x < bounds.right() - 8; x += 5) {
            const QPointF point(x, y);
            if (!silhouette.contains(point + QPointF(-2, -2))
                || !silhouette.contains(point + QPointF(2, 2))) continue;
            const QColor actual = colorAt(material, x, y);
            check(actual.alpha() >= 250, "A deformed wallpaper silhouette must have no transparent interior holes.");
            check(colorDistance(actual, colorAt(expected, x, y)) <= 6,
                  "Jelly must move its silhouette while the high-resolution wallpaper remains in world coordinates.");
            ++interiorSamples;
        }
    check(interiorSamples > 100, "World-coordinate sampling must cover the card interior across all four columns.");
    const qreal sampleY = bounds.center().y() - 17;
    for (int column = 1; column < 4; ++column) {
        const qreal seam = table.visualRect(table.model()->index(row, column)).left();
        check(colorAt(material, seam - 1 / dpr, sampleY).alpha() >= 250
                  && colorAt(material, seam, sampleY).alpha() >= 250
                  && colorDistance(colorAt(material, seam - 1 / dpr, sampleY),
                                   colorAt(material, seam, sampleY)) < 8,
              "Deformed material must sample continuously across column clips without a seam or hole.");
    }
    check(colorAt(material, bounds.center().x(), first.top() + 1).alpha() == 0,
          "Wallpaper sampling must respect the deformed rounded silhouette and leave the inter-card gap clear.");

    const auto card = [&] {
        QImage image = blank();
        QPainter painter(&image);
        QStyleOptionViewItem option;
        option.initFrom(table.viewport());
        option.font = table.font();
        option.state = QStyle::State_Enabled;
        for (int column = 0; column < 4; ++column) {
            const QModelIndex index = table.model()->index(row, column);
            option.rect = table.visualRect(index);
            delegate->paint(&painter, option, index);
        }
        return image;
    };
    const QImage relaxed = card();
    delegate->m_scrollImpulse = .65;
    delegate->m_motionEnvelope = 1;
    delegate->m_motionAge = .17;
    const QImage moving = card();
    delegate->stopScrollMotion();
    delegate->m_pressedRow = row;
    delegate->m_pressDepth = .75;
    const QImage pressed = card();
    check(moving != relaxed && pressed != relaxed,
          "Real-wallpaper cards must visibly retain jelly and press deformation.");
    const qreal leftX = bounds.left() + bounds.width() * .25;
    const qreal rightX = bounds.left() + bounds.width() * .75;
    for (const QImage *image : {&relaxed, &moving, &pressed}) {
        const QColor leftColor = colorAt(*image, leftX, sampleY);
        const QColor rightColor = colorAt(*image, rightX, sampleY);
        check(colorDistance(leftColor, rightColor) > 50 && leftColor.alpha() >= 250,
              "Glass must reveal the wallpaper's different colors during animation rather than become an opaque solid fill.");
    }
    delegate->m_pressedRow = -1;
    delegate->m_pressDepth = 0;
    check(card() == relaxed, "After jelly and press settle, the real-wallpaper card must recover its original image.");
}

void verifyCacheReuseAndInvalidation(QTableWidget &table, SongGlassDelegate &delegate)
{
    table.verticalScrollBar()->setValue(0);
    waitForEvents(520);
    QPixmap image(54, 54);
    image.fill(QColor(65, 82, 109));
    table.item(0, 0)->setIcon(QIcon(image));
    const QImage first = paintRow(table, delegate, 0);
    const int surfaces = delegate.m_glassLayers.size();
    const int covers = delegate.m_coverLayers.size();
    const int texts = delegate.m_textLayouts.size();
    check(first == paintRow(table, delegate, 0)
              && delegate.m_glassLayers.size() == surfaces
              && delegate.m_coverLayers.size() == covers
              && delegate.m_textLayouts.size() == texts,
          "Repeated visible frames must reuse cached surfaces, covers, and text without changing their appearance.");

    table.item(0, 1)->setText(QStringLiteral("Changed title after background loading"));
    check(first != paintRow(table, delegate, 0),
          "Text caching must not retain stale metadata when a loaded song changes.");
    const QImage titleChanged = paintRow(table, delegate, 0);
    image.fill(QColor(164, 65, 111));
    table.item(0, 0)->setIcon(QIcon(image));
    check(titleChanged != paintRow(table, delegate, 0),
          "Cover caching must replace a newly loaded cover instead of retaining its placeholder.");

    table.setColumnWidth(1, 220);
    const QImage resized = paintRow(table, delegate, 0);
    check(resized == paintRow(table, delegate, 0),
          "Resizing a column must produce a stable surface and new elided text layout.");
    delegate.setAccentColor(QColor(100, 215, 195));
    check(resized != paintRow(table, delegate, 0), "Recoloring must invalidate cached glass surfaces.");
    check(delegate.m_motionTimer.timerType() == Qt::PreciseTimer
              && delegate.m_motionTimer.isSingleShot()
              && delegate.m_motionSchedule.refreshRate() == Aero::displayRefreshRate(&table)
              && delegate.m_motionTimer.interval() >= 1
              && delegate.m_motionTimer.interval() <= Aero::animationInterval(&table),
          "Scroll animation must use precise display deadlines without polling above the refresh budget.");
}

void verifyBoundedLargeLibraryCaches(QTableWidget &table, SongGlassDelegate &delegate)
{
    constexpr int trackCount = 12043;
    table.setRowCount(trackCount);
    {
        const QSignalBlocker blocked(table.model());
        for (int row = 0; row < trackCount; ++row)
            for (int column = 0; column < table.columnCount(); ++column)
                if (!table.item(row, column))
                    table.setItem(row, column, new QTableWidgetItem(QStringLiteral("Song %1").arg(row)));
    }
    // Traversing 400 separate visible contexts exceeds the cache budgets while
    // remaining independent of any arbitrary machine-specific frame deadline.
    for (int row = 0; row < 400; ++row) {
        table.verticalScrollBar()->setValue(row * table.rowHeight(row));
        paintRow(table, delegate, row);
    }
    check(table.rowCount() == trackCount && delegate.m_glassLayers.size() <= 8
              && delegate.m_coverLayers.size() <= 128 && delegate.m_textLayouts.size() <= 256,
          "Painting a large library must keep caches bounded instead of allocating a texture per song.");
}

} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QApplication app(argc, argv);
    QTableWidget table(120, 4);
    table.resize(720, 460);
    table.setShowGrid(false);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table.verticalHeader()->setDefaultSectionSize(80);
    table.horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    for (int column = 0; column < 4; ++column)
        table.setColumnWidth(column, 160);
    for (int row = 0; row < table.rowCount(); ++row)
        for (int column = 0; column < 4; ++column)
            table.setItem(row, column, new QTableWidgetItem(QStringLiteral("Song %1").arg(row)));
    auto *delegate = new SongGlassDelegate(&table);
    table.setItemDelegate(delegate);
    table.show();
    QCoreApplication::processEvents();
    verifyCardContinuity(table, *delegate);
    verifyScrollMotion(table, *delegate);
    verifyContinuousJellyPhase(table, *delegate);
    verifySharedScrollFrames();
    verifyPressFeedback(table, *delegate);
    verifyCacheReuseAndInvalidation(table, *delegate);
    verifyRasterInkReuse(table, *delegate);
    verifyWallpaperMaterial();
    verifyBoundedLargeLibraryCaches(table, *delegate);
    verifySmoothScrolling();
    qInfo("SongGlass tests passed.");
    return 0;
}
