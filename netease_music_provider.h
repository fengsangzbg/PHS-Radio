#pragma once

#include "music_provider.h"
#include <QPointer>

class NeteaseApiClient;

// This provider never substitutes example playlists for an authorized account.
class NeteaseMusicProvider final : public MusicProvider {
public:
    explicit NeteaseMusicProvider(NeteaseApiClient *client);
    MusicPlatform platform() const override;
    QVector<Playlist> playlists() const override;
    QVector<Track> likedTracks() const override;
    QVector<Track> search(const QString &query) const override;
    void searchCatalog(const QString &query, int page, CatalogSearchCallback callback) const override;
    void fetchDailyRecommendations(RecommendationsCallback callback) const override;
    void fetchLyrics(const Track &track, LyricsCallback callback) const override;
private:
    QPointer<NeteaseApiClient> m_client;
};
