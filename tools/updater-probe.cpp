// Release-tooling probe: runs the real updater against the live GitHub release
// so a failed "check for updates" can be diagnosed without the GUI.
#include "../app_updater.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString current = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("0.3.1");
    const bool download = argc > 2 && QString::fromLatin1(argv[2]) == QLatin1String("download");

    AppUpdater updater(nullptr, current);
    bool settled = false;
    QObject::connect(&updater, &AppUpdater::statusChanged, [](const QString &s) {
        std::printf("[status] %s\n", qPrintable(s));
    });
    QObject::connect(&updater, &AppUpdater::updateAvailable, [&](const QString &v, const QString &) {
        std::printf("[available] %s\n", qPrintable(v));
        if (download) {
            std::printf("[downloading]\n");
            updater.downloadUpdate();
            return;
        }
        settled = true;
        app.quit();
    });
    QObject::connect(&updater, &AppUpdater::upToDate, [&] {
        std::printf("[up-to-date]\n");
        settled = true;
        app.quit();
    });
    QObject::connect(&updater, &AppUpdater::updateReady, [&](const QString &v) {
        std::printf("[ready] %s -> %s\n", qPrintable(v), qPrintable(updater.downloadedArchive()));
        settled = true;
        app.quit();
    });
    QObject::connect(&updater, &AppUpdater::progress, [](qint64 got, qint64 total) {
        static int last = -10;
        const int percent = total > 0 ? int(got * 100 / total) : 0;
        if (percent >= last + 10) { last = percent; std::printf("[progress] %d%%\n", percent); }
    });
    QObject::connect(&updater, &AppUpdater::error, [&](const QString &message) {
        std::printf("[error] %s\n", qPrintable(message));
        settled = true;
        app.quit();
    });

    QTimer::singleShot(0, &updater, &AppUpdater::checkForUpdates);
    QTimer::singleShot(download ? 300000 : 30000, &app, [&] {
        std::printf("[timeout] settled=%d\n", int(settled));
        app.quit();
    });
    const int result = app.exec();
    std::printf("exit=%d settled=%d\n", result, int(settled));
    return result;
}
