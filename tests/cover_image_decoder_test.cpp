#include "../cover_image_decoder.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <cstdlib>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

QByteArray encodedCover()
{
    QImage source(2048, 1536, QImage::Format_RGB32);
    source.fill(QColor(35, 120, 205));
    for (int y = 0; y < source.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(source.scanLine(y));
        for (int x = 0; x < 512; ++x)
            pixels[x] = qRgb(240, 40, 40);
        for (int x = 1536; x < source.width(); ++x)
            pixels[x] = qRgb(40, 240, 40);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(source.save(&buffer, "PNG"), "Encode the cover fixture");
    return bytes;
}

template<class Predicate>
void waitFor(Predicate finished)
{
    QElapsedTimer timeout;
    timeout.start();
    while (!finished() && timeout.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    check(finished(), "Decoder completion timed out");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray bytes = encodedCover();
    CoverImageDecoder decoder;
    int completed = 0;
    int timerTicks = 0;
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(1);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++timerTicks; });
    heartbeat.start();

    check(decoder.decode(bytes, {}, [&](const QImage &image) {
        check(QThread::currentThread() == app.thread(), "Complete on the caller's thread");
        check(image.size() == QSize(2048, 1536), "Preserve the original HD cover dimensions");
        check(image.pixelColor(0, 0).red() == 240, "Preserve original cover pixels");
        ++completed;
    }), "Accept an original-resolution decode");
    for (int index = 0; index < 8; ++index) {
        check(decoder.decode(bytes, QSize(160, 160), [&](const QImage &image) {
            check(QThread::currentThread() == app.thread(), "Thumbnail completion stays on the UI thread");
            check(image.size() == QSize(160, 160), "Create an exact DPR-sized square thumbnail");
            check(image.pixelColor(80, 80) == QColor(35, 120, 205), "Crop the center without stretching");
            ++completed;
        }), "Accept a thumbnail decode");
    }
    check(completed == 0, "Decoding must never run synchronously on the caller");
    waitFor([&] { return completed == 9; });
    check(timerTicks > 0, "The UI event loop keeps servicing timers during image work");
    heartbeat.stop();

    // A new record cover must appear before a backlog of list thumbnails is
    // drained. A small valid original avoids making this a decode-speed test.
    QImage recordCover(64, 64, QImage::Format_RGB32);
    recordCover.fill(Qt::yellow);
    QByteArray recordBytes;
    QBuffer recordBuffer(&recordBytes);
    recordBuffer.open(QIODevice::WriteOnly);
    check(recordCover.save(&recordBuffer, "PNG"), "Encode a selected-song cover");
    QString completionOrder;
    for (int index = 0; index < 12; ++index)
        check(decoder.decode(bytes, QSize(160, 160), [&](const QImage &) {
            completionOrder += QLatin1Char('T');
        }), "Queue background thumbnails");
    check(decoder.decode(recordBytes, {}, [&](const QImage &image) {
        check(image.size() == QSize(64, 64), "Decode the selected original cover");
        completionOrder += QLatin1Char('H');
    }), "Queue the newly selected high-resolution cover");
    waitFor([&] { return completionOrder.size() == 13; });
    check(completionOrder.indexOf(QLatin1Char('H')) <= 3,
          "Selected record covers take precedence over queued thumbnail work");

    bool invalidCompleted = false;
    check(decoder.decode(QByteArray("invalid-cover"), {}, [&](const QImage &image) {
        check(image.isNull(), "Malformed images fail safely");
        invalidCompleted = true;
    }), "Accept malformed bytes for asynchronous validation");
    waitFor([&] { return invalidCompleted; });
    check(!decoder.decode(QByteArray(16 * 1024 * 1024 + 1, '\0'), {}, [](const QImage &) {}),
          "Reject an oversized encoded file before it enters the pool");

    // A valid BMP header advertising 64 million pixels must be rejected before
    // any pixel buffer is allocated, even when only a small icon was requested.
    QByteArray oversizedBmp(54, '\0');
    oversizedBmp[0] = 'B';
    oversizedBmp[1] = 'M';
    qToLittleEndian<quint32>(54, oversizedBmp.data() + 10);
    qToLittleEndian<quint32>(40, oversizedBmp.data() + 14);
    qToLittleEndian<quint32>(8000, oversizedBmp.data() + 18);
    qToLittleEndian<quint32>(8000, oversizedBmp.data() + 22);
    qToLittleEndian<quint16>(1, oversizedBmp.data() + 26);
    qToLittleEndian<quint16>(24, oversizedBmp.data() + 28);
    invalidCompleted = false;
    check(decoder.decode(oversizedBmp, QSize(80, 80), [&](const QImage &image) {
        check(image.isNull(), "Reject images exceeding the decoded pixel budget");
        invalidCompleted = true;
    }), "Validate advertised image dimensions in the worker");
    waitFor([&] { return invalidCompleted; });

    // Worker completions wait for the UI queue, so the outstanding-job limit is
    // deterministic while this thread submits requests without pumping events.
    bool calledAfterDestruction = false;
    {
        CoverImageDecoder retiring;
        int accepted = 0;
        for (int index = 0; index < 150; ++index)
            accepted += retiring.decode(QByteArray("invalid-cover"), QSize(80, 80),
                [&](const QImage &) { calledAfterDestruction = true; });
        check(accepted == 128, "Bound the number of queued and active image jobs");
        check(retiring.decode(recordBytes, {}, [&](const QImage &) { calledAfterDestruction = true; }),
              "A saturated thumbnail queue still accepts the selected original cover");
    }
    QCoreApplication::processEvents();
    check(!calledAfterDestruction, "Destroying the decoder discards queued callbacks safely");
    {
        CoverImageDecoder bounded;
        const QByteArray largeBytes(8 * 1024 * 1024, '\0');
        int accepted = 0;
        for (int index = 0; index < 20; ++index)
            accepted += bounded.decode(largeBytes, QSize(80, 80), [](const QImage &) {});
        check(accepted == 10, "Bound queued encoded bytes independently of job count");
    }
    return 0;
}
