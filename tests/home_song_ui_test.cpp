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

void verifyResponsiveRecommendations()
{
    HomePage home;
    QVector<Track> tracks;
    for (int row = 0; row < 30; ++row) {
        Track track;
        track.title = QStringLiteral("每日推荐歌曲 %1 — 完整标题可通过提示查看").arg(row);
        track.artist = QStringLiteral("歌手 %1").arg(row);
        tracks.append(track);
    }
    home.setRecommendations(MusicPlatform::Kugou, tracks);
    home.setRecommendations(MusicPlatform::NetEaseCloud, tracks);
    home.resize(1180, 840);
    home.show();
    waitForEvents(80);
    auto *scroll = home.findChild<QScrollArea *>(QStringLiteral("homeScrollArea"));
    auto *kugou = home.findChild<QListWidget *>(QStringLiteral("kugouRecommendations"));
    auto *netease = home.findChild<QListWidget *>(QStringLiteral("neteaseRecommendations"));
    check(scroll && kugou && netease, "The home page must expose its scrollable recommendation strips.");
    check(!kugou->isWrapping() && kugou->horizontalScrollBar()->maximum() > 0,
          "All daily recommendations must remain reachable in one horizontal strip.");

    home.showMaximized();
    waitForEvents(50);
    home.showNormal();
    home.resize(760, 420);
    waitForEvents(100);
    check(home.width() == 760 && home.height() == 420,
          "Restoring a compact window must not be blocked by large recommendation minimum hints.");
    check(home.rect().contains(QRect(scroll->pos(), scroll->size()))
          && scroll->verticalScrollBar()->maximum() > 0,
          "Compact windows must scroll the complete home page instead of clipping its content.");
    const QRect first = kugou->visualItemRect(kugou->item(0));
    const QRect lastBeforeScroll = kugou->visualItemRect(kugou->item(29));
    check(first.height() <= kugou->viewport()->height()
          && first.top() == lastBeforeScroll.top(),
          "Recommendation covers and both text lines must fit a complete single-row strip.");
    check(netease->mapTo(scroll->widget(), QPoint()).y()
              > kugou->mapTo(scroll->widget(), QPoint()).y(),
          "Narrow two-platform home pages must stack their recommendation sections.");
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    kugou->scrollToItem(kugou->item(29), QAbstractItemView::PositionAtCenter);
    QCoreApplication::processEvents();
    check(kugou->viewport()->rect().contains(kugou->visualItemRect(kugou->item(29))),
          "The last of thirty recommendations must be fully reachable horizontally.");
    check(netease->mapTo(scroll->viewport(), QPoint()).y() < scroll->viewport()->height(),
          "Scrolling a short window must reveal the second platform's recommendations.");

    // Render a non-square source cover and measure its visible central rows and
    // columns: the artwork itself must retain a square card aperture.
    QPixmap artwork(300, 150);
    artwork.fill(Qt::magenta);
    home.setCover(MusicPlatform::Kugou, 0, artwork);
    QStyleOptionViewItem option;
    option.initFrom(kugou);
    const QSize itemSize = kugou->itemDelegate()->sizeHint(option, kugou->model()->index(0, 0));
    option.rect = QRect(QPoint(), itemSize);
    QImage image(itemSize, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    kugou->itemDelegate()->paint(&painter, option, kugou->model()->index(0, 0));
    painter.end();
    const int middle = 14 + (itemSize.width() - 28) / 2;
    int coloredWidth = 0, coloredHeight = 0;
    for (int x = 0; x < image.width(); ++x)
        coloredWidth += image.pixelColor(x, middle) == QColor(Qt::magenta);
    for (int y = 0; y < image.height(); ++y)
        coloredHeight += image.pixelColor(middle, y) == QColor(Qt::magenta);
    check(coloredWidth > 100 && std::abs(coloredWidth - coloredHeight) <= 1,
          "Recommendation artwork must remain square after resizing, even for rectangular source images.");

    home.setKugouOnly(true);
    home.resize(360, 420);
    waitForEvents(80);
    check(QFontMetrics(home.clockLabel()->font()).horizontalAdvance(home.clockLabel()->text())
              <= home.clockLabel()->width(),
          "The digital clock must resize to fit compact page widths.");
    kugou->horizontalScrollBar()->setValue(0);
    const QPoint center = kugou->viewport()->rect().center();
    QWheelEvent horizontalWheel(QPointF(center), QPointF(kugou->viewport()->mapToGlobal(center)),
        QPoint(), QPoint(0, -120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(kugou->viewport(), &horizontalWheel);
    check(kugou->horizontalScrollBar()->value() > 0,
          "Shift and the mouse wheel must reach more recommendations horizontally.");
    const int horizontalPosition = kugou->horizontalScrollBar()->value();
    scroll->verticalScrollBar()->setValue(0);
    QWheelEvent verticalWheel(QPointF(center), QPointF(kugou->viewport()->mapToGlobal(center)),
        QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(kugou->viewport(), &verticalWheel);
    QCoreApplication::processEvents();
    check(scroll->verticalScrollBar()->value() > 0
              && kugou->horizontalScrollBar()->value() == horizontalPosition,
          "Ordinary vertical wheel input over recommendations must scroll the whole home page.");
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
    verifyResponsiveRecommendations();
    verifyRecordAndLyrics();
    verifyDockPageLinks();
    return 0;
}
