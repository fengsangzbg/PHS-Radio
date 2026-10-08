#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QVideoFrame>
#include <utility>

// The backend can produce frames while the GUI is busy. Store one implicitly
// shared frame directly instead of queuing an event (and a GPU resource) for
// every source frame. Consumers retain only the newest work; presentation uses
// a display budget and plain RGB conversion uses one bounded worker job. Qt fallback
// conversion and widget presentation remain on the GUI thread.
// A sink callback must capture a shared_ptr to this buffer, never a widget.
class LatestVideoFrame final {
public:
    void reset(bool enabled)
    {
        QVideoFrame previous;
        {
            QMutexLocker lock(&m_mutex);
            m_enabled = enabled;
            previous = std::exchange(m_frame, QVideoFrame{});
        }
    }

    void offer(const QVideoFrame &frame)
    {
        if (!frame.isValid())
            return;
        QVideoFrame previous;
        {
            QMutexLocker lock(&m_mutex);
            if (!m_enabled)
                return;
            previous = std::exchange(m_frame, frame);
        }
        // Release displaced decoder/GPU resources outside the buffer's lock.
    }

    QVideoFrame take()
    {
        QMutexLocker lock(&m_mutex);
        return std::exchange(m_frame, QVideoFrame{});
    }

    template<typename Predicate>
    QVideoFrame takeIf(Predicate &&accepts)
    {
        QMutexLocker lock(&m_mutex);
        // Keep testing and consuming one atomic operation. A rejected frame
        // stays available to the GUI fallback; taking and offering it back
        // could overwrite a newer frame arriving concurrently.
        // The predicate must only inspect metadata: no mapping, callbacks,
        // conversion, or re-entry into this mailbox while its mutex is held.
        if (!m_frame.isValid() || !std::forward<Predicate>(accepts)(std::as_const(m_frame)))
            return {};
        return std::exchange(m_frame, QVideoFrame{});
    }

private:
    QMutex m_mutex;
    QVideoFrame m_frame;
    bool m_enabled = false;
};
