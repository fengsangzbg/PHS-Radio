#pragma once

#include "music_domain.h"

#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>

class NeteaseApiClient final : public QObject {
public:
    explicit NeteaseApiClient(QObject *parent = nullptr);
    ~NeteaseApiClient() override;

    void startQrLogin();
    void restoreSession();
    void logout();
    bool isLoggedIn() const;
    using TracksCallback = std::function<void(const QVector<Track> &, const QString &)>;
    using LyricsCallback = std::function<void(const TrackLyrics &, const QString &)>;
    using AudioCallback = std::function<void(const QUrl &, const QString &)>;
    void fetchDailyRecommendations(TracksCallback callback);
    void fetchLyrics(const Track &track, LyricsCallback callback);
    void fetchAudioUrl(const Track &track, AudioCallback callback);

    std::function<void(const QString &)> onStatusChanged;
    std::function<void(const QString &)> onError;
    std::function<void(const QImage &)> onQrCodeReady;
    std::function<void(bool)> onLoginChanged;

private:
    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;
    void ensureService(std::function<void(const QString &)> callback);
    void startLocalService();
    void requestJson(const QString &path, const QJsonObject &body,
                     const QString &cookie, JsonCallback callback);
    void requestQrKey(int generation);
    void requestQrImage(int generation);
    void pollQrStatus();
    void validateSession(int generation, bool persist);
    void setLoggedIn(bool loggedIn);
    void reportStatus(const QString &status) const;
    void reportError(const QString &error) const;

    QNetworkAccessManager m_network;
    QProcess m_serviceProcess;
    QTimer m_qrTimer;
    QString m_serviceRoot;
    QString m_qrKey;
    QString m_cookie;
    QString m_userId;
    int m_generation = 0;
    bool m_loggedIn = false;
    bool m_loginInProgress = false;
    bool m_pollPending = false;
    bool m_serviceStartedByApp = false;
};
