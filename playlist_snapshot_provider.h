#pragma once

#include "music_provider.h"
#include "music_search.h"
#include <QPointer>
#include <memory>

class KugouApiClient;

class PlaylistSnapshotProvider final : public MusicProvider {
public:
    PlaylistSnapshotProvider(MusicPlatform platform, QVector<Playlist> playlists,
                             KugouApiClient *catalogClient = nullptr);

    MusicPlatform platform() const override;
    QVector<Playlist> playlists() const override;
    QVector<Track> likedTracks() const override;
    QVector<Track> search(const QString &query) const override;
    void searchCatalog(const QString &query, int page, CatalogSearchCallback callback) const override;
    void fetchDailyRecommendations(RecommendationsCallback callback) const override;
    void fetchLyrics(const Track &track, LyricsCallback callback) const override;

private:
    MusicPlatform m_platform;
    QVector<Playlist> m_playlists;
    QPointer<KugouApiClient> m_catalogClient;
    mutable std::shared_ptr<MusicSearchIndex> m_searchIndex;
};
