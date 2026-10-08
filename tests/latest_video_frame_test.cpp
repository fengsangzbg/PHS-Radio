#include "../latest_video_frame.h"

#include <QCoreApplication>
#include <QImage>
#include <QThread>
#include <QVideoSink>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

QVideoFrame frame(qint64 sequence)
{
    // The format constructor also supports the project's Qt 6.6 minimum.
    // QVideoFrame(QImage) would require Qt 6.8 just for this fixture.
    QVideoFrame result(QVideoFrameFormat(QSize(32, 24), QVideoFrameFormat::Format_RGBA8888));
    check(result.map(QVideoFrame::WriteOnly), "The owned software frame must be writable.");
    for (int y = 0; y < 24; ++y) {
        uchar *pixels = result.bits(0) + y * result.bytesPerLine(0);
        for (int x = 0; x < 32; ++x) {
            pixels[x * 4] = 17;
            pixels[x * 4 + 1] = 110;
            pixels[x * 4 + 2] = 225;
            pixels[x * 4 + 3] = 255;
        }
    }
    result.unmap();
    result.setStartTime(sequence);
    return result;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto buffer = std::make_shared<LatestVideoFrame>();
    buffer->offer(frame(1));
    check(!buffer->take().isValid(), "Inactive capture must reject source frames.");
    buffer->reset(true);
    buffer->offer(frame(2));
    buffer->offer(QVideoFrame{});
    check(buffer->take().startTime() == 2, "An invalid backend frame must not erase the latest valid frame.");
    check(!buffer->take().isValid(), "A published frame must be consumed only once.");

    buffer->offer(frame(10));
    int inspected = 0;
    const auto acceptsEleven = [&inspected](const QVideoFrame &candidate) {
        ++inspected;
        return candidate.startTime() == 11;
    };
    check(!buffer->takeIf(acceptsEleven).isValid() && inspected == 1,
          "A worker must leave an unsupported candidate available to GUI fallback rather than consume it.");
    check(buffer->take().startTime() == 10,
          "Predicate rejection must retain the same candidate for another consumer.");
    check(!buffer->takeIf(acceptsEleven).isValid() && inspected == 1,
          "An empty mailbox must not invoke a metadata predicate for an invalid frame.");

    buffer->offer(frame(10));
    check(!buffer->takeIf(acceptsEleven).isValid(), "An incompatible latest frame must remain unconsumed.");
    buffer->offer(frame(11));
    const QVideoFrame accepted = buffer->takeIf(acceptsEleven);
    check(accepted.startTime() == 11 && !buffer->take().isValid(),
          "A compatible replacement must be returned exactly once and atomically clear the mailbox.");
    buffer->offer(frame(12));
    check(!buffer->takeIf(acceptsEleven).isValid() && buffer->take().startTime() == 12,
          "Rejecting a new incompatible replacement must not resurrect an older accepted/rejected frame.");

    QVideoSink sink;
    const QThread *guiThread = QThread::currentThread();
    std::atomic<int> sourceCallbacks{0};
    std::atomic<bool> callbackOnGui{false};
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &app,
        [buffer, guiThread, &sourceCallbacks, &callbackOnGui](const QVideoFrame &value) {
            callbackOnGui.store(QThread::currentThread() == guiThread);
            buffer->offer(value);
            ++sourceCallbacks;
        }, Qt::DirectConnection);

    constexpr int burst = 4000;
    std::unique_ptr<QThread> producer(QThread::create([&sink] {
        for (int index = 0; index < burst; ++index)
            emit sink.videoFrameChanged(frame(index));
    }));
    producer->start();
    // Deliberately keep the GUI event loop blocked. The newest frame must be
    // available without processing a backlog of backend signal deliveries.
    check(producer->wait(5000), "The capture producer must remain independent of GUI event processing.");
    check(sourceCallbacks == burst && !callbackOnGui,
          "The source callback must only update its thread-safe buffer directly.");
    check(!buffer->takeIf([](const QVideoFrame &candidate) { return candidate.startTime() < 0; }).isValid(),
          "Rejecting the result of a concurrent burst must retain its newest candidate for another path.");
    const QVideoFrame newest = buffer->takeIf([](const QVideoFrame &candidate) {
        return candidate.pixelFormat() == QVideoFrameFormat::Format_RGBA8888;
    });
    check(newest.isValid() && newest.startTime() == burst - 1,
          "A busy GUI must resume with the latest source frame instead of replaying stale frames.");
    check(newest.toImage().size() == QSize(32, 24), "GUI conversion must preserve original frame dimensions.");

    buffer->offer(frame(42));
    buffer->reset(false);
    buffer->offer(frame(43));
    check(!buffer->take().isValid(), "Pause/stop must flush retained frames and reject later callbacks.");
    buffer->reset(true);
    buffer->offer(frame(44));
    check(buffer->take().startTime() == 44, "Resume must accept fresh frames after flushing the previous session.");

    std::weak_ptr<LatestVideoFrame> weak = buffer;
    QObject::disconnect(&sink, nullptr, &app, nullptr);
    auto owned = buffer;
    std::unique_ptr<QThread> detached(QThread::create([owned] { owned->offer(frame(45)); }));
    owned.reset();
    buffer.reset();
    detached->start();
    check(detached->wait(5000), "An in-flight backend callback must safely retain its own buffer lifetime.");
    detached.reset();
    check(weak.expired(), "Completed callbacks must release the mailbox and its retained frame resources.");
    std::fprintf(stderr, "Latest-frame delivery passed: %d source frames, no GUI delivery backlog.\n", burst);
    return 0;
}
