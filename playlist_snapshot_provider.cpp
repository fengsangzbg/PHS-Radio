#include "playlist_snapshot_provider.h"
#include "kugou_api_client.h"

PlaylistSnapshotProvider::PlaylistSnapshotProvider(MusicPlatform platform, QVector<Playlist> playlists,
                                                 KugouApiClient *catalogClient)
    : m_platform(platform), m_playlists(std::move(playlists)), m_catalogClient(catalogClient)
{
}

MusicPlatform PlaylistSnapshotProvider::platform() const
{
    return m_platform;
}

QVector<Playlist> PlaylistSnapshotProvider::playlists() const
{
    return m_playlists;
}

QVector<Track> PlaylistSnapshotProvider::likedTracks() const
{
    return allPlaylistTracks(m_playlists);
}

QVector<Track> PlaylistSnapshotProvider::search(const QString &query) const
{
    if (!m_searchIndex)
        m_searchIndex = std::make_shared<MusicSearchIndex>(allPlaylistTracks(m_playlists));
    return m_searchIndex->search(query);
}

void PlaylistSnapshotProvider::searchCatalog(const QString &query, int page,
                                           CatalogSearchCallback callback) const
{
    if (m_platform != MusicPlatform::Kugou) {
        MusicProvider::searchCatalog(query, page, std::move(callback));
        return;
    }
    if (!m_catalogClient) {
        CatalogSearchPage result;
        result.page = qMax(1, page);
        callback(result, QStringLiteral("酷狗在线曲库服务未连接，请先连接酷狗账号。"));
        return;
    }
    m_catalogClient->searchCatalog(query, page, std::move(callback));
}

void PlaylistSnapshotProvider::fetchDailyRecommendations(RecommendationsCallback callback) const
{
    if (m_platform != MusicPlatform::Kugou) {
        MusicProvider::fetchDailyRecommendations(std::move(callback));
    } else if (!m_catalogClient) {
        callback({}, QStringLiteral("酷狗每日推荐服务未连接，请先连接酷狗账号。"));
    } else {
        m_catalogClient->fetchDailyRecommendations(std::move(callback));
    }
}

void PlaylistSnapshotProvider::fetchLyrics(const Track &track, LyricsCallback callback) const
{
    if (m_platform != MusicPlatform::Kugou) {
        MusicProvider::fetchLyrics(track, std::move(callback));
    } else if (!m_catalogClient) {
        callback({}, QStringLiteral("酷狗歌词服务未连接。"));
    } else {
        m_catalogClient->fetchLyrics(track, std::move(callback));
    }
}
