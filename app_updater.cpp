#include "app_updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifndef PHSRADIO_VERSION
#define PHSRADIO_VERSION "0.2.3-beta"
#endif

static void initializeUpdaterResources() { Q_INIT_RESOURCE(updater); }

namespace {
const QString Repository = QStringLiteral("/fengsangzbg/PHS-Radio/");
constexpr qint64 MaximumArchive = 2LL * 1024 * 1024 * 1024;

bool report(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool versionParts(QString version, QList<qulonglong> *parts)
{
    if (version.startsWith('v')) version.remove(0, 1);
    const auto match = QRegularExpression(QStringLiteral("\\A(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\z")).match(version);
    if (!match.hasMatch()) return false;
    parts->clear();
    for (int i = 1; i <= 3; ++i) {
        bool ok = false;
        const auto number = match.captured(i).toULongLong(&ok);
        if (!ok) return false;
        parts->append(number);
    }
    return true;
}

struct ComparableVersion {
    QList<qulonglong> numbers;
    bool beta = false;
    bool numberedBeta = false;
    qulonglong betaNumber = 0;
};

bool comparableVersion(const QString &version, ComparableVersion *parsed)
{
    // Local beta builds may compare themselves with the next stable release.
    // Keep versionParts above stable-only: parseRelease uses it to gate remote
    // packages independently of GitHub's draft/prerelease flags.
    static const QRegularExpression expression(QStringLiteral(
        "\\Av?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-(beta)(?:\\.(0|[1-9][0-9]*))?)?\\z"));
    const auto match = expression.match(version);
    if (!match.hasMatch())
        return false;
    parsed->numbers.clear();
    for (int component = 1; component <= 3; ++component) {
        bool valid = false;
        const auto value = match.captured(component).toULongLong(&valid);
        if (!valid)
            return false;
        parsed->numbers.append(value);
    }
    parsed->beta = !match.captured(4).isEmpty();
    parsed->numberedBeta = !match.captured(5).isEmpty();
    if (parsed->numberedBeta) {
        bool valid = false;
        parsed->betaNumber = match.captured(5).toULongLong(&valid);
        if (!valid)
            return false;
    }
    return true;
}
}

AppUpdater::AppUpdater(QObject *parent, const QString &currentVersion,
                       QNetworkAccessManager *network)
    : QObject(parent), m_currentVersion(currentVersion.isEmpty()
        ? QStringLiteral(PHSRADIO_VERSION) : currentVersion),
      m_network(network ? network : new QNetworkAccessManager(this)),
      m_installerPoll(new QTimer(this))
{
    initializeUpdaterResources();
    m_installerPoll->setInterval(100);
    connect(m_installerPoll, &QTimer::timeout, this, [this] {
        const QString resultPath = QDir(m_transactionDir).filePath("installer-result.json");
        if (QFile::exists(resultPath)) {
            QFile file(resultPath);
            if (file.open(QIODevice::ReadOnly)) {
                const auto result = QJsonDocument::fromJson(file.readAll()).object();
                if (!result.value("ok").toBool()) {
                    fail(QStringLiteral("更新安装预检失败：%1\n记录：%2")
                        .arg(result.value("error").toString(), installerLogPath()));
                    return;
                }
            }
        }
        if (QFile::exists(QDir(m_transactionDir).filePath("installer.ready"))) {
            m_installerPoll->stop();
            m_state = State::Launched;
            emit statusChanged(QStringLiteral("安装检查通过，正在退出并更新…"));
            emit installerStarted();
        } else if (++m_pollCount > 6000) {
            // Do not quit on timeout: no files can be replaced until this process exits.
            fail(QStringLiteral("安装器未完成检查，当前版本继续运行。记录：%1").arg(installerLogPath()));
        } else if (m_pollCount % 150 == 0) {
            emit statusChanged(QStringLiteral("正在校验并解压更新包（已等待 %1 秒），当前版本继续运行，可关闭窗口取消。")
                .arg(m_pollCount / 10));
        }
    });
}

AppUpdater::~AppUpdater()
{
    if (m_state == State::Launching) {
        QFile cancelled(QDir(m_transactionDir).filePath("installer.cancel"));
        if (cancelled.open(QIODevice::WriteOnly)) cancelled.write("cancel\n");
    }
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
}

bool AppUpdater::isBusy() const
{
    return m_state == State::Checking || m_state == State::Downloading
        || m_state == State::Launching || m_state == State::Launched;
}

int AppUpdater::compareVersions(const QString &left, const QString &right, bool *valid)
{
    ComparableVersion a, b;
    const bool ok = comparableVersion(left, &a) && comparableVersion(right, &b);
    if (valid) *valid = ok;
    if (!ok) return 0;
    for (int i = 0; i != 3; ++i) {
        if (a.numbers[i] != b.numbers[i]) return a.numbers[i] < b.numbers[i] ? -1 : 1;
    }
    if (a.beta != b.beta)
        return a.beta ? -1 : 1;
    if (a.beta) {
        if (a.numberedBeta != b.numberedBeta)
            return a.numberedBeta ? 1 : -1;
        if (a.betaNumber != b.betaNumber)
            return a.betaNumber < b.betaNumber ? -1 : 1;
    }
    return 0;
}

bool AppUpdater::isOfficialDownloadUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == "https" && url.host() == "github.com"
        && (url.port(-1) == -1 || url.port() == 443) && url.userInfo().isEmpty()
        && url.fragment().isEmpty() && url.query().isEmpty()
        && url.path().startsWith(Repository + "releases/download/");
}

bool AppUpdater::trustedTransferUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty()
        || !url.fragment().isEmpty() || (url.port(-1) != -1 && url.port() != 443)) return false;
    if (isOfficialDownloadUrl(url)) return true;
    if (url.host() == "api.github.com")
        return url.path() == "/repos/fengsangzbg/PHS-Radio/releases/latest" && url.query().isEmpty();
    return url.host() == "release-assets.githubusercontent.com"
        || url.host() == "objects.githubusercontent.com"
        || url.host() == "github-releases.githubusercontent.com";
}

bool AppUpdater::parseRelease(const QByteArray &json, ReleaseInfo *release, QString *error)
{
    if (!release) return report(error, QStringLiteral("未提供更新信息目标。"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return report(error, QStringLiteral("GitHub 返回的发布信息无效。"));
    const auto object = document.object();
    if (object.value("draft").toBool() || object.value("prerelease").toBool())
        return report(error, QStringLiteral("不安装草稿或预发布版本。"));
    ReleaseInfo parsed;
    parsed.tag = object.value("tag_name").toString();
    QList<qulonglong> parts;
    if (!versionParts(parsed.tag, &parts))
        return report(error, QStringLiteral("发布版本号不是正式的 x.y.z 格式。"));
    parsed.version = parsed.tag.startsWith('v') ? parsed.tag.mid(1) : parsed.tag;
    parsed.packageRoot = QStringLiteral("PHSRadio-%1-windows-x64").arg(parsed.version);
    parsed.notes = object.value("body").toString();
    const QStringList names{QStringLiteral("PHS-Radio-%1-windows-x64.zip").arg(parsed.version),
                            QStringLiteral("PHSRadio-%1-windows-x64.zip").arg(parsed.version)};
    int archiveCount = 0, checksumsCount = 0;
    for (const auto &value : object.value("assets").toArray()) {
        const auto asset = value.toObject();
        const QString name = asset.value("name").toString();
        const bool checksumFile = name == "SHA256SUMS.txt" || name == "SHA256SUMS";
        if (!names.contains(name) && !checksumFile) continue;
        const QUrl url(asset.value("browser_download_url").toString());
        if (!isOfficialDownloadUrl(url)
            || url.path() != Repository + "releases/download/" + parsed.tag + '/' + name)
            return report(error, QStringLiteral("更新附件来自非官方地址，已拒绝。"));
        if (checksumFile) { parsed.checksumsUrl = url; ++checksumsCount; }
        else {
            ++archiveCount;
            parsed.assetName = name;
            parsed.archiveUrl = url;
            parsed.assetSize = asset.value("size").toInteger();
            const QString digest = asset.value("digest").toString();
            if (!digest.isEmpty()) {
                if (!QRegularExpression("^sha256:[0-9a-fA-F]{64}$").match(digest).hasMatch())
                    return report(error, QStringLiteral("GitHub 附件校验摘要格式无效。"));
                parsed.apiDigest = digest.mid(7).toLatin1().toLower();
            }
        }
    }
    if (archiveCount != 1 || checksumsCount != 1 || parsed.assetSize <= 0
        || parsed.assetSize > MaximumArchive)
        return report(error, QStringLiteral("发布缺少唯一的 Windows 安装包或 SHA256SUMS 校验文件。"));
    *release = parsed;
    return true;
}

bool AppUpdater::checksumForAsset(const QByteArray &text, const QString &assetName,
                                  QByteArray *sha256, QString *error)
{
    const auto expression = QRegularExpression(QStringLiteral("^([0-9a-fA-F]{64})[ \\t]+\\*?(.+)$"));
    int found = 0;
    QByteArray digest;
    QString content = QString::fromUtf8(text);
    if (content.startsWith(QChar(0xfeff))) content.remove(0, 1);
    for (const auto &line : content.split('\n')) {
        QString normalized = line;
        if (normalized.endsWith('\r')) normalized.chop(1);
        const auto match = expression.match(normalized);
        if (match.hasMatch() && match.captured(2) == assetName) {
            ++found;
            digest = match.captured(1).toLatin1().toLower();
        }
    }
    if (found != 1 || !sha256)
        return report(error, QStringLiteral("SHA256SUMS 未包含该安装包唯一有效的 SHA-256。"));
    *sha256 = digest;
    return true;
}

void AppUpdater::fail(const QString &message)
{
    if (m_state == State::Launching) {
        QFile cancelled(QDir(m_transactionDir).filePath("installer.cancel"));
        if (cancelled.open(QIODevice::WriteOnly)) cancelled.write("cancel\n");
    }
    ++m_generation;
    m_installerPoll->stop();
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    if (m_download) m_download->cancelWriting();
    m_download.reset(); m_hash.reset();
    m_state = m_archivePath.isEmpty() ? (m_release.version.isEmpty() ? State::Idle : State::Available) : State::Ready;
    emit statusChanged(message);
    emit error(message);
}

void AppUpdater::cancel()
{
    // Before the ready handshake the helper can abort without touching installed files.
    if (m_state == State::Launched) return;
    if (m_state == State::Launching) {
        QFile cancelled(QDir(m_transactionDir).filePath("installer.cancel"));
        if (cancelled.open(QIODevice::WriteOnly)) cancelled.write("cancel\n");
        m_installerPoll->stop();
    }
    ++m_generation;
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    if (m_download) m_download->cancelWriting();
    m_download.reset(); m_hash.reset();
    m_state = m_archivePath.isEmpty() ? (m_release.version.isEmpty() ? State::Idle : State::Available) : State::Ready;
    emit statusChanged(QStringLiteral("已取消更新。"));
}

void AppUpdater::fetch(const QUrl &url, qint64 limit, bool archive,
                       std::function<void(const QByteArray &)> done, int redirects)
{
    if (!m_network || !trustedTransferUrl(url) || redirects > 5) {
        fail(QStringLiteral("更新下载重定向到不受信任的地址，或跳转次数过多。")); return;
    }
    const quint64 generation = m_generation;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "PHS-Radio-Updater/" + m_currentVersion.toLatin1());
    request.setRawHeader("Accept", url.host() == "api.github.com" ? "application/vnd.github+json" : "application/octet-stream");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(60000);
    auto *reply = m_network->get(request);
    m_reply = reply;
    auto data = std::make_shared<QByteArray>();
    auto received = std::make_shared<qint64>(0);
    const auto consume = [this, reply, generation, data, received, limit, archive] {
        if (generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) { reply->readAll(); return; }
        while (reply->bytesAvailable() > 0) {
            const QByteArray chunk = reply->read(64 * 1024);
            *received += chunk.size();
            if (*received > limit) { fail(QStringLiteral("下载超过允许的文件大小，已取消。")); return; }
            if (archive) {
                if (!m_download || m_download->write(chunk) != chunk.size()) {
                    fail(QStringLiteral("无法写入更新包，请检查磁盘空间与文件权限。")); return;
                }
                m_hash->addData(chunk);
            } else data->append(chunk);
        }
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    if (archive) connect(reply, &QNetworkReply::downloadProgress, this,
        [this, generation](qint64 received, qint64 total) {
            if (generation == m_generation) emit progress(received, total);
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation, data, received, limit, archive, done = std::move(done), redirects, consume] {
            if (generation != m_generation) { reply->deleteLater(); return; }
            consume();
            if (generation != m_generation) return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 300 && status < 400) {
                const QUrl next = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                m_reply = nullptr; reply->deleteLater();
                fetch(next, limit, archive, done, redirects + 1);
                return;
            }
            if (reply->error() != QNetworkReply::NoError || status != 200) {
                QString message = status == 403 || status == 429
                    ? QStringLiteral("GitHub 请求暂时受限，请稍后重试。")
                    : status == 404 ? QStringLiteral("尚未找到可安装的官方版本。")
                    : QStringLiteral("更新下载失败（HTTP %1）：%2").arg(status).arg(reply->errorString());
                fail(message); return;
            }
            m_reply = nullptr; reply->deleteLater();
            if (archive && *received != m_release.assetSize) {
                fail(QStringLiteral("更新包大小与发布信息不一致，已拒绝安装。")); return;
            }
            done(*data);
        });
}

void AppUpdater::checkForUpdates()
{
    if (isBusy()) return;
    cancel();
    m_release = {}; m_archivePath.clear(); m_transactionDir.clear();
    m_state = State::Checking;
    emit statusChanged(QStringLiteral("正在检查官方更新…"));
    fetch(QUrl("https://api.github.com/repos/fengsangzbg/PHS-Radio/releases/latest"), 2 * 1024 * 1024, false,
        [this](const QByteArray &json) {
            ReleaseInfo release;
            QString message;
            if (!parseRelease(json, &release, &message)) { fail(message); return; }
            bool valid = false;
            const int order = compareVersions(release.version, m_currentVersion, &valid);
            if (!valid) { fail(QStringLiteral("当前程序版本号无效，无法安全比较版本。")); return; }
            if (order <= 0) {
                m_state = State::Idle;
                emit statusChanged(QStringLiteral("暂无更新的正式版本。"));
                emit upToDate(); return;
            }
            m_release = release; m_state = State::Available;
            emit statusChanged(QStringLiteral("发现新版本 %1。").arg(release.version));
            emit updateAvailable(release.version, release.notes);
        });
}

bool AppUpdater::prepareTransaction()
{
    const QString cache = m_cacheRootOverride.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : m_cacheRootOverride;
    if (cache.isEmpty() || !QDir().mkpath(QDir(cache).filePath("updates"))) {
        fail(QStringLiteral("无法创建本地更新缓存目录。")); return false;
    }
    QTemporaryDir directory(QDir(cache).filePath("updates/install-XXXXXX"));
    if (!directory.isValid()) { fail(QStringLiteral("无法创建更新事务目录。")); return false; }
    directory.setAutoRemove(false);
    m_transactionDir = directory.path();
    return true;
}

void AppUpdater::downloadUpdate()
{
    if (isBusy() || m_release.archiveUrl.isEmpty()) return;
    m_archivePath.clear();
    if (!prepareTransaction()) return;
    m_state = State::Downloading;
    emit statusChanged(QStringLiteral("正在读取发布校验和…"));
    fetch(m_release.checksumsUrl, 256 * 1024, false, [this](const QByteArray &text) {
        QString message;
        if (!checksumForAsset(text, m_release.assetName, &m_expectedDigest, &message)) { fail(message); return; }
        if (!m_release.apiDigest.isEmpty() && m_release.apiDigest != m_expectedDigest) {
            fail(QStringLiteral("发布校验和与 GitHub 附件摘要不一致，已拒绝安装。")); return;
        }
        const QString destination = QDir(m_transactionDir).filePath(m_release.assetName);
        m_download = std::make_unique<QSaveFile>(destination);
        if (!m_download->open(QIODevice::WriteOnly)) { fail(QStringLiteral("无法创建更新下载文件。")); return; }
        m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
        emit statusChanged(QStringLiteral("正在下载 %1…").arg(m_release.version));
        fetch(m_release.archiveUrl, m_release.assetSize, true, [this, destination](const QByteArray &) {
            if (m_hash->result().toHex() != m_expectedDigest) {
                fail(QStringLiteral("安装包 SHA-256 校验失败，当前版本未改变。")); return;
            }
            if (!m_download->commit()) { fail(QStringLiteral("无法保存已校验的更新包。")); return; }
            m_download.reset(); m_hash.reset(); m_archivePath = destination;
            m_state = State::Ready;
            emit statusChanged(QStringLiteral("更新包校验通过，可以安装并重新启动。"));
            emit updateReady(m_release.version);
        });
    });
}

QString AppUpdater::installerLogPath() const
{
    return m_transactionDir.isEmpty() ? QString() : QDir(m_transactionDir).filePath("update.log");
}

bool AppUpdater::launchInstaller()
{
    if (m_state != State::Ready || m_archivePath.isEmpty()) return false;
#ifndef Q_OS_WIN
    fail(QStringLiteral("一键安装目前仅支持 Windows。")); return false;
#else
    // Each launch gets its own handshake files, including after cancellation/retry.
    QTemporaryDir attempt(QDir(m_transactionDir).filePath("attempt-XXXXXX"));
    if (!attempt.isValid()) { fail(QStringLiteral("无法创建安装器工作目录。")); return false; }
    attempt.setAutoRemove(false);
    m_transactionDir = attempt.path();
    QFile helper(":/updater/update-install.ps1");
    QSaveFile script(QDir(m_transactionDir).filePath("update-install.ps1"));
    if (!helper.open(QIODevice::ReadOnly) || !script.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("无法读取程序内置安装器。")); return false;
    }
    const QByteArray scriptBytes = helper.readAll();
    if (script.write(scriptBytes) != scriptBytes.size() || !script.commit()) {
        fail(QStringLiteral("无法保存程序内置安装器。")); return false;
    }
    const QJsonObject plan{{"schema", 1}, {"archivePath", m_archivePath},
        {"expectedSha256", QString::fromLatin1(m_expectedDigest)},
        {"installDir", QCoreApplication::applicationDirPath()},
        {"packageRoot", m_release.packageRoot},
        {"parentPid", QCoreApplication::applicationPid()}, {"restartExe", "PHSRadio.exe"}};
    QSaveFile planFile(QDir(m_transactionDir).filePath("plan.json"));
    const QByteArray bytes = QJsonDocument(plan).toJson(QJsonDocument::Compact);
    if (!planFile.open(QIODevice::WriteOnly) || planFile.write(bytes) != bytes.size() || !planFile.commit()) {
        fail(QStringLiteral("无法保存更新安装计划。")); return false;
    }
    wchar_t systemPath[MAX_PATH] = {};
    if (!GetSystemDirectoryW(systemPath, MAX_PATH)) { fail(QStringLiteral("无法定位 Windows 安装器。")); return false; }
    QProcess process;
    process.setProgram(QDir(QString::fromWCharArray(systemPath)).filePath("WindowsPowerShell/v1.0/powershell.exe"));
    process.setArguments({"-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-WindowStyle", "Hidden", "-File", script.fileName(), "-PlanFile", planFile.fileName()});
    process.setWorkingDirectory(m_transactionDir);
    process.setStandardOutputFile(QDir(m_transactionDir).filePath("helper-output.log"));
    process.setStandardErrorFile(QDir(m_transactionDir).filePath("helper-output.log"), QIODevice::Append);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    if (!process.startDetached()) { fail(QStringLiteral("无法启动 Windows 更新安装器。")); return false; }
    m_state = State::Launching; m_pollCount = 0;
    emit statusChanged(QStringLiteral("正在检查安装包及软件目录…"));
    m_installerPoll->start();
    return true;
#endif
}
