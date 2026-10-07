#include "playback_queue.h"

#include <QRandomGenerator>
#include <algorithm>

void PlaybackQueue::setTracks(const QVector<Track> &tracks, int selectedIndex)
{
    m_tracks = tracks;
    m_currentIndex = m_tracks.isEmpty()
        ? -1 : std::clamp(selectedIndex, 0, int(m_tracks.size()) - 1);
    resetTraversal();
}

bool PlaybackQueue::empty() const
{
    return m_tracks.isEmpty();
}

const Track *PlaybackQueue::current() const
{
    return m_currentIndex >= 0 && m_currentIndex < m_tracks.size()
        ? &m_tracks[m_currentIndex] : nullptr;
}

int PlaybackQueue::currentIndex() const
{
    return m_currentIndex;
}

int PlaybackQueue::size() const
{
    return int(m_tracks.size());
}

void PlaybackQueue::setShuffle(bool shuffle)
{
    if (m_shuffle == shuffle)
        return;
    m_shuffle = shuffle;
    resetTraversal();
}

bool PlaybackQueue::isShuffle() const
{
    return m_shuffle;
}

const Track *PlaybackQueue::next(bool automatic)
{
    if (empty() || (automatic && size() == 1))
        return nullptr;

    if (!m_shuffle) {
        if (m_currentIndex + 1 >= size()) {
            if (automatic)
                return nullptr;
            m_currentIndex = 0;
        } else {
            ++m_currentIndex;
        }
        return current();
    }

    // Going forward after "previous" follows the same played songs rather than
    // consuming a second random choice and skipping the original successor.
    if (m_historyPosition + 1 < m_history.size()) {
        m_currentIndex = m_history[++m_historyPosition];
        return current();
    }

    if (m_shuffleBag.isEmpty())
        refillShuffleBag(true);
    if (m_shuffleBag.isEmpty())
        return nullptr;

    m_currentIndex = m_shuffleBag.takeLast();
    m_history.append(m_currentIndex);
    m_historyPosition = int(m_history.size()) - 1;
    return current();
}

const Track *PlaybackQueue::previous()
{
    if (empty())
        return nullptr;
    if (!m_shuffle) {
        m_currentIndex = m_currentIndex > 0 ? m_currentIndex - 1 : size() - 1;
    } else if (m_historyPosition > 0) {
        m_currentIndex = m_history[--m_historyPosition];
    }
    return current();
}

void PlaybackQueue::resetTraversal()
{
    m_shuffleBag.clear();
    m_history.clear();
    m_historyPosition = -1;
    if (empty())
        return;

    m_history.append(m_currentIndex);
    m_historyPosition = 0;
    // The selected song already accounts for one item in the first round.
    if (m_shuffle)
        refillShuffleBag(false);
}

void PlaybackQueue::refillShuffleBag(bool includeCurrent)
{
    m_shuffleBag.clear();
    m_shuffleBag.reserve(size());
    for (int i = 0; i < size(); ++i)
        if (includeCurrent || i != m_currentIndex)
            m_shuffleBag.append(i);

    auto *random = QRandomGenerator::global();
    for (int i = int(m_shuffleBag.size()) - 1; i > 0; --i)
        std::swap(m_shuffleBag[i], m_shuffleBag[random->bounded(i + 1)]);

    // takeLast() is the next choice. Restrict only the first position of a new
    // round; the rest still includes each song exactly once.
    if (m_shuffleBag.size() > 1 && m_shuffleBag.last() == m_currentIndex) {
        const int alternative = random->bounded(int(m_shuffleBag.size()) - 1);
        std::swap(m_shuffleBag.last(), m_shuffleBag[alternative]);
    }
}
