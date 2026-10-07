#pragma once

#include "music_domain.h"

#include <QString>
#include <QVector>
#include <functional>

using CatalogSearchCallback = std::function<void(const CatalogSearchPage &, const QString &)>;
using RecommendationsCallback = std::function<void(const QVector<Track> &, const QString &)>;
using LyricsCallback = std::function<void(const TrackLyrics &, const QString &)>;

class MusicProvider {
public:
    using CatalogSearchCallback = ::CatalogSearchCallback;
    using RecommendationsCallback = ::RecommendationsCallback;
    using LyricsCallback = ::LyricsCallback;
    virtual ~MusicProvider() = default;
    virtual MusicPlatform platform() const = 0;
    virtual QVector<Playlist> playlists() const = 0;
    virtual QVector<Track> likedTracks() const = 0;
    virtual QVector<Track> search(const QString &query) const = 0;
    virtual void searchCatalog(const QString &query, int page, CatalogSearchCallback callback) const
    {
        Q_UNUSED(query)
        CatalogSearchPage result;
        result.page = qMax(1, page);
        callback(result, platformName(platform()) + QStringLiteral("在线曲库搜索尚未接入。"));
    }
    virtual void fetchDailyRecommendations(RecommendationsCallback callback) const
    {
        callback({}, platformName(platform()) + QStringLiteral("每日推荐尚未接入。"));
    }
    virtual void fetchLyrics(const Track &track, LyricsCallback callback) const
    {
        Q_UNUSED(track)
        callback({}, platformName(platform()) + QStringLiteral("歌词服务尚未接入。"));
    }
};
