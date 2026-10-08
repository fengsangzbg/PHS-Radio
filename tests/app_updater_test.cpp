#include <QtCore>
#include <QtNetwork>
#include <functional>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define private public
#include "../app_updater.h"
#undef private

namespace {
void verify(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::fflush(stderr); std::abort(); }
}

void await(const std::function<bool()> &done)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!done() && elapsed.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    verify(done(), "An asynchronous updater operation must finish within the test deadline.");
}

struct Response {
    QByteArray body;
    int status = 200;
    QUrl redirect;
    int delay = 0;
};

class FixtureReply final : public QNetworkReply {
public:
    FixtureReply(const QNetworkRequest &request, const Response &response, QObject *parent)
        : QNetworkReply(parent), m_body(response.body)
    {
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        setHeader(QNetworkRequest::ContentLengthHeader, m_body.size());
        if (!response.redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(response.delay, this, [this] {
            if (isFinished()) return;
            emit downloadProgress(m_body.size(), m_body.size());
            emit readyRead();
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    void abort() override
    {
        if (isFinished()) return;
        setError(QNetworkReply::OperationCanceledError, "Fixture request aborted.");
        setFinished(true); emit finished();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *destination, qint64 maximum) override
    {
        const qint64 count = qMin(maximum, static_cast<qint64>(m_body.size()) - m_offset);
        if (!count) return -1;
        std::memcpy(destination, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FixtureNetwork final : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QUrl> requested;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *) override
    {
        verify(operation == GetOperation, "The updater must only send read-only GET requests.");
        verify(!responses.isEmpty(), "The updater must not issue an unplanned request.");
        requested.append(request.url());
        return new FixtureReply(request, responses.takeFirst(), this);
    }
};

QByteArray checksum(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
QString assetName() { return QStringLiteral("PHS-Radio-0.2.2-windows-x64.zip"); }
QString releaseUrl(const QString &name) { return "https://github.com/fengsangzbg/PHS-Radio/releases/download/v0.2.2/" + name; }

QJsonObject release(const QByteArray &archive)
{
    return {{"tag_name", "v0.2.2"}, {"draft", false}, {"prerelease", false}, {"body", "Fixture release notes."},
        {"assets", QJsonArray{
            QJsonObject{{"name", assetName()}, {"browser_download_url", releaseUrl(assetName())},
                {"size", archive.size()}, {"digest", "sha256:" + QString::fromLatin1(checksum(archive))}},
            QJsonObject{{"name", "SHA256SUMS"}, {"browser_download_url", releaseUrl("SHA256SUMS")}, {"size", 200}},
            QJsonObject{{"name", "source.zip"}, {"browser_download_url", "https://example.invalid/ignored"}}}}};
}

QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

void verifyParsers()
{
    bool valid = false;
    verify(AppUpdater::compareVersions("0.2.10", "0.2.9", &valid) > 0 && valid,
        "Version comparison must compare numeric components, not strings.");
    verify(AppUpdater::compareVersions("v1.0.0", "0.999.999", &valid) > 0 && valid, "Major versions must take priority.");
    verify(AppUpdater::compareVersions("v0.2.2", "0.2.2", &valid) == 0 && valid, "Tag prefix v must be accepted.");
    verify(AppUpdater::compareVersions("0.2.3-beta", "0.2.3", &valid) < 0 && valid
        && AppUpdater::compareVersions("0.2.3", "0.2.3-beta", &valid) > 0 && valid,
        "The next stable release must sort above its same-version local beta.");
    verify(AppUpdater::compareVersions("0.2.3-beta", "0.2.2", &valid) > 0 && valid
        && AppUpdater::compareVersions("0.2.3-beta.99", "0.2.4-beta", &valid) < 0 && valid,
        "Numeric base components take precedence over beta/stable status and beta sequence numbers.");
    verify(AppUpdater::compareVersions("v0.2.3-beta.10", "0.2.3-beta.2", &valid) > 0 && valid
        && AppUpdater::compareVersions("0.2.3-beta", "0.2.3-beta.0", &valid) < 0 && valid
        && AppUpdater::compareVersions("0.2.3-beta.2", "v0.2.3-beta.2", &valid) == 0 && valid,
        "Beta sequence numbers compare numerically, with an unnumbered beta before numbered betas.");
    for (const auto &value : {"0.2", "01.2.3", "0.2.2+meta", "-1.2.3", "18446744073709551616.0.0",
        "0.2.3-alpha", "0.2.3-rc.1", "0.2.3-BETA", "0.2.3-beta1", "0.2.3-beta.",
        "0.2.3-beta.01", "0.2.3-beta.-1", "0.2.3-beta.1.2", "0.2.3-beta+meta",
        "0.2.3-beta.18446744073709551616", "0.2.3-beta ", "v0.2.3-beta\n"}) {
        AppUpdater::compareVersions(value, "0.2.2", &valid);
        verify(!valid, "Version comparison must reject unsupported or malformed beta suffixes and overflowing numbers.");
    }
    const QByteArray archive("fixture archive bytes");
    const auto object = release(archive);
    AppUpdater::ReleaseInfo info;
    QString message;
    verify(AppUpdater::parseRelease(json(object), &info, &message), "A valid official stable release must parse.");
    verify(info.version == "0.2.2" && info.packageRoot == "PHSRadio-0.2.2-windows-x64",
        "ZIP asset and internal root must use their documented distinct naming conventions.");
    for (const QString &url : {QString("http://github.com/fengsangzbg/PHS-Radio/releases/download/v0.2.2/") + assetName(),
        QString("https://github.com/other/PHS-Radio/releases/download/v0.2.2/") + assetName(),
        releaseUrl(assetName()) + "?download=1", QString("https://github.com.evil.invalid/fengsangzbg/PHS-Radio/releases/download/v0.2.2/") + assetName(),
        QString("https://user:secret@github.com/fengsangzbg/PHS-Radio/releases/download/v0.2.2/") + assetName(),
        releaseUrl(assetName()).replace("v0.2.2", "v9.9.9")}) {
        auto invalid = object;
        auto assets = invalid.value("assets").toArray();
        auto asset = assets[0].toObject(); asset["browser_download_url"] = url; assets[0] = asset; invalid["assets"] = assets;
        verify(!AppUpdater::parseRelease(json(invalid), &info, &message), "Foreign, downgraded or mismatched release URLs must be refused.");
    }
    for (const auto &field : {"draft", "prerelease"}) {
        auto invalid = object; invalid[field] = true;
        verify(!AppUpdater::parseRelease(json(invalid), &info), "Draft and prerelease packages must be rejected.");
    }
    for (const auto &tag : {"v0.2.3-beta", "v0.2.3-beta.1"}) {
        auto invalid = object;
        invalid["tag_name"] = tag;
        invalid["prerelease"] = false;
        verify(!AppUpdater::parseRelease(json(invalid), &info, &message)
            && message.contains(QStringLiteral("正式")),
            "Remote beta tags remain forbidden even when GitHub incorrectly marks the release stable.");
    }
    auto duplicate = object;
    auto assets = duplicate.value("assets").toArray(); assets.append(assets[0]); duplicate["assets"] = assets;
    verify(!AppUpdater::parseRelease(json(duplicate), &info), "Ambiguous matching Windows archives must be rejected.");
    auto missing = object; assets = missing.value("assets").toArray(); assets.removeAt(1); missing["assets"] = assets;
    verify(!AppUpdater::parseRelease(json(missing), &info), "An unsigned-by-checksum archive must never be installed.");
    auto legacy = object; assets = legacy.value("assets").toArray();
    auto checksumAsset = assets[1].toObject(); checksumAsset["name"] = "SHA256SUMS.txt";
    checksumAsset["browser_download_url"] = releaseUrl("SHA256SUMS.txt"); assets[1] = checksumAsset; legacy["assets"] = assets;
    verify(AppUpdater::parseRelease(json(legacy), &info), "The published SHA256SUMS.txt naming convention must remain supported.");
    assets.append(object.value("assets").toArray()[1]); legacy["assets"] = assets;
    verify(!AppUpdater::parseRelease(json(legacy), &info), "Two checksum lists are ambiguous and must be rejected.");
    QByteArray digest;
    const QByteArray sum = checksum(archive).toUpper() + " *" + assetName().toUtf8() + "\r\n";
    verify(AppUpdater::checksumForAsset(sum, assetName(), &digest) && digest == checksum(archive),
        "SHA256SUMS must accept CRLF, uppercase digests and the standard binary marker.");
    verify(!AppUpdater::checksumForAsset(sum + sum, assetName(), &digest), "Repeated hashes for the same filename must be rejected.");
    verify(!AppUpdater::checksumForAsset(checksum(archive) + "  ./" + assetName().toUtf8(), assetName(), &digest),
        "Checksum filenames must match the selected official asset exactly.");
}

void verifyDownload(const QString &cache)
{
    const QByteArray archive(200000, 'z');
    FixtureNetwork network;
    network.responses = {{json(release(archive))}, {checksum(archive) + "  " + assetName().toUtf8() + "\n"},
        {{}, 302, QUrl("https://release-assets.githubusercontent.com/github-production-release-asset/fixture")}, {archive}};
    AppUpdater updater(nullptr, "0.2.1", &network);
    updater.m_cacheRootOverride = cache;
    int available = 0, ready = 0, failures = 0, progressCount = 0;
    QObject::connect(&updater, &AppUpdater::updateAvailable, &updater, [&](const QString &version, const QString &) {
        ++available; verify(version == "0.2.2", "The UI must receive the selected version.");
    });
    QObject::connect(&updater, &AppUpdater::updateReady, &updater, [&](const QString &) { ++ready; });
    QObject::connect(&updater, &AppUpdater::error, &updater, [&](const QString &) { ++failures; });
    QObject::connect(&updater, &AppUpdater::progress, &updater, [&](qint64, qint64) { ++progressCount; });
    updater.checkForUpdates();
    verify(updater.isBusy() && available == 0, "Update checks must return before asynchronous network completion.");
    await([&] { return available || failures; });
    verify(available == 1 && failures == 0 && network.requested.size() == 1,
        "Checking must not silently download or launch an installer.");
    updater.downloadUpdate();
    await([&] { return ready || failures; });
    verify(ready == 1 && failures == 0 && progressCount > 0 && !updater.isBusy(),
        "A valid official redirected archive must report progress and become ready exactly once.");
    QFile file(updater.downloadedArchive());
    verify(file.open(QIODevice::ReadOnly) && file.readAll() == archive,
        "Verified archive bytes must be saved unchanged, including binary contents.");
    verify(!QFile::exists(QDir(updater.m_transactionDir).filePath("installer.ready")),
        "Downloading a package must not apply any installation changes.");
}

void verifyBetaUpdateChecks()
{
    const QByteArray archive("future stable fixture archive");
    auto futureStable = release(archive);
    futureStable["tag_name"] = "v0.2.3";
    auto assets = futureStable.value("assets").toArray();
    for (int index = 0; index < assets.size(); ++index) {
        auto asset = assets[index].toObject();
        asset["name"] = asset.value("name").toString().replace("0.2.2", "0.2.3");
        asset["browser_download_url"] = asset.value("browser_download_url").toString().replace("0.2.2", "0.2.3");
        assets[index] = asset;
    }
    futureStable["assets"] = assets;
    {
        FixtureNetwork network;
        network.responses = {{json(futureStable)}};
        AppUpdater updater(nullptr, "0.2.3-beta", &network);
        int available = 0, failures = 0;
        QObject::connect(&updater, &AppUpdater::updateAvailable, &updater,
            [&](const QString &version, const QString &) {
                verify(version == "0.2.3", "A local beta must offer its subsequent same-version stable release.");
                ++available;
            });
        QObject::connect(&updater, &AppUpdater::error, &updater, [&](const QString &) { ++failures; });
        updater.checkForUpdates();
        await([&] { return available || failures; });
        verify(available == 1 && failures == 0 && updater.latestVersion() == "0.2.3"
            && network.requested.size() == 1,
            "Beta update checks must compare safely with stable releases without downloading automatically.");
    }
    {
        FixtureNetwork network;
        network.responses = {{json(release(archive))}};
        AppUpdater updater(nullptr, "0.2.3-beta.2", &network);
        int current = 0, failures = 0;
        QObject::connect(&updater, &AppUpdater::upToDate, &updater, [&] { ++current; });
        QObject::connect(&updater, &AppUpdater::error, &updater, [&](const QString &) { ++failures; });
        updater.checkForUpdates();
        await([&] { return current || failures; });
        verify(current == 1 && failures == 0 && updater.latestVersion().isEmpty(),
            "A local beta must not downgrade itself to an older-base stable release or report an invalid version.");
    }
}

void verifyFailuresAndCancel(const QString &cache)
{
    const QByteArray expected("correct archive"), corrupt("corrupt archive");
    verify(expected.size() == corrupt.size(), "Corruption fixture must preserve the advertised size.");
    {
        FixtureNetwork network;
        network.responses = {{json(release(expected))}, {checksum(expected) + "  " + assetName().toUtf8()}, {corrupt}};
        AppUpdater updater(nullptr, "0.2.1", &network); updater.m_cacheRootOverride = cache;
        int available = 0, failures = 0, ready = 0;
        QObject::connect(&updater, &AppUpdater::updateAvailable, &updater, [&](const QString &, const QString &) { ++available; });
        QObject::connect(&updater, &AppUpdater::error, &updater, [&](const QString &) { ++failures; });
        QObject::connect(&updater, &AppUpdater::updateReady, &updater, [&](const QString &) { ++ready; });
        updater.checkForUpdates(); await([&] { return available > 0; });
        updater.downloadUpdate(); await([&] { return failures > 0; });
        verify(failures == 1 && ready == 0 && updater.downloadedArchive().isEmpty(),
            "Corrupted bytes must never be marked ready or installed.");
        verify(!QFile::exists(QDir(updater.m_transactionDir).filePath(assetName())),
            "A failed checksum must not commit a downloadable archive.");
    }
    {
        FixtureNetwork network;
        network.responses = {{{}, 302, QUrl("https://evil.invalid/package.zip")}};
        AppUpdater updater(nullptr, "0.2.1", &network);
        int failures = 0;
        QObject::connect(&updater, &AppUpdater::error, &updater, [&](const QString &) { ++failures; });
        updater.checkForUpdates(); await([&] { return failures > 0; });
        verify(network.requested.size() == 1 && failures == 1,
            "An untrusted redirect must be rejected before issuing a request to that host.");
    }
    {
        FixtureNetwork network;
        network.responses = {{json(release(expected)), 200, {}, 100}};
        AppUpdater updater(nullptr, "0.2.1", &network);
        int available = 0, failures = 0;
        QObject::connect(&updater, &AppUpdater::updateAvailable, &updater, [&](const QString &, const QString &) { ++available; });
        QObject::connect(&updater, &AppUpdater::error, &updater, [&](const QString &) { ++failures; });
        updater.checkForUpdates(); updater.cancel();
        QElapsedTimer wait; wait.start();
        while (wait.elapsed() < 150) { QCoreApplication::processEvents(); QThread::msleep(1); }
        verify(!updater.isBusy() && available == 0 && failures == 0,
            "Cancelled requests must not publish stale callbacks or errors later.");
    }
    {
        FixtureNetwork network;
        network.responses = {{json(release(expected))}};
        AppUpdater updater(nullptr, "0.2.2", &network);
        int current = 0;
        QObject::connect(&updater, &AppUpdater::upToDate, &updater, [&] { ++current; });
        updater.checkForUpdates(); await([&] { return current > 0; });
        verify(current == 1 && updater.latestVersion().isEmpty() && network.requested.size() == 1,
            "Equal versions must not offer reinstall or download.");
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir fixture;
    verify(fixture.isValid(), "Updater tests need an isolated temporary directory.");
    verifyParsers();
    verifyDownload(fixture.path());
    verifyBetaUpdateChecks();
    verifyFailuresAndCancel(fixture.path());
    std::puts("App updater parsers, trusted downloads, checksum failures and cancellation passed.");
    return 0;
}
