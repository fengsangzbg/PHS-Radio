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
    tracks.last().title = QStringLiteral("Updated last song");
    // Refreshing the same large row count previously stalled the UI thread.
    window.setTracks(tracks);
    app.processEvents();
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
    window.setTracks({tracks.last()});
    app.processEvents();
    check(window.m_tracks->rowCount() == 1);
    check(window.m_tracks->item(0, 1)->text() == tracks.last().title);

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
