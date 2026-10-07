#include <QtWidgets>
#define private public
#include "../playlist_drawer.h"
#include "../aero_widgets.h"
#undef private

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
    QEventLoop events;
    QTimer::singleShot(duration, &events, &QEventLoop::quit);
    events.exec();
}

void verifyPointerAndFocus(QWidget &host, PlaylistDrawer &drawer,
                           QLineEdit &outside, QLineEdit &input, QListWidget &list)
{
    outside.setFocus();
    QCoreApplication::processEvents();
    check(!drawer.isExpanded() && drawer.isHidden(), "The drawer must begin hidden.");
    check(QApplication::focusWidget() == &outside, "A hidden drawer must not steal input focus.");
    drawer.updatePointer(QPoint(17, 100), true);
    check(!drawer.isExpanded(), "A pointer beyond the 16px edge must not open the drawer.");
    drawer.updatePointer(QPoint(16, 100), true);
    check(drawer.isExpanded(), "The 16px edge must expand the drawer.");
    waitForEvents(240);
    check(drawer.geometry().left() == 10 && !drawer.isHidden(),
          "The opening animation must reach the visible panel position.");
    check(QApplication::focusWidget() == &outside, "Hover expansion must preserve outside input focus.");

    drawer.updatePointer(QPoint(host.width() - 5, 100), true);
    waitForEvents(250);
    check(drawer.isExpanded(), "Leaving the drawer must honor the close delay.");
    drawer.updatePointer(QPoint(120, 100), true);
    waitForEvents(360);
    check(drawer.isExpanded(), "Entering the panel must cancel a pending close.");
    drawer.updatePointer(QPoint(host.width() - 5, 100), true);
    waitForEvents(750);
    check(!drawer.isExpanded() && drawer.isHidden(), "Leaving the drawer must close it after the delay.");
    check(QApplication::focusWidget() == &outside, "A collapsed drawer must preserve outside focus.");

    drawer.updatePointer(QPoint(5, 100), true);
    waitForEvents(240);
    input.setFocus();
    QCoreApplication::processEvents();
    check(QApplication::focusWidget() == &input, "The drawer input must receive keyboard focus.");
    drawer.updatePointer(QPoint(host.width() - 5, 100), true);
    waitForEvents(750);
    check(drawer.isExpanded(), "Active text input must keep the drawer open.");

    list.setFocus();
    QCoreApplication::processEvents();
    check(QApplication::focusWidget() == &list, "The playlist list must receive keyboard focus.");
    drawer.updatePointer(QPoint(host.width() - 5, 100), true);
    waitForEvents(750);
    check(!drawer.isExpanded() && drawer.isHidden(),
          "Playlist selection focus must not permanently prevent automatic collapse.");
}

void verifyPinAndResize(QWidget &host, PlaylistDrawer &drawer, QLineEdit &outside)
{
    outside.setFocus();
    drawer.setPinned(true);
    drawer.updatePointer(QPoint(host.width() - 5, 100), false);
    waitForEvents(750);
    check(drawer.isExpanded() && drawer.isPinned(), "A pinned drawer must stay expanded.");
    drawer.setExpanded(false);
    waitForEvents(240);
    check(!drawer.isExpanded() && drawer.isHidden(), "An explicit close must also close a pinned drawer.");
    drawer.setPinned(false);
    drawer.setExpanded(true);
    waitForEvents(240);
    host.resize(260, 420);
    QCoreApplication::processEvents();
    check(drawer.geometry() == QRect(10, 10, 240, 400),
          "The expanded drawer must adapt to a narrower and shorter host.");
    host.resize(900, 600);
    QCoreApplication::processEvents();
    check(drawer.geometry() == QRect(10, 10, 380, 580),
          "The drawer must keep its intended width when the host grows.");
    drawer.setExpanded(false);
    waitForEvents(240);
    host.resize(720, 520);
    QCoreApplication::processEvents();
    check(drawer.isHidden() && drawer.geometry().right() < 0 && drawer.height() == 500,
          "A collapsed drawer must remain outside the resized host.");
}

void verifyAnimationReversal(PlaylistDrawer &drawer, QLineEdit &outside)
{
    outside.setFocus();
    drawer.setExpanded(true);
    waitForEvents(60);
    drawer.setExpanded(false);
    waitForEvents(60);
    drawer.setExpanded(true);
    waitForEvents(240);
    check(drawer.isExpanded() && !drawer.isHidden() && drawer.geometry().left() == 10,
          "Rapidly reversed animations must finish in the latest requested expanded state.");
    drawer.setExpanded(false);
    waitForEvents(60);
    drawer.setExpanded(true);
    waitForEvents(60);
    drawer.setExpanded(false);
    waitForEvents(240);
    check(!drawer.isExpanded() && drawer.isHidden() && drawer.geometry().right() < 0,
          "Rapidly reversed animations must also finish fully collapsed.");
    check(QApplication::focusWidget() == &outside, "Animation reversals must preserve outside input focus.");
}

void verifyGlassAndButtons(QWidget &host, PlaylistDrawer &drawer)
{
    const QColor accent(QStringLiteral("#59d6c1"));
    drawer.setAccentColor(accent);
    check(drawer.accentColor() == accent, "The drawer must retain the selected glass accent.");
    drawer.setAccentColor(QColor());
    check(drawer.accentColor() == Aero::defaultAccent(), "An invalid accent must restore the default blue.");
    QImage glass(QSize(200, 120), QImage::Format_ARGB32_Premultiplied);
    glass.fill(Qt::transparent);
    {
        QPainter painter(&glass);
        painter.setClipRect(QRect(0, 0, 100, 120));
        Aero::paintGlass(painter, QRectF(8, 8, 180, 100), Aero::defaultAccent());
    }
    check(glass.pixelColor(70, 30).alpha() > 0, "The glass painter must render a visible liquid reflection.");
    check(glass.pixelColor(70, 30).alpha() < 145,
          "Liquid glass must stay translucent instead of becoming an opaque white or blue body.");
    check(glass.pixelColor(150, 60).alpha() == 0, "Glass painting must respect the caller's clip rectangle.");

    JellyButton button(QString(), &host);
    button.setGlyph(JellyButton::Glyph::Play);
    button.setAccentColor(accent);
    button.setAccessibleName(QStringLiteral("播放"));
    button.setCheckable(true);
    button.setGeometry(500, 85, 56, 56);
    int clicks = 0;
    QObject::connect(&button, &QPushButton::clicked, &host, [&] { ++clicks; });
    button.show();
    button.click();
    check(clicks == 1 && button.isChecked(), "A glass button must preserve clicked and checkable semantics.");
    button.setFocus();
    QCoreApplication::processEvents();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&button, &press);
    waitForEvents(140);
    check(button.isDown() && button.m_pressure > .9,
          "Keyboard activation must visibly compress the jelly button.");
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&button, &release);
    waitForEvents(520);
    check(clicks == 2 && !button.isChecked() && !button.isDown(),
          "Keyboard release must preserve click and toggle behavior.");
    check(std::abs(button.m_pressure) < .001, "The elastic release must settle back to the original size.");
    check(button.accessibleName() == QStringLiteral("播放"), "Custom painting must preserve accessibility labels.");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget host;
    host.resize(900, 600);
    QLineEdit outside(&host);
    outside.setGeometry(500, 20, 280, 40);
    PlaylistDrawer drawer(&host);
    drawer.m_pointerTimer->stop(); // Public updatePointer supplies deterministic hover positions.
    QLineEdit input(&drawer);
    QListWidget list(&drawer);
    list.addItem(QStringLiteral("Test playlist"));
    drawer.contentLayout()->addWidget(&input);
    drawer.contentLayout()->addWidget(&list);
    host.show();
    host.activateWindow();
    QCoreApplication::processEvents();
    verifyPointerAndFocus(host, drawer, outside, input, list);
    verifyPinAndResize(host, drawer, outside);
    verifyAnimationReversal(drawer, outside);
    verifyGlassAndButtons(host, drawer);
    return 0;
}
