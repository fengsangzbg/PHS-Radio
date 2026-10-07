#include "../playback_queue.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSet>
#include <cstdlib>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

QVector<Track> tracks(int count)
{
    QVector<Track> result;
    for (int i = 0; i < count; ++i) {
        Track track;
        track.id = QString::number(i);
        track.title = QStringLiteral("Song %1").arg(i);
        track.artist = QStringLiteral("Artist %1").arg(i);
        result.append(track);
    }
    return result;
}

void verifySequentialPlayback()
{
    PlaybackQueue queue;
    queue.setTracks(tracks(4), 1);
    check(queue.size() == 4 && !queue.empty() && !queue.isShuffle(),
          "A new queue must copy all songs and use sequential mode.");
    check(queue.current()->id == QStringLiteral("1"), "Playback must start at the selected song.");
    check(queue.next(true)->id == QStringLiteral("2"), "Automatic playback must advance in order.");
    check(queue.next(true)->id == QStringLiteral("3"), "Automatic playback must reach the last song.");
    check(!queue.next(true) && queue.currentIndex() == 3,
          "The end of a sequential queue must stop and keep the final song selected.");
    check(queue.next()->id == QStringLiteral("0"), "Manual next must wrap to the beginning.");
    check(queue.previous()->id == QStringLiteral("3"), "Manual previous must wrap to the end.");
    check(queue.previous()->id == QStringLiteral("2"), "Manual previous must follow track order.");
}

void verifyIndependentSnapshot()
{
    QVector<Track> displayed = tracks(5);
    PlaybackQueue queue;
    queue.setTracks(displayed, 2);
    displayed[2].title = QStringLiteral("Replaced metadata");
    displayed.removeAt(3);
    displayed.clear();
    check(queue.size() == 5 && queue.current()->title == QStringLiteral("Song 2"),
          "Filtering or reloading the displayed list must not replace the playback context.");
    check(queue.next()->id == QStringLiteral("3"),
          "The next song must come from the original context after a view change.");
}

void verifyShuffleRounds()
{
    // Multiple real random rounds check the contract without depending on any
    // particular random order or a seeded replacement of QRandomGenerator.
    for (const int count : {2, 3, 17, 43}) {
        PlaybackQueue queue;
        queue.setTracks(tracks(count), count / 2);
        queue.setShuffle(true);
        const int selected = queue.currentIndex();
        QSet<int> firstRound{selected};
        int previous = selected;
        for (int i = 1; i < count; ++i) {
            check(queue.next(true), "Shuffle must continue within the first round.");
            const int index = queue.currentIndex();
            check(index != previous && !firstRound.contains(index),
                  "Shuffle must visit every queue position once before repeating.");
            firstRound.insert(index);
            previous = index;
        }
        check(firstRound.size() == count, "The first shuffle round must include the starting song.");

        for (int round = 0; round < 20; ++round) {
            QSet<int> visited;
            for (int i = 0; i < count; ++i) {
                check(queue.next(true), "Shuffle must continue into new rounds.");
                const int index = queue.currentIndex();
                check(index != previous, "Shuffle must avoid an immediate repeat across rounds.");
                check(!visited.contains(index), "A complete shuffle round must contain no duplicate position.");
                visited.insert(index);
                previous = index;
            }
            check(visited.size() == count, "Shuffle must not drop any queue position in later rounds.");
        }
    }
}

void verifyShuffleHistory()
{
    PlaybackQueue queue;
    queue.setTracks(tracks(8), 3);
    queue.setShuffle(true);
    QVector<int> played{queue.currentIndex()};
    for (int i = 0; i < 15; ++i) {
        queue.next();
        played.append(queue.currentIndex());
    }
    for (int i = int(played.size()) - 2; i >= 0; --i) {
        check(queue.previous() && queue.currentIndex() == played[i],
              "Shuffle previous must revisit actual playback history even across round boundaries.");
    }
    check(queue.previous() && queue.currentIndex() == played.first(),
          "Previous before the beginning of shuffle history must keep the current song.");
    for (int i = 1; i < played.size(); ++i) {
        check(queue.next() && queue.currentIndex() == played[i],
              "Next after going backwards must follow the original playback history.");
    }
    const int oldCurrent = queue.currentIndex();
    check(queue.next() && queue.currentIndex() != oldCurrent,
          "Playback must resume choosing fresh songs after replaying saved history.");
}

void verifyModeAndContextChanges()
{
    PlaybackQueue queue;
    queue.setTracks(tracks(6), 4);
    queue.setShuffle(true);
    check(queue.isShuffle() && queue.currentIndex() == 4,
          "Enabling shuffle must keep the current song.");
    queue.next();
    const int shuffled = queue.currentIndex();
    queue.setShuffle(false);
    check(!queue.isShuffle() && queue.currentIndex() == shuffled,
          "Disabling shuffle must keep the current song.");
    queue.next();
    check(queue.currentIndex() == (shuffled + 1) % 6,
          "Sequential mode must resume from the actual current position.");

    queue.setShuffle(true);
    queue.setTracks(tracks(3), 2);
    check(queue.isShuffle() && queue.size() == 3 && queue.currentIndex() == 2,
          "A new playback context must retain the selected playback mode.");
    check(queue.previous()->id == QStringLiteral("2"),
          "Replacing a context must discard history from the previous list.");
    check(queue.next()->id != QStringLiteral("2"),
          "A new shuffle context must count its selected song as already visited.");

    QVector<Track> repeated = tracks(3);
    repeated[1] = repeated[0];
    queue.setTracks(repeated, 0);
    QSet<int> positions{queue.currentIndex()};
    queue.next();
    positions.insert(queue.currentIndex());
    queue.next();
    positions.insert(queue.currentIndex());
    check(positions.size() == 3,
          "Repeated songs in a playlist must remain separate playable queue positions.");
}

void verifyEmptyAndSingleSong()
{
    PlaybackQueue queue;
    check(queue.empty() && queue.size() == 0 && queue.currentIndex() == -1,
          "An uninitialized queue must be empty.");
    check(!queue.current() && !queue.next() && !queue.next(true) && !queue.previous(),
          "Navigation of an empty queue must be safe.");
    queue.setShuffle(true);
    queue.setTracks(tracks(1), -8);
    check(queue.currentIndex() == 0 && queue.current(),
          "An out-of-range selected index must clamp to a valid song.");
    check(!queue.next(true), "An automatic advance of a one-song queue must stop.");
    check(queue.next() && queue.currentIndex() == 0 && queue.previous(),
          "Manual navigation may replay a one-song queue.");
    queue.setShuffle(false);
    check(!queue.next(true) && queue.next() && queue.previous(),
          "One-song sequential playback must stop automatically and support manual replay.");
    queue.setTracks(tracks(2), 99);
    check(queue.currentIndex() == 1, "A selected index past the end must clamp to the final song.");
    queue.setTracks({}, 0);
    check(queue.empty() && queue.currentIndex() == -1 && !queue.current()
              && !queue.next() && !queue.previous(),
          "Clearing a populated queue must clear its selection and navigation state.");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    verifySequentialPlayback();
    verifyIndependentSnapshot();
    verifyShuffleRounds();
    verifyShuffleHistory();
    verifyModeAndContextChanges();
    verifyEmptyAndSingleSong();
    qInfo("PlaybackQueue tests passed.");
    return 0;
}
