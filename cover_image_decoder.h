#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSize>
#include <QThreadPool>
#include <functional>
#include <memory>

// Only QImage work runs in the pool. Completion always runs in the owner's
// thread, where the caller can safely create QPixmap/QIcon objects.
class CoverImageDecoder final : public QObject {
public:
    using Completion = std::function<void(const QImage &)>;

    explicit CoverImageDecoder(QObject *parent = nullptr);
    ~CoverImageDecoder() override;

    // An empty thumbnailSize preserves the complete original-resolution image.
    // False means the bounded queue could not accept the request.
    bool decode(QByteArray bytes, QSize thumbnailSize, Completion done);

private:
    struct Lifetime;
    struct Job {
        QByteArray bytes;
        QSize thumbnailSize;
        Completion done;
    };
    void dispatch();
    static QImage decodeImage(const QByteArray &bytes, const QSize &thumbnailSize);

    QThreadPool m_pool;
    QQueue<Job> m_pending;
    std::shared_ptr<Lifetime> m_lifetime;
    qint64 m_pendingBytes = 0;
    int m_active = 0;
};
