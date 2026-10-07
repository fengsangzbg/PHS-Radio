#pragma once

#include "music_provider.h"
#include "music_search.h"
#include <memory>

class MockMusicProvider final : public MusicProvider {
public:
    explicit MockMusicProvider(MusicPlatform platform);

    MusicPlatform platform() const override;
    QVector<Playlist> playlists() const override;
    QVector<Track> likedTracks() const override;
    QVector<Track> search(const QString &query) const override;

private:
    MusicPlatform m_platform;
    QVector<Playlist> m_playlists;
    QVector<Track> m_likedTracks;
    mutable std::shared_ptr<MusicSearchIndex> m_searchIndex;
};
