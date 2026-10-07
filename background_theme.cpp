#include "background_theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <algorithm>

namespace {
QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

WallpaperEngineProject readProject(const QString &path)
{
    const QJsonObject object = readObject(path);
    WallpaperEngineProject result;
    result.type = object.value(QStringLiteral("type")).toString().toLower();
    if (result.type != QStringLiteral("video") && result.type != QStringLiteral("scene")
        && result.type != QStringLiteral("web"))
        return {};
    result.projectPath = QFileInfo(path).absoluteFilePath();
    const QDir directory(QFileInfo(path).absolutePath());
    const QString file = object.value(QStringLiteral("file")).toString();
    // The asset belongs to this project. Scene/web resources may live in a pkg
    // archive and are rendered only by Wallpaper Engine, never decoded here.
    if (!file.isEmpty() && QDir::isRelativePath(file)) {
        const QString asset = QDir::cleanPath(directory.absoluteFilePath(file));
        if (asset.startsWith(directory.absolutePath() + QLatin1Char('/'), Qt::CaseInsensitive))
            result.assetPath = asset;
    }
    if (result.type == QStringLiteral("video") && !QFileInfo::exists(result.assetPath))
        return {};
    result.name = object.value(QStringLiteral("title")).toString().trimmed();
    if (result.name.isEmpty())
        result.name = directory.dirName();
    QString preview = object.value(QStringLiteral("preview")).toString();
    if (preview.isEmpty())
        preview = QStringLiteral("preview.jpg");
    if (QDir::isRelativePath(preview))
        result.previewPath = directory.absoluteFilePath(preview);
    return result;
}

QString currentProject(const QJsonValue &value)
{
    if (value.isString()) {
        const QString path = value.toString();
        const QString candidate = QFileInfo(path).fileName() == QStringLiteral("project.json")
            ? path : QFileInfo(path).absolutePath() + QStringLiteral("/project.json");
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString candidate = currentProject(it.value());
            if (!candidate.isEmpty())
                return candidate;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray()) {
            const QString candidate = currentProject(child);
            if (!candidate.isEmpty())
                return candidate;
        }
    }
    return {};
}
} // namespace

BackgroundTheme loadBackgroundTheme(QSettings &settings)
{
    BackgroundTheme result;
    const int kind = settings.value(QStringLiteral("appearance/backgroundKind"), 0).toInt();
    if (kind >= 0 && kind <= static_cast<int>(BackgroundKind::WallpaperEngine))
        result.kind = static_cast<BackgroundKind>(kind);
    result.sourcePath = settings.value(QStringLiteral("appearance/backgroundPath")).toString();
    result.engineExecutable = settings.value(QStringLiteral("appearance/backgroundEngine")).toString();
    result.displayName = settings.value(QStringLiteral("appearance/backgroundName")).toString();
    result.dimming = std::clamp(settings.value(QStringLiteral("appearance/backgroundDimming"), 38).toInt(), 0, 85);
    return result;
}

void saveBackgroundTheme(QSettings &settings, const BackgroundTheme &theme)
{
    settings.setValue(QStringLiteral("appearance/backgroundKind"), static_cast<int>(theme.kind));
    settings.setValue(QStringLiteral("appearance/backgroundPath"), theme.sourcePath);
    settings.setValue(QStringLiteral("appearance/backgroundEngine"), theme.engineExecutable);
    settings.setValue(QStringLiteral("appearance/backgroundName"), theme.displayName);
    settings.setValue(QStringLiteral("appearance/backgroundDimming"), std::clamp(theme.dimming, 0, 85));
}

bool backgroundThemeForFile(const QString &path, BackgroundTheme *theme, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!theme || !QFileInfo(path).isFile())
        return fail(QStringLiteral("背景文件不存在，请重新选择。"));
    BackgroundTheme result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    result.displayName = QFileInfo(path).completeBaseName();
    const QString extension = QFileInfo(path).suffix().toLower();
    if (extension == QStringLiteral("json")) {
        const auto project = readProject(path);
        if (project.projectPath.isEmpty())
            return fail(QStringLiteral("不是可识别的 Wallpaper Engine 壁纸项目。"));
        result.displayName = project.name;
        if (project.type == QStringLiteral("video")) {
            result.kind = BackgroundKind::Video;
            result.sourcePath = project.assetPath;
        } else {
            result.kind = BackgroundKind::WallpaperEngine;
            result.sourcePath = project.projectPath;
        }
    } else if (QStringList{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"),
                           QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v"),
                           QStringLiteral("wmv")}.contains(extension)) {
        result.kind = BackgroundKind::Video;
    } else {
        QImageReader image(path);
        if (!image.canRead())
            return fail(QStringLiteral("请选择图片、GIF、视频，或 Wallpaper Engine 的 project.json。"));
        result.kind = BackgroundKind::Image;
    }
    *theme = result;
    if (error)
        error->clear();
    return true;
}

WallpaperEngineLibrary discoverWallpaperEngine(const QStringList &steamRoots)
{
    WallpaperEngineLibrary result;
    QStringList roots = steamRoots;
    if (roots.isEmpty()) {
#ifdef Q_OS_WIN
        QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
        roots.append(steam.value(QStringLiteral("SteamPath")).toString());
        roots.append(QStringLiteral("D:/steam"));
        roots.append(qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Steam"));
#endif
    }
    roots.removeAll(QString());
    const QStringList initialRoots = roots;
    const QRegularExpression libraryPath(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
    for (const QString &root : initialRoots) {
        QFile libraries(QDir(root).filePath(QStringLiteral("steamapps/libraryfolders.vdf")));
        if (libraries.open(QIODevice::ReadOnly) && libraries.size() < 1024 * 1024) {
            auto matches = libraryPath.globalMatch(QString::fromUtf8(libraries.readAll()));
            while (matches.hasNext())
                roots.append(matches.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("/")));
        }
    }
    QSet<QString> knownRoots, knownProjects;
    for (QString root : roots) {
        root = QDir::cleanPath(QDir::fromNativeSeparators(root));
        if (knownRoots.contains(root.toLower()))
            continue;
        knownRoots.insert(root.toLower());
        const QDir installation(QDir(root).filePath(QStringLiteral("steamapps/common/wallpaper_engine")));
        for (const QString &executable : {QStringLiteral("wallpaper64.exe"), QStringLiteral("wallpaper32.exe")}) {
            const QString path = installation.filePath(executable);
            if (result.executable.isEmpty() && QFileInfo(path).isFile())
                result.executable = path;
        }
        const QJsonObject configuration = readObject(installation.filePath(QStringLiteral("config.json")));
        if (result.currentProjectPath.isEmpty()) {
            for (const QJsonValue &user : configuration) {
                const QString current = currentProject(user.toObject().value(QStringLiteral("general"))
                    .toObject().value(QStringLiteral("wallpaperconfig")));
                if (!current.isEmpty()) {
                    result.currentProjectPath = current;
                    break;
                }
            }
        }
        const QStringList directories{
            QDir(root).filePath(QStringLiteral("steamapps/workshop/content/431960")),
            installation.filePath(QStringLiteral("projects/defaultprojects")),
            installation.filePath(QStringLiteral("projects/myprojects"))};
        for (const QString &directory : directories) {
            const QDir parent(directory);
            for (const QString &child : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const auto project = readProject(parent.filePath(child + QStringLiteral("/project.json")));
                if (project.projectPath.isEmpty() || knownProjects.contains(project.projectPath.toLower()))
                    continue;
                knownProjects.insert(project.projectPath.toLower());
                result.projects.append(project);
            }
        }
    }
    std::sort(result.projects.begin(), result.projects.end(), [](const auto &left, const auto &right) {
        return left.name.localeAwareCompare(right.name) < 0;
    });
    return result;
}
