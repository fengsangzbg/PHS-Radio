#include "mock_music_provider.h"

#include <algorithm>

MockMusicProvider::MockMusicProvider(MusicPlatform platform)
    : m_platform(platform)
{
    const Track nightDrive{QStringLiteral("t1"), QStringLiteral("夜行列车"), QStringLiteral("林间回声"), QStringLiteral("城市以北"), {}};
    const Track morningLight{QStringLiteral("t2"), QStringLiteral("清晨来信"), QStringLiteral("白色房间"), QStringLiteral("日常片段"), {}};
    const Track blueHour{QStringLiteral("t3"), QStringLiteral("蓝色时刻"), QStringLiteral("远岸"), QStringLiteral("潮汐"), {}};

    m_playlists = {
        {QStringLiteral("p1"), QStringLiteral("收藏的歌单 · 夜色漫游"), PlaylistKind::Favorite, {}, {nightDrive, blueHour}},
        {QStringLiteral("p2"), QStringLiteral("我创建的 · 慢速清晨"), PlaylistKind::Created, {}, {morningLight, nightDrive}}
    };
    m_likedTracks = {nightDrive, morningLight};
}

MusicPlatform MockMusicProvider::platform() const
{
    return m_platform;
}

QVector<Playlist> MockMusicProvider::playlists() const
{
    return m_playlists;
}

QVector<Track> MockMusicProvider::likedTracks() const
{
    return m_likedTracks;
}

QVector<Track> MockMusicProvider::search(const QString &query) const
{
    if (!m_searchIndex) {
        QVector<Playlist> lists = m_playlists;
        Playlist liked;
        liked.tracks = m_likedTracks;
        lists.prepend(liked);
        m_searchIndex = std::make_shared<MusicSearchIndex>(allPlaylistTracks(lists));
    }
    return m_searchIndex->search(query);
}

