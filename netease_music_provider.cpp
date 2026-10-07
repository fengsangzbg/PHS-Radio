#include "netease_music_provider.h"
#include "netease_api_client.h"

NeteaseMusicProvider::NeteaseMusicProvider(NeteaseApiClient *client) : m_client(client) {}
MusicPlatform NeteaseMusicProvider::platform() const { return MusicPlatform::NetEaseCloud; }
QVector<Playlist> NeteaseMusicProvider::playlists() const { return {}; }
QVector<Track> NeteaseMusicProvider::likedTracks() const { return {}; }
QVector<Track> NeteaseMusicProvider::search(const QString &query) const { Q_UNUSED(query) return {}; }
void NeteaseMusicProvider::fetchDailyRecommendations(RecommendationsCallback callback) const
{
    if (!m_client)
        callback({}, QStringLiteral("网易云音乐账号未连接，请先扫码登录。"));
    else
        m_client->fetchDailyRecommendations(std::move(callback));
}
void NeteaseMusicProvider::fetchLyrics(const Track &track, LyricsCallback callback) const
{
    if (!m_client)
        callback({}, QStringLiteral("网易云歌词服务未连接。"));
    else
        m_client->fetchLyrics(track, std::move(callback));
}
