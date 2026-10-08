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
#include <cstdlib>

void check(bool value) { if (!value) std::abort(); }

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("PHS Radio Tests");
    QCoreApplication::setApplicationName("Library View Test");
    PlayerWindow window({MusicPlatform::QQMusic}, true);
    window.show();
    QVector<Track> tracks;
    for (int row = 0; row < 12042; ++row) {
        Track track;
        track.id = QString::number(row);
        track.title = QStringLiteral("Song %1").arg(row);
        track.artist = QStringLiteral("Artist");
        track.album = QStringLiteral("Album");
        tracks.append(track);
    }
    window.setTracks(tracks);
    app.processEvents();
    check(window.m_tracks->rowCount() == tracks.size());
    check(window.m_tracks->item(tracks.size() - 1, 1)->text() == tracks.last().title);
    QTableWidgetItem *lastSongItem = window.m_tracks->item(tracks.size() - 1, 1);
    QTableWidgetItem *firstCoverItem = window.m_tracks->item(0, 0);
    tracks.last().title = QStringLiteral("Updated last song");
    // Refreshing the same large row count previously stalled the UI thread.
    window.setTracks(tracks);
    app.processEvents();
    check(window.m_tracks->item(tracks.size() - 1, 1) == lastSongItem);
    check(window.m_tracks->item(0, 0) == firstCoverItem);
    window.m_tracks->setCurrentCell(tracks.size() - 1, 1);
    check(window.m_tracks->currentRow() == tracks.size() - 1);
    check(window.m_tracks->currentItem()->text() == tracks.last().title);
    check(window.m_visibleTracks.last().title == tracks.last().title);
    QTableWidgetItem *selectedItem = window.m_tracks->currentItem();
    const int scrollPosition = window.m_tracks->verticalScrollBar()->value();
    tracks.last().audioUrl = QUrl(QStringLiteral("https://example.invalid/refreshed-source"));
    tracks.last().searchAliases.append(QStringLiteral("New alias"));
    window.setTracks(tracks);
    app.processEvents();
    check(window.m_tracks->currentItem() == selectedItem);
    check(window.m_tracks->verticalScrollBar()->value() == scrollPosition);
    check(window.m_visibleTracks.last().audioUrl == tracks.last().audioUrl);
    check(window.m_visibleTracks.last().searchAliases == tracks.last().searchAliases);

    Track appended;
    appended.id = QStringLiteral("appended-song");
    appended.title = QStringLiteral("Appended song");
    tracks.append(appended);
    window.setTracks(tracks);
    app.processEvents();
    check(window.m_tracks->rowCount() == tracks.size());
    check(window.m_tracks->item(tracks.size() - 2, 1) == selectedItem);
    check(window.m_tracks->currentItem() == selectedItem);
    check(window.m_tracks->item(tracks.size() - 1, 1)->text() == appended.title);

    // Duplicate rows share one row lookup and receive the same completed icon.
    Track duplicate = tracks.first();
    duplicate.coverUrl = QUrl(QStringLiteral("https://example.invalid/shared-cover"));
    window.setTracks({duplicate, tracks.last(), duplicate});
    QPixmap blue(80, 80);
    blue.fill(Qt::blue);
    const QIcon completedCover(blue);
    window.m_readyTrackCovers.insert(coverRequestKey(duplicate), completedCover);
    window.flushTrackCovers();
    check(window.m_tracks->item(0, 0)->icon().cacheKey() == completedCover.cacheKey());
    check(window.m_tracks->item(2, 0)->icon().cacheKey() == completedCover.cacheKey());

    // Appending a duplicate after the original request was applied must reuse
    // its cover: the shared request marker intentionally prevents another fetch.
    const QString sharedCoverKey = coverRequestKey(duplicate);
    window.m_requestedCovers.insert(sharedCoverKey);
    const quint64 coverGeneration = window.m_trackCoverGeneration;
    QVector<Track> appendedDuplicate = window.m_visibleTracks;
    appendedDuplicate.append(duplicate);
    window.setTracks(appendedDuplicate);
    app.processEvents();
    check(window.m_trackCoverGeneration == coverGeneration);
    check(window.m_readyTrackCovers.isEmpty());
    check(window.m_requestedCovers.contains(sharedCoverKey));
    check(window.m_tracks->item(3, 0)->icon().cacheKey() == completedCover.cacheKey());
    const QString thumbnailKey = duplicate.coverUrl.toString() + QLatin1Char('|')
        + QString::number(qCeil(80 * window.m_surface->devicePixelRatioF()));
    check(!window.m_pendingCovers.contains(thumbnailKey));

    window.m_readyTrackCovers.insert(coverRequestKey(duplicate), completedCover);
    window.setTracks({tracks.last()});
    app.processEvents();
    check(window.m_readyTrackCovers.isEmpty()); // Switching lists discards stale queued covers.
    check(window.m_tracks->rowCount() == 1);
    check(window.m_tracks->item(0, 1)->text() == tracks.last().title);

    // A monitor density change must discard catalog icons as well as request
    // markers; otherwise the URL cache would keep displaying old-size pixels.
    window.m_catalogCoverIcons.insert(QStringLiteral("obsolete-dpr-cover"), completedCover);
    window.m_requestedCatalogCovers.insert(QStringLiteral("obsolete-dpr-cover"));
    window.m_thumbnailDpr = window.m_surface->devicePixelRatioF() + 1;
    window.refreshDevicePixelRatioAssets();
    check(window.m_catalogCoverIcons.isEmpty());
    check(!window.m_requestedCatalogCovers.contains(QStringLiteral("obsolete-dpr-cover")));

    Track miku, unrelated;
    miku.id = QStringLiteral("miku");
    miku.title = QStringLiteral("World Is Mine");
    miku.artist = QStringLiteral("初音ミク");
    miku.searchAliases = {QStringLiteral("世界第一的公主殿下")};
    unrelated.id = QStringLiteral("other-miku");
    unrelated.title = QStringLiteral("Another Song");
    unrelated.artist = QStringLiteral("Miku Ito");
    Playlist list;
    list.id = QStringLiteral("offline-test");
    list.name = QStringLiteral("我的测试歌单");
    list.tracks = {miku, unrelated};
    window.m_providers.clear();
    window.m_providers.push_back(std::make_unique<PlaylistSnapshotProvider>(MusicPlatform::QQMusic,
        QVector<Playlist>{list}));
    window.m_navigation->setCurrentRow(3);
    check(window.m_tracks->rowCount() == 2);
    window.rememberSelectedPlaylist(list);
    window.applyPlaylistSearch();
    for (const QString &query : {QStringLiteral("初音未来"), QStringLiteral("cywl 世界 第一")}) {
        window.m_playlistSearch->setText(query);
        QEventLoop events;
        QTimer::singleShot(350, &events, &QEventLoop::quit);
        events.exec();
        check(window.m_tracks->rowCount() == 1);
        check(window.m_tracks->item(0, 1)->text() == miku.title);
    }
    window.m_playlistSearch->clear();
    check(window.m_tracks->rowCount() == 2);
    window.m_search->setText(QStringLiteral("周杰伦"));
    {
        QEventLoop events;
        QTimer::singleShot(350, &events, &QEventLoop::quit);
        events.exec();
    }
    check(window.m_tracks->rowCount() == 2);
    check(window.m_catalogTracks.isEmpty());
    check(window.m_catalogPanel->isVisible());
    bool kugouLoginExplained = false;
    for (QLabel *label : window.m_catalogPanel->findChildren<QLabel *>()) {
        kugouLoginExplained |= label->text().contains(QStringLiteral("登录"))
            && label->text().contains(QStringLiteral("酷狗"));
        check(!label->text().contains(QStringLiteral("QQ 音乐在线曲库搜索尚未接入")));
    }
    check(kugouLoginExplained);
    check(window.m_search->placeholderText().contains(QStringLiteral("酷狗")));
    window.m_search->clear();
    check(window.m_tracks->rowCount() == 2);

    Track outside = miku;
    outside.id = QStringLiteral("outside");
    outside.title = QStringLiteral("Outside the selected playlist");
    Playlist otherList;
    otherList.id = QStringLiteral("other-list");
    otherList.name = QStringLiteral("另一个歌单");
    otherList.tracks = {outside};
    window.m_providers.clear();
    window.m_providers.push_back(std::make_unique<PlaylistSnapshotProvider>(MusicPlatform::QQMusic,
        QVector<Playlist>{list, otherList}));
    window.m_navigation->setCurrentRow(0);
    window.refresh(); // The local filter already selected this navigation row.
    check(window.m_playlistList->parentWidget() == window.m_drawer);
    check(window.m_playlistList->count() == 2);
    check(window.m_tracks->rowCount() == 2);
    window.m_playlistSearch->setText(QStringLiteral("初音未来"));
    {
        QEventLoop events;
        QTimer::singleShot(350, &events, &QEventLoop::quit);
        events.exec();
    }
    check(window.m_tracks->rowCount() == 1);
    check(window.m_visibleTracks.first().id == miku.id);
    window.m_playlistSearch->clear();
    check(window.m_tracks->rowCount() == 2);
    window.m_playlistList->setCurrentRow(1);
    check(window.m_tracks->rowCount() == 1);
    check(window.m_visibleTracks.first().id == outside.id);
    window.m_playlistSearch->setText(QStringLiteral("cywl"));
    {
        QEventLoop events;
        QTimer::singleShot(350, &events, &QEventLoop::quit);
        events.exec();
    }
    check(window.m_visibleTracks.first().id == outside.id);
    window.m_search->setText(QStringLiteral("初音未来"));
    {
        QEventLoop events;
        QTimer::singleShot(350, &events, &QEventLoop::quit);
        events.exec();
    }
    check(window.m_playlistSearch->text().isEmpty());
    check(window.m_tracks->rowCount() == 1); // Online lookup preserves the selected playlist view.
    check(window.m_visibleTracks.first().id == outside.id);
    check(window.m_catalogTracks.isEmpty()); // Online search requires an authorized Kugou provider.
    window.m_search->clear();
    check(window.m_playlistList->currentRow() == 1);
    check(window.m_visibleTracks.first().id == outside.id);
    return 0;
}
