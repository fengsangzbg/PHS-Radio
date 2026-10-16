#pragma once

#include "music_domain.h"
#include "music_quality.h"

#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <memory>
#include "music_provider.h"

class NeteaseApiClient final : public QObject {
public:
    explicit NeteaseApiClient(QObject *parent = nullptr);
    ~NeteaseApiClient() override;

    void startQrLogin();
    void restoreSession();
    void logout();
    bool isLoggedIn() const;
    const QVector<Playlist> &playlists() const { return m_playlists; }
    void loadPlaylists();
    using TracksCallback = std::function<void(const QVector<Track> &, const QString &)>;
    using LyricsCallback = std::function<void(const TrackLyrics &, const QString &)>;
    using AudioCallback = std::function<void(const QUrl &, const QString &)>;
    void fetchPlaylistTracks(const Playlist &playlist, TracksCallback callback);
    void searchCatalog(const QString &query, int page, CatalogSearchCallback callback);
    void fetchDailyRecommendations(TracksCallback callback);
    void fetchLyrics(const Track &track, LyricsCallback callback);
    void fetchAudioUrl(const Track &track, AudioCallback callback);
    void fetchAudioUrl(const Track &track, AudioQuality quality, AudioCallback callback);
    void setAudioQuality(AudioQuality quality) { m_audioQuality = quality; }
    AudioQuality audioQuality() const { return m_audioQuality; }

    std::function<void(const QString &)> onStatusChanged;
    std::function<void(const QString &)> onError;
    std::function<void(const QImage &)> onQrCodeReady;
    std::function<void(bool)> onLoginChanged;
    std::function<void(const QVector<Playlist> &)> onPlaylistsReady;

private:
    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;
    struct TrackFetchState;
    void requestPlaylistPage(int generation, int revision, int offset, QVector<Playlist> playlists);
    void requestTrackBatch(const std::shared_ptr<TrackFetchState> &state);
    void finishTrackFetch(const std::shared_ptr<TrackFetchState> &state, const QString &error);
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

    AudioQuality m_audioQuality = AudioQuality::Standard;
    QNetworkAccessManager m_network;
    QProcess m_serviceProcess;
    QTimer m_qrTimer;
    QString m_serviceRoot;
    QString m_qrKey;
    QString m_cookie;
    QString m_userId;
    QVector<Playlist> m_playlists;
    QSet<QString> m_loadedPlaylists;
    int m_libraryRevision = 0;
    int m_generation = 0;
    bool m_loggedIn = false;
    bool m_loginInProgress = false;
    bool m_pollPending = false;
    bool m_serviceStartedByApp = false;
};
