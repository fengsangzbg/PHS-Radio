#include <QtWidgets>
#include "../home_page.h"
#include "../song_page.h"
#include "../playback_dock.h"
#include "../aero_widgets.h"
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) { qCritical("%s", message); std::abort(); }
}

void waitForEvents(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void clickItem(QListWidget *list, int row)
{
    const QPoint position = list->visualItemRect(list->item(row)).center();
    QWidget *viewport = list->viewport();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(position),
        QPointF(viewport->mapToGlobal(position)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position),
        QPointF(viewport->mapToGlobal(position)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &press); QApplication::sendEvent(viewport, &release);
}

void verifyLocalClock()
{
    HomePage home;
    home.resize(1180, 840);
    home.show();
    QCoreApplication::processEvents();
    const QDate date(2026, 10, 8);
    const QTime time(12, 34, 56);
    const QDateTime nepal(date, time, QTimeZone::fromSecondsAheadOfUtc(5 * 3600 + 45 * 60));
    home.refreshClock(nepal);
    check(home.clockLabel()->text() == QStringLiteral("12:34:56"), "Clock must display the supplied local wall time.");
    check(home.dateLabel()->text().contains(QStringLiteral("10月8日")), "Calendar must display the local date.");
    check(home.timeZoneLabel()->text().contains(QStringLiteral("UTC+05:45")), "Quarter-hour UTC offsets must be preserved.");
    const QDateTime newfoundland(date, time, QTimeZone::fromSecondsAheadOfUtc(-3 * 3600 - 30 * 60));
    check(HomePage::utcOffsetText(newfoundland) == QStringLiteral("UTC-03:30"), "Negative half-hour offsets must retain their sign.");
    const QTimeZone eastern(QByteArray("America/New_York"));
    if (eastern.isValid()) {
        const QDateTime winter(QDate(2026, 1, 15), time, eastern);
        const QDateTime summer(QDate(2026, 7, 15), time, eastern);
        check(HomePage::utcOffsetText(winter) == QStringLiteral("UTC-05:00")
            && HomePage::utcOffsetText(summer) == QStringLiteral("UTC-04:00"),
            "Time zone formatting must respect the date's daylight-saving offset.");
    }
    const QDateTime local = QDateTime::currentDateTime();
    home.refreshClock(local);
    check(home.timeZoneLabel()->text().endsWith(HomePage::utcOffsetText(local)),
          "The runtime clock must follow the computer's actual local UTC offset.");

    auto *kugou = home.findChild<QListWidget *>(QStringLiteral("kugouRecommendations"));
    auto *netease = home.findChild<QListWidget *>(QStringLiteral("neteaseRecommendations"));
    check(kugou && netease && kugou->count() == 0 && netease->count() == 0,
          "Disconnected recommendation cards must not contain fabricated songs.");
    Track first; first.title = QStringLiteral("Real recommendation"); first.artist = QStringLiteral("Artist");
    Track second = first; second.title = QStringLiteral("Second recommendation");
    home.setRecommendations(MusicPlatform::Kugou, {first, second});
    home.setRecommendations(MusicPlatform::NetEaseCloud, {second});
    int activated = -1;
    MusicPlatform activatedPlatform = MusicPlatform::QQMusic;
    home.onRecommendationActivated = [&](MusicPlatform platform, int index) {
        activatedPlatform = platform; activated = index;
    };
    QCoreApplication::processEvents();
    clickItem(kugou, 1);
    check(activated == 1 && activatedPlatform == MusicPlatform::Kugou,
          "Clicking a recommendation must preserve the correct source platform and index.");
    check(netease->count() == 1 && netease->item(0)->text() == second.title,
          "Each platform must keep its own recommendation snapshot.");
    home.setRecommendations(MusicPlatform::Kugou, {});
    check(kugou->count() == 0 && netease->count() == 1,
          "Clearing an account's recommendations must not affect another platform.");
}

void verifyRecordAndLyrics()
{
    SongPage page;
    page.resize(1180, 840); page.show();
    QCoreApplication::processEvents();
    Track track; track.title = QStringLiteral("Actual song"); track.artist = QStringLiteral("Actual artist");
    page.setTrack(track);
    QPixmap fullResolution(1200, 1200); fullResolution.fill(QColor("#e7528e"));
    fullResolution.setDevicePixelRatio(2);
    page.setCover(fullResolution);
    check(page.coverSourceSize() == QSize(1200, 1200),
          "The record must retain downloaded high-resolution artwork rather than a list thumbnail.");
    page.setPlaying(true);
    check(page.rotationActive(), "Visible playing records must rotate.");
    waitForEvents(140);
    check(page.recordAngle() > .5, "Rotation must advance with real elapsed time.");
    page.setPlaying(false);
    const qreal pausedAngle = page.recordAngle();
    waitForEvents(140);
    check(!page.rotationActive() && page.recordAngle() == pausedAngle,
          "Pausing must freeze the record without resetting its angle.");
    page.setPlaying(true); page.hide();
    const qreal hiddenAngle = page.recordAngle();
    waitForEvents(100);
    check(!page.rotationActive() && page.recordAngle() == hiddenAngle,
          "Hidden song pages must stop their frame timer.");
    page.show(); waitForEvents(100);
    check(page.rotationActive() && page.recordAngle() > hiddenAngle,
          "Showing an already-playing record must resume from the saved angle.");
    page.setPlaying(false);

    QVector<TimedLyricLine> lines;
    for (int row = 0; row < 100; ++row)
        lines.append({1000 + row * 1000, QStringLiteral("Lyric line %1").arg(row),
                      row == 60 ? QStringLiteral("Translated line") : QString()});
    std::reverse(lines.begin(), lines.end());
    page.setPosition(61000);
    QString observed;
    int notifications = 0;
    page.onCurrentLyricChanged = [&](const QString &text) { observed = text; ++notifications; };
    page.setLyrics(lines);
    check(page.currentLyricIndex() == 60 && observed == QStringLiteral("Lyric line 60"),
          "Late asynchronous lyrics must immediately align to the existing playback position.");
    waitForEvents(320);
    const QRect activeRect = page.lyricsList()->visualItemRect(page.lyricsList()->item(60));
    check(page.lyricsList()->verticalScrollBar()->value() > 0
          && std::abs(activeRect.center().y() - page.lyricsList()->viewport()->height() / 2) < 6,
          "Active timed lyrics must scroll into the center of the visible lyric panel.");
    const int previousNotifications = notifications;
    page.setPosition(61500);
    check(notifications == previousNotifications, "Repeated position events within one line must not re-emit lyric changes.");
    page.setPosition(500);
    check(page.currentLyricIndex() == -1 && page.currentLyric().isEmpty(),
          "Playback before the first lyric timestamp must leave all lines inactive.");
    waitForEvents(300);
    check(page.lyricsList()->verticalScrollBar()->value() == 0,
          "Seeking back before the first lyric must return the panel to the beginning.");
    page.setPosition(61000); waitForEvents(300);
    qint64 requestedPosition = -1;
    page.onSeekRequested = [&](qint64 position) { requestedPosition = position; };
    page.setSeekable(false); clickItem(page.lyricsList(), 60);
    check(requestedPosition == -1, "Unavailable seeking must not issue a lyric seek request.");
    page.setSeekable(true); clickItem(page.lyricsList(), 60);
    check(requestedPosition == 61000, "Clicking a lyric must request that line's original timestamp.");
    page.setPosition(119000);
    check(page.currentLyricIndex() == 99, "Seeking beyond the last timestamp must highlight the final lyric line.");
    page.setTrack(track, MusicPlatform::NetEaseCloud);
    check(page.currentLyric().isEmpty() && page.coverSourceSize().isEmpty()
          && page.lyricsList()->count() == 0, "Changing songs must clear stale artwork and lyrics.");
    bool backRequested = false;
    page.onBackRequested = [&] { backRequested = true; };
    page.findChild<QPushButton *>(QStringLiteral("songPageBack"))->click();
    check(backRequested, "The song page's back control must notify its controller.");
}

void verifyDockPageLinks()
{
    QWidget host; host.resize(1180, 840);
    PlaybackDock dock(&host);
    bool songRequested = false, lyricsRequested = false;
    dock.onSongPageRequested = [&] { songRequested = true; };
    dock.onLyricsRequested = [&] { lyricsRequested = true; };
    dock.songPageButton()->click(); dock.lyricsButton()->click();
    check(songRequested && lyricsRequested, "The dock must expose independent record and lyric page interactions.");
    dock.setTrack(QStringLiteral("Song"), QStringLiteral("Artist"));
    dock.setCurrentLyric(QStringLiteral("Current lyric\nsecond fragment"));
    check(dock.currentLyric() == QStringLiteral("Current lyric second fragment"),
          "The dock lyric preview must remain a single line.");
    dock.setTrack(QStringLiteral("Next song"), QStringLiteral("Next artist"));
    check(dock.currentLyric().isEmpty(), "Changing the dock track must clear the previous song's lyric preview.");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    verifyLocalClock();
    verifyRecordAndLyrics();
    verifyDockPageLinks();
    return 0;
}
