#pragma once

#include "lyric_domain.h"

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QSet>
#include <QHash>

enum class MusicPlatform {
    QQMusic,
    NetEaseCloud,
    Kugou
};

enum class PlaylistKind {
    Favorite,
    Created
};

struct Track {
    QString id;
    QString title;
    QString artist;
    QString album;
    QUrl audioUrl;
    QUrl coverUrl;
    QString hash;
    QString albumId;
    QString albumAudioId;
    QStringList searchAliases;
};

struct CatalogTrack {
    Track track;
    MusicPlatform platform = MusicPlatform::Kugou;
};

struct CatalogSearchPage {
    QVector<CatalogTrack> tracks;
    int page = 1;
    int pageSize = 30;
    int total = -1;
    bool hasMore = false;
};

struct Playlist {
    QString id;
    QString name;
    PlaylistKind kind = PlaylistKind::Favorite;
    QUrl coverUrl;
    QVector<Track> tracks;
    QString externalId;
    bool isLiked = false;
    int expectedTrackCount = -1;
    QString originalId;
    int source = 0;
    QString originalAlbumId;
};

inline QString platformName(MusicPlatform platform)
{
    switch (platform) {
    case MusicPlatform::QQMusic: return QStringLiteral("QQ 音乐");
    case MusicPlatform::NetEaseCloud: return QStringLiteral("网易云音乐");
    case MusicPlatform::Kugou: return QStringLiteral("酷狗音乐");
    }
    return {};
}

inline QString trackKey(const Track &track)
{
    if (!track.albumAudioId.isEmpty() && track.albumAudioId != QStringLiteral("0"))
        return QStringLiteral("audio:") + track.albumAudioId;
    if (!track.hash.isEmpty())
        return QStringLiteral("hash:") + track.hash.toLower();
    if (!track.id.isEmpty())
        return QStringLiteral("id:") + track.id.toLower();
    return QStringLiteral("text:%1|%2|%3").arg(track.title, track.artist, track.album);
}

inline QVector<Track> allPlaylistTracks(const QVector<Playlist> &playlists)
{
    QVector<Track> tracks;
    QHash<QString, int> positions;
    for (const Playlist &playlist : playlists) {
        for (const Track &track : playlist.tracks) {
            const QString key = trackKey(track);
            const auto existing = positions.constFind(key);
            if (existing != positions.cend()) {
                // The same recording may carry alternate names in another playlist.
                Track &saved = tracks[existing.value()];
                QStringList aliases = track.searchAliases;
                aliases += QStringList{track.title, track.artist, track.album};
                for (const QString &alias : aliases)
                    if (!alias.trimmed().isEmpty() && alias != saved.title && alias != saved.artist
                        && alias != saved.album && !saved.searchAliases.contains(alias))
                        saved.searchAliases.append(alias);
                continue;
            }
            positions.insert(key, tracks.size());
            tracks.push_back(track);
        }
    }
    return tracks;
}

inline bool isLikedPlaylist(const Playlist &playlist)
{
    const QString name = playlist.name.trimmed();
    return playlist.isLiked || name == QStringLiteral("我喜欢") || name == QStringLiteral("我喜欢的歌曲")
        || name == QStringLiteral("我喜欢的音乐") || name == QStringLiteral("喜欢的音乐")
        || name == QStringLiteral("红心歌曲") || name == QStringLiteral("我的收藏")
        || name == QStringLiteral("默认收藏");
}
