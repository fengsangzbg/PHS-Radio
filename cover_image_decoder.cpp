#include "cover_image_decoder.h"

#include <QBuffer>
#include <QImageIOHandler>
#include <QImageReader>
#include <QMetaObject>
#include <atomic>

namespace {
constexpr int WorkerCount = 2;
constexpr int MaximumOutstanding = 128;
constexpr qint64 MaximumEncodedBytes = 16 * 1024 * 1024;
// Pending bytes exclude the at most two active files (each <=16 MiB).
constexpr qint64 MaximumQueuedBytes = 64 * 1024 * 1024;
constexpr qint64 MaximumImagePixels = 40000000;
}

struct CoverImageDecoder::Lifetime {
    std::atomic_bool cancelled{false};
};

CoverImageDecoder::CoverImageDecoder(QObject *parent)
    : QObject(parent), m_lifetime(std::make_shared<Lifetime>())
{
    m_pool.setMaxThreadCount(WorkerCount);
    m_pool.setExpiryTimeout(10000);
}

CoverImageDecoder::~CoverImageDecoder()
{
    m_lifetime->cancelled.store(true, std::memory_order_release);
    m_pending.clear();
    // There are at most two active decodes. Waiting keeps the invokeMethod
    // receiver alive until workers stop; queued completions are then discarded
    // automatically by QObject destruction.
    m_pool.waitForDone();
}

bool CoverImageDecoder::decode(QByteArray bytes, QSize thumbnailSize, Completion done)
{
    if (bytes.isEmpty() || bytes.size() > MaximumEncodedBytes || !done)
        return false;
    const auto hasRoom = [this, &bytes] {
        return m_active + m_pending.size() < MaximumOutstanding
            && m_pendingBytes + bytes.size() <= MaximumQueuedBytes;
    };
    while (!hasRoom() && thumbnailSize.isEmpty()) {
        // A saturated icon queue must not prevent the selected song's original
        // cover from loading. Retired thumbnails report failure asynchronously,
        // allowing their callers to remove request markers and retry on scroll.
        int expendable = -1;
        for (int index = 0; index < m_pending.size(); ++index)
            if (!m_pending.at(index).thumbnailSize.isEmpty()) {
                expendable = index;
                break;
            }
        if (expendable < 0)
            return false;
        Job retired = m_pending.takeAt(expendable);
        m_pendingBytes -= retired.bytes.size();
        QMetaObject::invokeMethod(this, [done = std::move(retired.done)] { done({}); },
                                  Qt::QueuedConnection);
    }
    if (!hasRoom())
        return false;
    m_pendingBytes += bytes.size();
    Job job{std::move(bytes), thumbnailSize, std::move(done)};
    // A newly selected song's original cover takes precedence over list icons.
    if (thumbnailSize.isEmpty())
        m_pending.prepend(std::move(job));
    else
        m_pending.enqueue(std::move(job));
    dispatch();
    return true;
}

void CoverImageDecoder::dispatch()
{
    while (m_active < WorkerCount && !m_pending.isEmpty()) {
        Job job = m_pending.dequeue();
        m_pendingBytes -= job.bytes.size();
        ++m_active;
        const auto lifetime = m_lifetime;
        m_pool.start([this, lifetime, job = std::move(job)]() mutable {
            if (lifetime->cancelled.load(std::memory_order_acquire))
                return;
            QImage image = decodeImage(job.bytes, job.thumbnailSize);
            // Release the encoded file before its result waits on the UI queue.
            job.bytes.clear();
            if (lifetime->cancelled.load(std::memory_order_acquire))
                return;
            QMetaObject::invokeMethod(this,
                [this, lifetime, image = std::move(image), done = std::move(job.done)] {
                    if (lifetime->cancelled.load(std::memory_order_acquire))
                        return;
                    --m_active;
                    dispatch();
                    done(image);
                }, Qt::QueuedConnection);
        });
    }
}

QImage CoverImageDecoder::decodeImage(const QByteArray &bytes, const QSize &thumbnailSize)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer);
    const QSize original = reader.size();
    if (!original.isValid() || qint64(original.width()) * original.height() > MaximumImagePixels)
        return {};
    if (!thumbnailSize.isEmpty()) {
        const QSize scaled = original.scaled(thumbnailSize, Qt::KeepAspectRatioByExpanding);
        if (!scaled.isValid() || qint64(scaled.width()) * scaled.height() > MaximumImagePixels)
            return {};
        // JPEG and other supporting handlers can avoid decoding the full cover.
        // Other handlers still decode and resample here, outside the UI thread.
        if (reader.supportsOption(QImageIOHandler::ScaledSize))
            reader.setScaledSize(scaled);
    }
    QImage image = reader.read();
    if (image.isNull() || thumbnailSize.isEmpty())
        return image;
    image = image.scaled(thumbnailSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (image.width() - thumbnailSize.width()) / 2;
    const int y = (image.height() - thumbnailSize.height()) / 2;
    return image.copy(x, y, thumbnailSize.width(), thumbnailSize.height());
}
