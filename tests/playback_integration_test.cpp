#include <QtWidgets>
#include <QtMultimedia>
#include <QtNetwork>
#include <functional>
#include <memory>
#include <vector>
#define private public
#define main originalPlayerMain
#include "../main.cpp"
#undef main
#undef private

#include <cmath>
#include <cstdlib>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

void waitForEvents(int duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitUntil(const std::function<bool()> &condition, int timeout = 10000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout)
        waitForEvents(10);
    return condition();
}

QUrl writeWave(const QString &path, int duration, int frequency)
{
    constexpr int sampleRate = 16000;
    const int samples = duration * sampleRate / 1000;
    const quint32 dataSize = samples * sizeof(qint16);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "The temporary WAV must be writable.");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataSize);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(sampleRate)
           << quint32(sampleRate * sizeof(qint16)) << quint16(sizeof(qint16)) << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataSize;
    for (int sample = 0; sample < samples; ++sample) {
        const double phase = sample * frequency * 6.283185307179586 / sampleRate;
        stream << qint16(std::sin(phase) * 1000);
    }
    check(stream.status() == QDataStream::Ok, "The temporary PCM WAV must be complete.");
    file.close();
    return QUrl::fromLocalFile(path);
}

Track song(const QString &id, const QUrl &url)
{
    Track track;
    track.id = id;
    track.title = id;
    track.artist = QStringLiteral("Local PCM fixture");
    track.audioUrl = url;
    return track;
}

void startPlayback(PlayerWindow &window, const QVector<Track> &tracks, int row = 0)
{
    window.setTracks(tracks);
    window.m_tracks->setCurrentCell(row, 1);
    window.playSelected();
    check(waitUntil([&] {
        return window.m_player->playbackState() == QMediaPlayer::PlayingState
            && window.m_player->duration() > 0 && window.m_player->isSeekable()
            && window.m_player->source() == tracks[row].audioUrl;
    }), "The actual Qt media backend must load and play the local PCM source.");
}

void verifyProgressAndPausedSeek(PlayerWindow &window, const QVector<Track> &tracks)
{
    startPlayback(window, tracks);
    check(waitUntil([&] { return window.m_player->position() >= 50; }),
          "Actual media playback must produce position updates.");
    check(window.m_dock->progressSlider()->isEnabled()
              && window.m_dock->progressSlider()->value() > 0,
          "Real duration and position changes must enable and advance the Dock progress slider.");
    const qint64 duration = window.m_player->duration();
    check(qAbs(duration - 1250) < 50, "The Dock must use the decoded WAV duration rather than a placeholder.");
    window.togglePlayback();
    check(waitUntil([&] { return window.m_player->playbackState() == QMediaPlayer::PausedState; }),
          "The Dock pause action must pause the actual media backend.");

    QSlider *slider = window.m_dock->progressSlider();
    slider->setSliderDown(true);
    slider->setValue(slider->maximum() * 3 / 5);
    slider->setSliderDown(false);
    const qint64 expectedPosition = duration * 3 / 5;
    check(waitUntil([&] { return qAbs(window.m_player->position() - expectedPosition) < 50; }),
          "Releasing the dragged Dock slider must seek the actual audio source.");
    waitForEvents(160);
    check(window.m_player->playbackState() == QMediaPlayer::PausedState
              && qAbs(window.m_player->position() - expectedPosition) < 50,
          "Seeking a paused source must retain pause and its new position.");
    check(window.m_playbackQueue.currentIndex() == 0 && window.m_playbackQueue.size() == 2,
          "Progress manipulation must preserve the selected queue context.");
}

void verifyManualNavigationAndViewIndependence(PlayerWindow &window, const QVector<Track> &tracks)
{
    // Changing the visible list represents filtering or choosing another
    // playlist while the first queue remains paused in the player.
    Track outside = tracks[1];
    outside.id = QStringLiteral("Outside the playing context");
    outside.title = outside.id;
    window.setTracks({outside});
    window.m_tracks->setCurrentCell(0, 1);
    check(window.m_playbackQueue.current()->id == tracks[0].id
              && window.m_playbackQueue.size() == 2,
          "Replacing the view must not replace the active playback queue.");
    window.m_dock->onNext();
    check(window.m_playbackQueue.currentIndex() == 1
              && window.m_player->source() == tracks[1].audioUrl
              && window.m_dock->titleLabel()->text() == tracks[1].title,
          "Dock next must use the original queue rather than the newly filtered list.");
    check(waitUntil([&] { return window.m_player->playbackState() == QMediaPlayer::PlayingState; }),
          "Manual next must start the next local source.");
    window.togglePlayback();
    window.m_dock->onPrevious();
    check(window.m_playbackQueue.currentIndex() == 0
              && window.m_player->source() == tracks[0].audioUrl,
          "Dock previous must return to the preceding song in the original queue.");
    check(waitUntil([&] { return window.m_player->playbackState() == QMediaPlayer::PlayingState; }),
          "Manual previous must start the preceding local source.");
    window.m_player->pause();
}

void verifyAutomaticAdvancement(PlayerWindow &window, const QVector<Track> &tracks)
{
    startPlayback(window, tracks);
    const int firstRequest = window.m_playRequest;
    check(waitUntil([&] {
        return window.m_playbackQueue.currentIndex() == 1
            && window.m_player->source() == tracks[1].audioUrl
            && window.m_player->playbackState() == QMediaPlayer::PlayingState;
    }), "Finishing the first local source must automatically play the second queue song.");
    check(window.m_playRequest == firstRequest + 1,
          "The media ending must request the next source only once.");
    check(waitUntil([&] {
        return window.m_player->mediaStatus() == QMediaPlayer::EndOfMedia
            && window.m_player->playbackState() == QMediaPlayer::StoppedState
            && window.statusBar()->currentMessage() == QStringLiteral("列表播放完毕");
    }), "Sequential playback must stop after the final song instead of wrapping automatically.");
    const int endedRequest = window.m_playRequest;
    waitForEvents(150);
    check(window.m_playbackQueue.currentIndex() == 1 && window.m_playRequest == endedRequest,
          "The completed sequential queue must remain stopped on its final song.");
}

void verifyStaleEndRejection(PlayerWindow &window, const QVector<Track> &tracks)
{
    bool interruptedEnding = false;
    bool staleEndingRejected = false;
    // Root's EndOfMedia handler first queues advancement. Invalidating that
    // captured request in a subsequent signal listener reproduces a newly
    // dispatched user request before the old deferred ending is processed.
    const QMetaObject::Connection ending = QObject::connect(window.m_player,
        &QMediaPlayer::mediaStatusChanged, &window, [&](QMediaPlayer::MediaStatus status) {
            if (status != QMediaPlayer::EndOfMedia || interruptedEnding
                || window.m_playbackQueue.currentIndex() != 0)
                return;
            interruptedEnding = true;
            ++window.m_playRequest;
            QTimer::singleShot(0, &window, [&] {
                staleEndingRejected = window.m_playbackQueue.currentIndex() == 0;
                window.m_dock->onNext();
            });
        });
    startPlayback(window, tracks);
    check(waitUntil([&] {
        return interruptedEnding && window.m_playbackQueue.currentIndex() == 1
            && window.m_player->source() == tracks[1].audioUrl
            && window.m_player->playbackState() == QMediaPlayer::PlayingState;
    }), "A new user request after media completion must still start its chosen successor.");
    check(staleEndingRejected,
          "A deferred EndOfMedia from an invalidated request must not advance the newer queue.");
    QObject::disconnect(ending);
    window.m_player->pause();
}

void verifyUnavailableSuccessor(PlayerWindow &window, const QVector<Track> &tracks)
{
    Track unavailable = tracks[1];
    unavailable.id = QStringLiteral("Unavailable successor");
    unavailable.title = unavailable.id;
    unavailable.audioUrl = QUrl();
    startPlayback(window, {tracks[0], unavailable});
    const int request = window.m_playRequest;
    window.m_dock->onNext();
    check(window.m_playbackQueue.currentIndex() == 1
              && window.m_dock->titleLabel()->text() == unavailable.title,
          "Selecting an unavailable successor must keep its queue and displayed song identity aligned.");
    check(window.m_playRequest > request && window.m_player->source().isEmpty()
              && window.m_player->playbackState() == QMediaPlayer::StoppedState
              && !window.m_sourceRequestPending && window.m_pendingTitle.isEmpty()
              && !window.m_dock->progressSlider()->isEnabled()
              && window.m_dock->playPauseButton()->isEnabled(),
          "An unavailable next song must stop the preceding source and reset pending/seek state.");
    check(window.statusBar()->currentMessage().contains(QStringLiteral("没有可播放")),
          "An unavailable source must leave a visible explanation instead of silently playing the old song.");
    window.m_dock->onPrevious();
    check(waitUntil([&] {
        return window.m_playbackQueue.currentIndex() == 0
            && window.m_player->playbackState() == QMediaPlayer::PlayingState;
    }), "An unavailable song must not prevent navigating back to a playable source.");
    window.m_player->pause();
}

void verifyAudioDeviceFollowing(PlayerWindow &window, const Track &track)
{
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (defaultDevice.isNull()) {
        qInfo("Audio route checks skipped: this environment has no output device.");
        return;
    }
    window.m_audioOutput->setMuted(true);
    window.m_audioOutput->setVolume(.37f);
    startPlayback(window, {track});
    check(waitUntil([&] { return window.m_player->position() >= 100; }),
          "Audio route checks must begin during real advancing playback.");
    const int request = window.m_playRequest;
    int outputRebindings = 0;
    const auto rebound = QObject::connect(window.m_player, &QMediaPlayer::audioOutputChanged,
        &window, [&] { ++outputRebindings; });
    const auto outputs = QMediaDevices::audioOutputs();
    auto moveToAlternateOutput = [&] {
        for (const auto &device : outputs) {
            if (device.id() != defaultDevice.id()) {
                window.m_audioOutput->setDevice(device);
                break;
            }
        }
    };
    auto notifyDeviceChange = [&] {
        for (int event = 0; event < 6; ++event)
            check(QMetaObject::invokeMethod(window.m_mediaDevices, "audioOutputsChanged",
                                           Qt::DirectConnection),
                  "The device monitor must accept a simulated output-change notification.");
        waitForEvents(280);
        check(window.m_audioOutput->device().id() == defaultDevice.id(),
              "Output-change notifications must restore the current system default output.");
        check(window.m_player->source() == track.audioUrl && window.m_playRequest == request
                  && window.m_playbackQueue.current()->id == track.id,
              "Reconnecting an output must preserve the source and playback queue without fetching a new song.");
        check(window.m_audioOutput->isMuted() && qAbs(window.m_audioOutput->volume() - .37f) < .001f,
              "An output change must retain the user's volume and mute state.");
    };
    moveToAlternateOutput();
    const qint64 playingPosition = window.m_player->position();
    notifyDeviceChange();
    check(window.m_player->playbackState() == QMediaPlayer::PlayingState
              && window.m_player->position() > playingPosition,
          "Switching outputs while playing must keep the song advancing.");

    window.m_player->pause();
    window.m_player->setPosition(700);
    moveToAlternateOutput();
    notifyDeviceChange();
    check(window.m_player->playbackState() == QMediaPlayer::PausedState
              && qAbs(window.m_player->position() - 700) < 60,
          "Switching outputs while paused must preserve pause and the current position.");
    const int bindingsBeforeUnchangedNotification = outputRebindings;
    notifyDeviceChange();
    check(outputRebindings == bindingsBeforeUnchangedNotification,
          "Unrelated device notifications must not repeatedly recreate an available output.");

    // An endpoint can disappear and return with the same ID. Its old sink
    // needs reopening even though the QAudioDevice value compares equal.
    window.m_audioRouteInterrupted = true;
    notifyDeviceChange();
    check(window.m_player->playbackState() == QMediaPlayer::PausedState
              && qAbs(window.m_player->position() - 700) < 60,
          "Reopening the same disconnected endpoint must not resume or reset a paused song.");
    window.m_player->play();
    check(waitUntil([&] { return window.m_player->position() >= 800; }),
          "Playback must remain usable after the paused output is reopened.");
    const qint64 reconnectedPosition = window.m_player->position();
    window.m_audioRouteInterrupted = true;
    notifyDeviceChange();
    check(window.m_player->playbackState() == QMediaPlayer::PlayingState
              && window.m_player->position() >= reconnectedPosition,
          "Reopening the same endpoint during playback must keep its state and progress.");
    window.m_player->pause();
    window.m_player->setPosition(0);
    waitForEvents(100);
    window.m_audioRouteInterrupted = true;
    notifyDeviceChange();
    check(window.m_player->playbackState() == QMediaPlayer::PausedState
              && window.m_player->position() == 0,
          "Reconnecting at the beginning must retain the paused position.");
    window.m_player->play();
    check(waitUntil([&] { return window.m_player->position() >= 100; }),
          "Reconnecting a fully buffered source at zero must not leave its playback clock stalled.");
    window.m_player->pause();
    QObject::disconnect(rebound);
    window.m_audioOutput->setVolume(0);
    window.m_audioOutput->setMuted(false);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PHS Radio Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Playback Integration Test"));
    QSettings().clear();
    QTemporaryDir temporary;
    check(temporary.isValid(), "A temporary folder must be available for local media fixtures.");
    const QVector<Track> tracks{
        song(QStringLiteral("First local song"), writeWave(temporary.filePath(QStringLiteral("first.wav")), 1250, 220)),
        song(QStringLiteral("Second local song"), writeWave(temporary.filePath(QStringLiteral("second.wav")), 1250, 330))
    };
    PlayerWindow window({MusicPlatform::QQMusic}, true);
    if (qEnvironmentVariableIsSet("PHSRADIO_AUDIO_DIAGNOSTIC")) {
        QObject::connect(window.m_mediaDevices, &QMediaDevices::audioOutputsChanged, &window, [&] {
            qInfo() << "Audio devices changed" << QMediaDevices::defaultAudioOutput().description()
                    << "position" << window.m_player->position();
        });
        QObject::connect(window.m_player, &QMediaPlayer::mediaStatusChanged, &window,
            [&](QMediaPlayer::MediaStatus status) {
                qInfo() << "Media status" << status << "position" << window.m_player->position()
                        << "queue" << window.m_playbackQueue.currentIndex();
            });
        QObject::connect(window.m_player, &QMediaPlayer::playbackStateChanged, &window,
            [&](QMediaPlayer::PlaybackState state) {
                qInfo() << "Playback state" << state << "position" << window.m_player->position();
            });
    }
    window.m_audioOutput->setVolume(0); // Validate the media path without audible test tones.
    window.m_playbackQueue.setShuffle(false);
    window.m_dock->setPinned(true);
    window.show();
    QCoreApplication::processEvents();
    verifyProgressAndPausedSeek(window, tracks);
    verifyManualNavigationAndViewIndependence(window, tracks);
    verifyAutomaticAdvancement(window, tracks);
    verifyStaleEndRejection(window, tracks);
    verifyUnavailableSuccessor(window, tracks);
    verifyAudioDeviceFollowing(window, song(QStringLiteral("Audio reconnection fixture"),
        writeWave(temporary.filePath(QStringLiteral("audio-route.wav")), 10000, 220)));
    window.m_player->stop();
    window.m_player->setSource(QUrl());
    qInfo("Playback integration tests passed.");
    return 0;
}
