#pragma once

#include "music_domain.h"

#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

class KugouApiClient final : public QObject {
public:
    explicit KugouApiClient(QObject *parent = nullptr);
    ~KugouApiClient() override;

    void startQrLogin();
    void restoreSession();
    void fetchPlaylists();
    using TracksCallback = std::function<void(const QVector<Track> &, const QString &)>;
    void fetchPlaylistTracks(const Playlist &playlist, TracksCallback callback);
    void searchTrackNames(const QString &query, TracksCallback callback);
    using CatalogSearchCallback = std::function<void(const CatalogSearchPage &, const QString &)>;
    void searchCatalog(const QString &query, int page, CatalogSearchCallback callback);
    void fetchDailyRecommendations(TracksCallback callback);
    using LyricsCallback = std::function<void(const TrackLyrics &, const QString &)>;
    void fetchLyrics(const Track &track, LyricsCallback callback);
    using AudioCallback = std::function<void(const QUrl &, const QString &)>;
    void fetchAudioUrl(const Track &track, AudioCallback callback);
    void fetchTrackCover(const Track &track, std::function<void(const QUrl &)> callback);
    void fetchPlaylistCover(const Playlist &playlist, std::function<void(const QUrl &)> callback);
    QVector<Playlist> playlists() const;

    std::function<void(const QString &)> onStatusChanged;
    std::function<void(const QString &)> onError;
    std::function<void(const QImage &)> onQrCodeReady;
    std::function<void(const QVector<Playlist> &)> onPlaylistsReady;

private:
    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;

    void ensureService(std::function<void()> ready);
    void startLocalService();
    void requestJson(const QString &path, const QJsonObject &body,
                     const QString &authorization, JsonCallback callback);
    void requestQrKey();
    void requestQrImage();
    void pollQrStatus();
    void setAccountSession(const QString &token, const QString &userId);
    void ensurePlaybackDevice(std::function<void(const QString &)> callback);
    void registerPlaybackDevice(int attempt, quint64 generation);
    void requestUserPlaylists(int page = 1, QVector<Playlist> accumulated = {});
    void fetchPlaylistCandidate(Playlist playlist, int candidate, QVector<Track> best,
                                TracksCallback callback);
    void requestPlaylistTracks(Playlist playlist, int page, QVector<Track> tracks,
                               TracksCallback callback, bool publicEndpoint = false,
                               QByteArray previousPage = {}, int retry = 0, int pageSize = 100);
    void reportStatus(const QString &status) const;
    void reportError(const QString &error) const;

    QNetworkAccessManager m_network;
    QNetworkAccessManager m_playbackNetwork;
    QNetworkAccessManager m_libraryNetwork;
    QVector<std::function<void(const QString &)>> m_deviceWaiters;
    QProcess m_serviceProcess;
    QTimer m_qrTimer;
    QVector<Playlist> m_playlists;
    QString m_serviceRoot;
    QString m_qrKey;
    QString m_token;
    QString m_userId;
    QString m_authorization;
    QString m_deviceId;
    quint64 m_accountGeneration = 0;
    quint64 m_qrGeneration = 0;
    bool m_serviceStartedByApp = false;
    bool m_loginInProgress = false;
};
