#include "../background_theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdlib>
#ifdef Q_OS_WIN
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

void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Theme fixture must be writable.");
}
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "Theme fixtures require a temporary directory.");
    const QString root = directory.path();
    const QString installation = root + QStringLiteral("/steamapps/common/wallpaper_engine");
    write(installation + QStringLiteral("/wallpaper64.exe"), QByteArray("fixture, never executed"));
    const QString video = root + QStringLiteral("/steamapps/workshop/content/431960/100/project.json");
    const QString scene = root + QStringLiteral("/steamapps/workshop/content/431960/200/project.json");
    const QString web = root + QStringLiteral("/steamapps/workshop/content/431960/300/project.json");
    write(video, R"({"type":"video","file":"clip.mp4","title":"Video fixture"})");
    write(QFileInfo(video).absolutePath() + QStringLiteral("/clip.mp4"), QByteArray("fixture"));
    write(scene, R"({"type":"Scene","file":"scene.json","title":"Scene fixture"})");
    write(web, R"({"type":"web","file":"index.html","title":"Web fixture"})");
    write(root + QStringLiteral("/steamapps/workshop/content/431960/400/project.json"),
          R"({"type":"video","file":"../outside.mp4","title":"Broken fixture"})");
    const QJsonObject wallpaper{{QStringLiteral("file"), QFileInfo(scene).absolutePath() + QStringLiteral("/scene.pkg")}};
    const QJsonObject monitors{{QStringLiteral("monitor"), wallpaper}};
    const QJsonObject general{{QStringLiteral("wallpaperconfig"), monitors}};
    const QJsonObject user{{QStringLiteral("general"), general}};
    const QJsonObject config{{QStringLiteral("fixture-user"), user}};
    write(installation + QStringLiteral("/config.json"), QJsonDocument(config).toJson());
    const auto library = discoverWallpaperEngine({root, root});
    check(library.projects.size() == 3 && library.executable.endsWith(QStringLiteral("wallpaper64.exe")),
          "Discovery must identify installed video/scene/web projects once and ignore missing media.");
    check(library.currentProjectPath == scene, "Read-only discovery must identify the selected project from current configuration.");
    BackgroundTheme selected;
    QString error;
    check(backgroundThemeForFile(video, &selected, &error) && selected.kind == BackgroundKind::Video
              && selected.sourcePath.endsWith(QStringLiteral("/clip.mp4")),
          "A real video project must select its media file for native video playback.");
    check(backgroundThemeForFile(scene, &selected, &error) && selected.kind == BackgroundKind::WallpaperEngine
              && selected.sourcePath == scene,
          "A scene must remain a Wallpaper Engine project rather than masquerade as a playable video.");
    check(backgroundThemeForFile(web, &selected, &error) && selected.kind == BackgroundKind::WallpaperEngine,
          "A web project must use the installed engine rather than unsupported local HTML rendering.");
    write(root + QStringLiteral("/scene.pkg"), QByteArray("not an image or movie"));
    check(!backgroundThemeForFile(root + QStringLiteral("/scene.pkg"), &selected, &error) && !error.isEmpty(),
          "An arbitrary pkg archive must not be falsely accepted as an image or video.");
    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const QString imagePath = root + QStringLiteral("/image.png");
    check(image.save(imagePath) && backgroundThemeForFile(imagePath, &selected, &error)
              && selected.kind == BackgroundKind::Image,
          "User images must be accepted independently of Wallpaper Engine.");
    selected.dimming = 54;
    selected.engineExecutable = library.executable;
    QSettings settings(root + QStringLiteral("/theme.ini"), QSettings::IniFormat);
    saveBackgroundTheme(settings, selected);
    const auto restored = loadBackgroundTheme(settings);
    check(restored.kind == selected.kind && restored.sourcePath == selected.sourcePath
              && restored.dimming == 54 && restored.engineExecutable == selected.engineExecutable,
          "The selected theme and readability level must survive a settings round trip.");
    settings.setValue(QStringLiteral("appearance/backgroundKind"), 99);
    settings.setValue(QStringLiteral("appearance/backgroundDimming"), 999);
    const auto invalid = loadBackgroundTheme(settings);
    check(invalid.kind == BackgroundKind::Liquid && invalid.dimming == 85,
          "Invalid persisted settings must recover to a bounded default theme.");
    return 0;
}
