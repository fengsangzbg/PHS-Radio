#pragma once

#include "music_domain.h"

// Platform lookup results can only identify recordings already present in this source.
QVector<Track> mergeLibrarySearchMatches(const QVector<Track> &source, const QVector<Track> &localMatches,
                                        const QVector<Track> &platformMatches);

// A reusable local index: normalization and transliteration happen once per field.
class MusicSearchIndex final {
public:
    explicit MusicSearchIndex(QVector<Track> tracks);
    QVector<Track> search(const QString &query) const;

private:
    struct Entry {
        Track track;
        QStringList names;
    };
    QVector<Entry> m_entries;
};
