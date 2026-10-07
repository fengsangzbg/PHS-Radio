#include <QtWidgets>
#include "../aero_widgets.h"
#define private public
#include "../song_glass_delegate.h"
#undef private

#include <cstdlib>
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
              && delegate.m_motionTimer.interval() == Aero::animationInterval(&table),
          "Scroll animations must use the display refresh interval with a precise timer.");
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
    verifyPressFeedback(table, *delegate);
    verifyCacheReuseAndInvalidation(table, *delegate);
    verifyBoundedLargeLibraryCaches(table, *delegate);
    qInfo("SongGlass tests passed.");
    return 0;
}
