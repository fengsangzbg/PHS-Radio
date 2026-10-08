#include <QtWidgets>
#define private public
#include "../playback_dock.h"
#include "../meteor_effect.h"
#undef private
#include "../aero_widgets.h"
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
    QEventLoop events;
    QTimer::singleShot(duration, &events, &QEventLoop::quit);
    events.exec();
}

void mouseEvent(QWidget *widget, QEvent::Type type, const QPoint &position,
                Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(widget->mapToGlobal(position)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void verifyPointerAndFocus(QWidget &host, PlaybackDock &dock, QLineEdit &outside)
{
    outside.setFocus();
    QCoreApplication::processEvents();
    check(dock.isHidden() && !dock.isExpanded(), "The playback dock must begin collapsed outside the host.");
    check(!dock.progressSlider()->isEnabled(), "Progress must be disabled until a seekable duration is available.");
    check(QApplication::focusWidget() == &outside, "A hidden dock must preserve outside input focus.");
    dock.updatePointer(QPoint(host.width() / 2, host.height() - 17), true);
    check(!dock.isExpanded(), "A pointer beyond the 16px bottom edge must not open the dock.");
    dock.updatePointer(QPoint(host.width() / 2, host.height() - 16), true);
    check(dock.isExpanded(), "The bottom 16px must expand the dock.");
    waitForEvents(240);
    check(!dock.isHidden() && dock.geometry().bottom() < host.height(),
          "The dock's opening animation must finish entirely inside the host.");
    check(QApplication::focusWidget() == &outside, "Hover expansion must not steal keyboard focus.");

    dock.updatePointer(QPoint(30, 30), true);
    waitForEvents(250);
    check(dock.isExpanded(), "Moving away must respect the 500ms collapse delay.");
    dock.updatePointer(dock.geometry().center(), true);
    waitForEvents(360);
    check(dock.isExpanded(), "Returning to the panel must cancel a pending collapse.");
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(750);
    check(!dock.isExpanded() && dock.isHidden() && dock.geometry().top() >= host.height(),
          "Leaving the host must collapse the dock completely below it.");
    check(QApplication::focusWidget() == &outside, "Hover collapse must preserve outside keyboard focus.");
    dock.updatePointer(QPoint(host.width() / 2, host.height() + 12), false);
    check(!dock.isExpanded(), "A pointer below the actual window must not open the dock.");
    dock.updatePointer(QPoint(host.width() / 2, host.height() + 12), true);
    check(dock.isExpanded(), "An in-window status-bar footer below the player page must open the dock.");
    waitForEvents(240);
    dock.setExpanded(false);
    waitForEvents(240);
}

void verifyPinResizeAndAnimation(QWidget &host, PlaybackDock &dock)
{
    bool observedPin = false;
    dock.onPinnedChanged = [&](bool pinned) { observedPin = pinned; };
    dock.setPinned(true);
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(750);
    check(observedPin && dock.isPinned() && dock.isExpanded(), "Pinning must persist and keep the panel expanded.");
    dock.setExpanded(false);
    waitForEvents(240);
    check(dock.isHidden(), "An explicit close must also work for a pinned dock.");
    dock.setPinned(false);
    check(!observedPin, "Unpinning must notify the persistence callback.");
    dock.onPinnedChanged = {};
    dock.setExpanded(true);
    waitForEvents(240);
    host.resize(760, 560);
    QCoreApplication::processEvents();
    check(dock.geometry().width() <= host.width() - 24 && dock.geometry().height() == 156
              && dock.geometry().left() >= 0 && dock.geometry().right() < host.width(),
          "A narrower host must keep the complete dock visible.");
    host.resize(1280, 840);
    QCoreApplication::processEvents();
    check(dock.width() >= 900 && dock.geometry().center().x() >= host.width() / 2 - 1
              && dock.geometry().center().x() <= host.width() / 2 + 1
              && dock.geometry().bottom() == host.height() - 15,
          "A larger host must center the intended dock width near the bottom.");
    dock.setExpanded(false);
    waitForEvents(240);
    host.resize(920, 640);
    QCoreApplication::processEvents();
    check(dock.isHidden() && dock.geometry().top() >= host.height(),
          "A collapsed dock must remain below the resized host.");
    dock.setExpanded(true);
    waitForEvents(55);
    dock.setExpanded(false);
    waitForEvents(55);
    dock.setExpanded(true);
    waitForEvents(240);
    check(dock.isExpanded() && !dock.isHidden() && dock.geometry().bottom() < host.height(),
          "A reversed animation must finish in the last requested state.");
}

void verifySeeking(PlaybackDock &dock)
{
    QSlider *slider = dock.progressSlider();
    int requests = 0;
    qint64 requestedPosition = -1;
    dock.onSeekRequested = [&](qint64 position) { ++requests; requestedPosition = position; };
    dock.setSeekable(true);
    dock.setDuration(300000);
    dock.setPosition(60000);
    QCoreApplication::processEvents();
    check(slider->isEnabled() && requests == 0, "Media position updates must not request a seek.");
    const QPoint start(slider->width() * 3 / 4, slider->height() / 2);
    mouseEvent(slider, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    check(slider->isSliderDown(), "Pressing the progress track must begin a draggable preview.");
    const int initialPreview = slider->value();
    dock.setPosition(120000);
    check(slider->value() == initialPreview && requests == 0,
          "Incoming media positions must not overwrite a drag preview or seek prematurely.");
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(600);
    check(dock.isExpanded(), "An active progress drag must keep the dock open outside its bounds.");
    const QPoint end(slider->width() * 11 / 20, slider->height() / 2);
    mouseEvent(slider, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    const int movedPreview = slider->value();
    dock.setPosition(180000);
    check(slider->value() == movedPreview && requests == 0,
          "Playback ticks during pointer movement must preserve the chosen preview.");
    mouseEvent(slider, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    check(requests == 1 && requestedPosition > 120000 && requestedPosition < 210000,
          "Releasing a drag must request the selected millisecond position once.");
    const QPoint click(slider->width() / 4, slider->height() / 2);
    mouseEvent(slider, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
    mouseEvent(slider, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
    check(requests == 2 && requestedPosition > 45000 && requestedPosition < 105000,
          "Clicking the groove must seek to that position rather than a page-step offset.");
    dock.setPosition(0);
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(slider, &right);
    check(requests == 3 && requestedPosition > 4500 && requestedPosition < 5500,
          "Keyboard progress adjustment must request a seek with the configured time step.");
    dock.m_keyboardTimer->stop();
    dock.setSeekable(false);
    QApplication::sendEvent(slider, &right);
    check(!slider->isEnabled() && requests == 3, "Non-seekable playback must reject user seeking.");
    dock.setSeekable(true);
    dock.setDuration(0);
    check(!slider->isEnabled(), "A missing duration must disable seeking even if media is seekable.");
    dock.setDuration(6000000000LL);
    dock.setPosition(3000000000LL);
    check(slider->value() == slider->maximum() / 2 && requests == 3,
          "Durations beyond the slider's integer range must retain accurate qint64 positions.");
    dock.setDuration(60000);
    check(slider->value() == slider->maximum() && dock.m_elapsed->text() == QStringLiteral("1:00")
              && requests == 3,
          "A shorter duration must clamp elapsed time and position without issuing a seek.");
    dock.setDuration(0);
    dock.onSeekRequested = {};
}

void verifyVolume(QWidget &host, PlaybackDock &dock)
{
    QSlider *volume = dock.volumeSlider();
    JellyButton *mute = dock.muteButton();
    int volumeRequests = 0, muteRequests = 0, seekRequests = 0;
    qreal observedVolume = -1;
    bool observedMute = false;
    dock.onVolumeChanged = [&](qreal value) { ++volumeRequests; observedVolume = value; };
    dock.onMutedChanged = [&](bool value) { ++muteRequests; observedMute = value; };
    dock.onSeekRequested = [&](qint64) { ++seekRequests; };
    dock.setVolume(.37);
    dock.setMuted(true);
    check(volume->minimum() == 0 && volume->maximum() == 100 && volume->value() == 37
              && mute->isChecked() && volumeRequests == 0 && muteRequests == 0,
          "Restoring music volume and mute must update the controls without user callbacks.");
    check(volume->accessibleName() == QStringLiteral("音乐音量")
              && mute->accessibleName().contains(QStringLiteral("恢复")),
          "Music volume controls must expose meaningful accessible names and mute actions.");
    dock.setVolume(2);
    check(volume->value() == 100, "Programmatic volume must clamp to one hundred percent.");
    dock.setVolume(-1);
    check(volume->value() == 0 && volumeRequests == 0 && muteRequests == 0,
          "Programmatic volume must clamp to zero without unmuting or issuing callbacks.");
    dock.setVolume(.37);
    const QPoint start(volume->width() * 3 / 4, volume->height() / 2);
    mouseEvent(volume, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    check(volume->isSliderDown() && volumeRequests > 0 && observedVolume > .5
              && muteRequests == 1 && !observedMute && !dock.m_muted,
          "Dragging a muted music volume to a positive value must adjust it live and restore sound.");
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(600);
    check(dock.isExpanded(), "An active music volume drag must keep the dock open outside its bounds.");
    const int requestsBeforeMove = volumeRequests;
    const QPoint end(volume->width() / 3, volume->height() / 2);
    mouseEvent(volume, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    check(volumeRequests > requestsBeforeMove && qAbs(observedVolume - volume->value() / 100.0) < .001,
          "Music volume changes must reach the controller while dragging, before release.");
    const int requestsBeforeRelease = volumeRequests;
    mouseEvent(volume, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    check(!volume->isSliderDown() && volumeRequests == requestsBeforeRelease && seekRequests == 0,
          "Releasing music volume must not duplicate the last value or seek the song.");
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(volume, &right);
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(500);
    check(dock.isExpanded() && dock.m_keyboardTimer->isActive(),
          "Keyboard adjustment of music volume must prevent automatic dock collapse.");
    dock.m_keyboardTimer->stop();

    dock.setVolume(.42);
    dock.setMuted(false);
    const int beforeMuteVolumeRequests = volumeRequests;
    mute->click();
    check(observedMute && dock.m_muted && volume->value() == 42,
          "Muting must preserve the user's selected music volume.");
    mute->click();
    check(!observedMute && !dock.m_muted && volume->value() == 42
              && volumeRequests == beforeMuteVolumeRequests,
          "Restoring a muted song must retain its previous volume without changing the volume callback.");
    volume->setValue(0);
    check(mute->isChecked() && !dock.m_muted, "Zero volume must expose the restore-sound action.");
    mute->click();
    check(volume->value() == 42 && qAbs(observedVolume - .42) < .001 && !dock.m_muted,
          "The restore button at zero percent must recover the last nonzero music volume.");
    const int requestsBeforeSync = volumeRequests, mutedBeforeSync = muteRequests;
    dock.setVolume(.63);
    dock.setMuted(true);
    dock.setTrack(QStringLiteral("Another song"), QStringLiteral("Another artist"));
    check(volume->value() == 63 && dock.m_muted && volumeRequests == requestsBeforeSync
              && muteRequests == mutedBeforeSync,
          "Programmatic audio synchronization and changing songs must preserve independent music volume and mute.");
    dock.setMuted(false);
    dock.setBusy(true);
    check(volume->isEnabled() && mute->isEnabled(), "Preparing a song must leave music volume controls usable.");
    dock.setBusy(false);

    host.resize(760, 560);
    QCoreApplication::processEvents();
    for (QWidget *control : {static_cast<QWidget *>(volume), static_cast<QWidget *>(mute),
                            static_cast<QWidget *>(dock.lyricsButton()), static_cast<QWidget *>(dock.songPageButton())}) {
        check(dock.rect().contains(QRect(control->mapTo(&dock, QPoint()), control->size())),
              "All music volume and page controls must fit inside the minimum-size dock.");
    }
    check(!dock.m_volumeLabel->isVisible()
              && qAbs(dock.playPauseButton()->mapTo(&dock, dock.playPauseButton()->rect().center()).x()
                      - dock.rect().center().x()) <= 1,
          "Compact docks must hide the percentage label and keep playback centered.");
    host.resize(1180, 780);
    QCoreApplication::processEvents();
    check(dock.m_volumeLabel->isVisible()
              && qAbs(dock.playPauseButton()->mapTo(&dock, dock.playPauseButton()->rect().center()).x()
                      - dock.rect().center().x()) <= 1,
          "Wide docks must restore the percentage label without shifting playback away from the center.");
    dock.onVolumeChanged = {};
    dock.onMutedChanged = {};
    dock.onSeekRequested = {};
}

void verifyButtons(PlaybackDock &dock)
{
    int playRequests = 0, nextRequests = 0, previousRequests = 0, modeRequests = 0;
    bool shuffle = false;
    dock.onPlayPause = [&] { ++playRequests; };
    dock.onNext = [&] { ++nextRequests; };
    dock.onPrevious = [&] { ++previousRequests; };
    dock.onShuffleChanged = [&](bool enabled) { ++modeRequests; shuffle = enabled; };
    dock.setTrack(QStringLiteral("A long song title"), QStringLiteral("Example Artist"));
    check(dock.titleLabel()->text() == QStringLiteral("A long song title"), "The title getter must expose the actual song title.");
    dock.setPlaying(true);
    check(dock.playPauseButton()->accessibleName() == QStringLiteral("暂停"),
          "The single transport button must describe the current pause action.");
    dock.playPauseButton()->click();
    dock.setBusy(true);
    dock.playPauseButton()->click();
    dock.m_next->click();
    dock.m_previous->click();
    check(playRequests == 1 && nextRequests == 1 && previousRequests == 1,
          "Busy playback must prevent duplicate play requests while retaining track navigation.");
    dock.setBusy(false);
    dock.setPlaying(false);
    check(dock.playPauseButton()->isEnabled() && dock.playPauseButton()->accessibleName() == QStringLiteral("播放"),
          "Ready playback must restore the play action.");
    dock.setShuffle(true);
    check(dock.isShuffle() && modeRequests == 0, "Restoring a playback mode must not emit a user change.");
    dock.m_mode->click();
    check(!dock.isShuffle() && !shuffle && modeRequests == 1
              && dock.m_mode->text().contains(QStringLiteral("顺序")),
          "The mode button must toggle to sequential playback and show its state.");
    dock.m_mode->click();
    check(dock.isShuffle() && shuffle && modeRequests == 2
              && dock.m_mode->text().contains(QStringLiteral("随机")),
          "The mode button must toggle back to random playback.");
    dock.onPlayPause = {};
    dock.onNext = {};
    dock.onPrevious = {};
    dock.onShuffleChanged = {};
}

void verifyPopupHold(PlaybackDock &dock)
{
    dock.m_keyboardTimer->stop();
    dock.setInteractionHeld(true);
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(750);
    check(dock.isExpanded(), "A held color-dialog interaction must prevent automatic collapse.");
    dock.setInteractionHeld(false);
    dock.updatePointer(QPoint(-20, -20), false);
    waitForEvents(750);
    check(!dock.isExpanded() && dock.isHidden(), "Closing the held interaction must permit normal automatic collapse.");
    dock.setExpanded(true);
    waitForEvents(240);
    bool colorRequested = false;
    dock.onColorRequested = [&] {
        colorRequested = true;
        dock.updatePointer(QPoint(-20, -20), false);
        waitForEvents(750);
        check(dock.isExpanded(), "A modal color callback must retain the dock during its nested event loop.");
    };
    dock.m_palette->click();
    check(colorRequested, "The palette button must invoke the color-selection callback.");
    dock.onColorRequested = {};
}

void verifyPlaybackMeteor(PlaybackDock &dock)
{
    int requests = 0;
    dock.onPlayPause = [&] { ++requests; };
    dock.setBusy(false);
    dock.setPlaying(false);
    dock.playPauseButton()->click();
    check(requests == 1 && dock.m_meteor->isRunning(), "A play request must trigger the blue meteor.");
    waitForEvents(100);
    const qreal phase = dock.m_meteor->progress();
    check(phase > 0 && phase < 1, "The meteor must advance while playback begins.");
    dock.setPlaying(true);
    dock.playPauseButton()->click();
    check(requests == 2 && dock.m_meteor->progress() == phase,
          "A pause request must preserve its callback without restarting the meteor.");
    dock.setBusy(true);
    dock.playPauseButton()->click();
    check(requests == 2 && dock.m_meteor->progress() == phase,
          "A busy transport must neither issue a play callback nor restart the meteor.");
    dock.setBusy(false);
    dock.triggerPlaybackMeteor();
    check(dock.m_meteor->isRunning() && dock.m_meteor->progress() == 0,
          "A new track started outside the dock must be able to restart the meteor.");
    waitForEvents(PlaybackMeteor::DurationMs + 140);
    check(!dock.m_meteor->isRunning() && dock.m_meteor->isHidden(), "The meteor must fade away at its endpoint.");
    dock.onPlayPause = {};
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget host;
    host.resize(1180, 780);
    QLineEdit outside(&host);
    outside.setGeometry(50, 30, 280, 40);
    PlaybackDock dock(&host);
    dock.m_pointerTimer->stop();
    host.show();
    host.activateWindow();
    QCoreApplication::processEvents();
    verifyPointerAndFocus(host, dock, outside);
    verifyPinResizeAndAnimation(host, dock);
    verifySeeking(dock);
    verifyVolume(host, dock);
    verifyButtons(dock);
    verifyPopupHold(dock);
    verifyPlaybackMeteor(dock);
    return 0;
}
