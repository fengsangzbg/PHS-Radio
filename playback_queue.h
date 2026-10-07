#pragma once

#include "music_domain.h"

// Owns the playback context independently of the currently displayed search or
// playlist. Returned Track pointers stay valid until the next setTracks call.
class PlaybackQueue {
public:
    // Copies the list and clamps selectedIndex for a non-empty list. Replacing
    // the context resets shuffle traversal and playback history, but not mode.
    void setTracks(const QVector<Track> &tracks, int selectedIndex);

    bool empty() const;
    const Track *current() const;
    int currentIndex() const;
    int size() const;

    // Switching mode keeps the current song and starts a fresh traversal.
    void setShuffle(bool shuffle);
    bool isShuffle() const;

    // Sequential manual navigation wraps. Automatic advancement stops at the
    // final song. Shuffle visits every position once per round and does not
    // immediately repeat the last song across rounds when there are 2+ songs.
    // An automatic advance of a one-song queue stops in either mode.
    const Track *next(bool automatic = false);

    // Sequential navigation wraps backwards. Shuffle goes back through actual
    // playback history; at the beginning it returns the current song.
    const Track *previous();

private:
    void resetTraversal();
    void refillShuffleBag(bool includeCurrent);

    QVector<Track> m_tracks;
    int m_currentIndex = -1;
    bool m_shuffle = false;
    QVector<int> m_shuffleBag;
    QVector<int> m_history;
    int m_historyPosition = -1;
};
