#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class QSettings;

enum class BackgroundKind { Liquid, Image, Video, WallpaperEngine };

struct BackgroundTheme {
    BackgroundKind kind = BackgroundKind::Liquid;
    QString sourcePath;
    QString engineExecutable;
    QString displayName;
    int dimming = 38;
};

BackgroundTheme loadBackgroundTheme(QSettings &settings);
void saveBackgroundTheme(QSettings &settings, const BackgroundTheme &theme);
bool backgroundThemeForFile(const QString &path, BackgroundTheme *theme, QString *error = nullptr);

struct WallpaperEngineProject {
    QString name;
    QString type;
    QString projectPath;
    QString assetPath;
    QString previewPath;
};

struct WallpaperEngineLibrary {
    QString executable;
    QString currentProjectPath;
    QVector<WallpaperEngineProject> projects;
};

// Read-only discovery of installed files; never starts Steam or changes a wallpaper.
WallpaperEngineLibrary discoverWallpaperEngine(const QStringList &steamRoots = {});
