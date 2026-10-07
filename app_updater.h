#pragma once

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QUrl>
#include <functional>
#include <memory>

class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
class QTimer;

// Only official stable releases from fengsangzbg/PHS-Radio are accepted.
// Downloads run asynchronously. Installation runs in the bundled fixed helper;
// installerStarted means its preflight succeeded and the caller should quit normally.
class AppUpdater final : public QObject {
    Q_OBJECT
public:
    struct ReleaseInfo {
        QString version;
        QString tag;
        QString notes;
        QString assetName;
        QString packageRoot;
        QUrl archiveUrl;
        QUrl checksumsUrl;
        qint64 assetSize = 0;
        QByteArray apiDigest;
    };

    explicit AppUpdater(QObject *parent = nullptr, const QString &currentVersion = {},
                        QNetworkAccessManager *network = nullptr);
    ~AppUpdater() override;
    bool isBusy() const;
    QString latestVersion() const { return m_release.version; }
    QString downloadedArchive() const { return m_archivePath; }
    QString installerLogPath() const;
    void checkForUpdates();
    void downloadUpdate();
    // Starts the trusted local installer; wait for installerStarted before quitting.
    bool launchInstaller();
    void cancel();

    // Pure parsers are public so release tooling and offline tests use the same rules.
    static int compareVersions(const QString &left, const QString &right, bool *valid = nullptr);
    static bool parseRelease(const QByteArray &json, ReleaseInfo *release, QString *error = nullptr);
    static bool checksumForAsset(const QByteArray &text, const QString &assetName,
                                 QByteArray *sha256, QString *error = nullptr);
    static bool isOfficialDownloadUrl(const QUrl &url);

signals:
    void statusChanged(const QString &status);
    void progress(qint64 received, qint64 total);
    void updateAvailable(const QString &version, const QString &notes);
    void updateReady(const QString &version);
    void upToDate();
    void error(const QString &message);
    void installerStarted();

private:
    enum class State { Idle, Checking, Available, Downloading, Ready, Launching, Launched };
    void fail(const QString &message);
    void fetch(const QUrl &url, qint64 limit, bool archive,
               std::function<void(const QByteArray &)> done, int redirects = 0);
    bool prepareTransaction();
    static bool trustedTransferUrl(const QUrl &url);
    QString m_currentVersion;
    // Tests inject a temporary root without touching the real user's application cache.
    QString m_cacheRootOverride;
    QPointer<QNetworkAccessManager> m_network;
    QPointer<QNetworkReply> m_reply;
    ReleaseInfo m_release;
    State m_state = State::Idle;
    QString m_transactionDir;
    QString m_archivePath;
    QByteArray m_expectedDigest;
    std::unique_ptr<QSaveFile> m_download;
    std::unique_ptr<QCryptographicHash> m_hash;
    QTimer *m_installerPoll = nullptr;
    int m_pollCount = 0;
    quint64 m_generation = 0;
};
