#include "../update_panel.h"
#include "../app_updater.h"
#include <QApplication>
#include <QMainWindow>
#include <cstdio>
#include <cstdlib>

static void verify(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QMainWindow owner;
    // The constructor's check is queued; deliberately keep this regression
    // synchronous so no network or real installer is started.
    UpdatePanel panel(QStringLiteral("0.2.2"), QColor(40, 170, 230), nullptr, &owner);
    bool readyToQuit = false;
    panel.onInstallerStarted = [&] {
        verify(!panel.isVisible(), "The modal update dialog must be hidden before the normal application quit callback.");
        verify(panel.result() == QDialog::Accepted, "An authorized update must end its modal loop successfully.");
        readyToQuit = true;
    };
    panel.show();
    verify(panel.isVisible(), "The update dialog must initially be visible.");
    auto *updater = panel.findChild<AppUpdater *>();
    verify(updater != nullptr, "The update dialog must own its updater.");
    updater->installerStarted();
    verify(readyToQuit && !panel.isVisible(), "A committed update must allow QApplication quit to close remaining windows.");
    return 0;
}
