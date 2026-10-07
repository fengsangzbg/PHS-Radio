#include <QtWidgets>
#include "../glass_title_bar.h"
#include "../aero_widgets.h"
#include <cstdlib>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) { qCritical("%s", message); std::abort(); }
}

class CloseObservedWindow final : public QMainWindow {
public:
    bool closeRequested = false;
protected:
    void closeEvent(QCloseEvent *event) override { closeRequested = true; event->ignore(); }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    CloseObservedWindow owner;
    owner.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    auto *central = new QWidget;
    owner.setCentralWidget(central);
    auto *layout = new QVBoxLayout(central); layout->setContentsMargins(0, 0, 0, 0);
    auto *bar = new GlassTitleBar(&owner, central);
    layout->addWidget(bar); layout->addStretch();
    owner.resize(760, 420); owner.show();
    QCoreApplication::processEvents();
    owner.setWindowTitle(QStringLiteral("Transparent caption"));
    check(bar->findChildren<QLabel *>().size() == 2, "Caption must expose the real owner icon and title.");
    bool titleObserved = false;
    for (QLabel *label : bar->findChildren<QLabel *>())
        if (label->text() == owner.windowTitle()) titleObserved = true;
    check(titleObserved, "Window title changes must propagate into the transparent caption.");
    auto *maximize = bar->findChild<QPushButton *>(QStringLiteral("windowMaximize"));
    auto *minimize = bar->findChild<QPushButton *>(QStringLiteral("windowMinimize"));
    auto *close = bar->findChild<QPushButton *>(QStringLiteral("windowClose"));
    check(maximize && minimize && close, "All three native window actions must remain available.");
    maximize->click(); QCoreApplication::processEvents();
    check(owner.isMaximized() && maximize->accessibleName() == QStringLiteral("还原窗口"),
          "Maximizing must update both owner state and the restore action.");
    maximize->click(); QCoreApplication::processEvents();
    check(!owner.isMaximized(), "The same caption control must restore the window.");
    const QPoint point(280, bar->height() / 2);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(point),
        QPointF(bar->mapToGlobal(point)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &doubleClick); QCoreApplication::processEvents();
    check(owner.isMaximized(), "Double-clicking the transparent title area must maximize the window.");
    maximize->click(); minimize->click(); QCoreApplication::processEvents();
    check(owner.isMinimized(), "Minimizing must retain the normal OS window state.");
    owner.showNormal(); QCoreApplication::processEvents();
    close->click();
    check(owner.closeRequested && owner.isVisible(),
          "Closing through the caption must dispatch the owner's close event, including cancellation.");
    owner.setWindowTitle({});
    QImage frame(bar->size(), QImage::Format_ARGB32_Premultiplied); frame.fill(Qt::transparent);
    bar->render(&frame);
    check(qAlpha(frame.pixel(bar->width() / 2, bar->height() / 2)) == 0,
          "The title bar's open area must preserve transparency instead of painting a solid caption strip.");
    return 0;
}
