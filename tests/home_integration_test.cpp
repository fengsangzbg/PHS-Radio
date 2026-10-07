#include <QtWidgets>
#include <QtMultimedia>
#include <QtNetwork>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#define private public
#define main originalPlayerMain
#include "../main.cpp"
#undef main
#undef private

#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

void events(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool until(const std::function<bool()> &predicate, int timeout = 6000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout)
        events(10);
    return predicate();
}

QUrl writeWave(const QString &path)
{
    constexpr quint32 rate = 16000, bytes = rate * 12 * sizeof(qint16);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "The owned silent WAV must be writable.");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << rate << quint32(rate * 2)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    const QByteArray silence(bytes, '\0');
    stream.writeRawData(silence.constData(), silence.size());
    check(stream.status() == QDataStream::Ok, "The local audio fixture must be valid PCM.");
    return QUrl::fromLocalFile(path);
}

class DelayedCover final {
public:
    QTcpServer server;
    QPointer<QTcpSocket> pending;
    QByteArray image;
    explicit DelayedCover(const QByteArray &png) : image(png)
    {
        check(server.listen(QHostAddress::LocalHost, 0), "The local cover mock must be available.");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, &server, [this, socket] {
                const QByteArray request = socket->property("fixtureRequest").toByteArray() + socket->readAll();
                socket->setProperty("fixtureRequest", request);
                if (request.contains("\r\n\r\n"))
                    pending = socket; // Delivery is deliberately held until a newer cover is visible.
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/old-cover.png").arg(server.serverPort()));
    }
    void deliver()
    {
        check(bool(pending), "The old cover request must reach the controlled local server.");
        const QByteArray response = QByteArray("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: ")
            + QByteArray::number(image.size()) + "\r\nConnection: close\r\n\r\n" + image;
        pending->write(response);
        pending->disconnectFromHost();
    }
};

Track track(const QString &id, const QUrl &audio, const QUrl &cover)
{
    Track result;
    result.id = id;
    result.title = id;
    result.artist = QStringLiteral("Owned offline fixture");
    result.album = QStringLiteral("Original resolution covers");
    result.audioUrl = audio;
    result.coverUrl = cover;
    return result;
}
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PHS Radio Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Home Integration Test"));
    QTemporaryDir directory;
    check(directory.isValid(), "Home integration fixtures require a temporary directory.");
    const QUrl audio = writeWave(directory.filePath(QStringLiteral("silence.wav")));
    QImage oldImage(1200, 900, QImage::Format_RGB32);
    oldImage.fill(QColor(210, 25, 30));
    QByteArray oldPng;
    QBuffer oldOutput(&oldPng);
    check(oldOutput.open(QIODevice::WriteOnly) && oldImage.save(&oldOutput, "PNG"),
          "The older cover fixture must be a valid original PNG.");
    QImage newImage(1600, 1200, QImage::Format_RGB32);
    newImage.fill(QColor(25, 40, 210));
    const QString newPath = directory.filePath(QStringLiteral("new-original.png"));
    check(newImage.save(newPath), "The newer cover fixture must be a valid original PNG.");
    DelayedCover oldCover(oldPng);

    // QQ-only startup skips both real clients' session restoration. Playback
    // uses directly supplied local audio and the only HTTP request is loopback.
    PlayerWindow window({MusicPlatform::QQMusic}, true);
    window.m_dayTimer.stop();
    window.m_audioOutput->setMuted(true);
    window.m_audioOutput->setVolume(0);
    window.m_coverNetwork->setProxy(QNetworkProxy::NoProxy);
    const QNetworkProxy localOnly(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 1);
    window.m_kugouApi.m_network.setProxy(localOnly);
    window.m_kugouApi.m_playbackNetwork.setProxy(localOnly);
    window.m_kugouApi.m_libraryNetwork.setProxy(localOnly);
    window.show();
    events(50);
    check(window.m_contentPages->currentWidget() == window.m_homePage,
          "The actual application controller must open on its home page.");
    auto *home = window.findChild<QPushButton *>(QStringLiteral("homeButton"));
    check(home, "The real toolbar must expose its home navigation button.");
    window.m_navigationButtons[1]->click();
    check(window.m_contentPages->currentWidget() == window.m_libraryPage,
          "Selecting a playlist category must open the library page.");
    home->click();
    check(window.m_contentPages->currentWidget() == window.m_homePage,
          "The home button must return from the library to the actual home page.");
    window.m_playlistList->setCurrentRow(0);
    check(QMetaObject::invokeMethod(window.m_playlistList, "itemClicked", Qt::DirectConnection,
                                   Q_ARG(QListWidgetItem *, window.m_playlistList->item(0))),
          "The real playlist activation signal must be callable.");
    check(window.m_contentPages->currentWidget() == window.m_libraryPage,
          "Selecting a playlist must open the library page even when the row was already selected.");
    home->click();
    window.m_dock->songPageButton()->click();
    check(window.m_contentPages->currentWidget() == window.m_songPage,
          "The dock's record button must open the song page.");
    home->click();
    window.m_dock->lyricsButton()->click();
    check(window.m_contentPages->currentWidget() == window.m_songPage,
          "The dock's lyrics button must open the same song page and route its lyrics action.");

    const Track first = track(QStringLiteral("old-song"), audio, oldCover.url());
    const Track second = track(QStringLiteral("new-song"), audio, QUrl::fromLocalFile(newPath));
    window.m_dailyTracks.insert(static_cast<int>(MusicPlatform::NetEaseCloud), {first, second});
    window.playRecommendation(MusicPlatform::NetEaseCloud, 0);
    check(until([&] { return bool(oldCover.pending); }), "The old asynchronous cover must remain pending.");
    window.playRecommendation(MusicPlatform::NetEaseCloud, 1);
    check(until([&] { return window.m_songPage->coverSourceSize() == newImage.size()
        && window.m_player->playbackState() == QMediaPlayer::PlayingState; }),
          "The new recommendation must play local audio and retain its original 1600x1200 cover.");
    check(window.m_queuePlatform == MusicPlatform::NetEaseCloud && !window.m_queueUsesKugou
              && window.m_playbackQueue.current()->id == second.id,
          "Recommendation playback must retain its NetEase source independently of the playlist browser.");
    oldCover.deliver();
    check(until([&] { return !window.m_highResolutionPending.contains(oldCover.url().toString()); }),
          "The deliberately late old cover response must finish processing.");
    check(window.m_songPage->coverSourceSize() == newImage.size(),
          "A late cover for the previous recommendation must never overwrite the current song artwork.");
    check(window.m_songPage->m_lyricsMessage->text() != QStringLiteral("正在加载歌词…"),
          "Offline recommendation playback must finish its lyric state instead of remaining in perpetual loading.");

    Playlist background;
    background.id = QStringLiteral("background-library");
    background.name = QStringLiteral("Background refresh fixture");
    background.coverUrl = QUrl::fromLocalFile(newPath);
    background.tracks = {second};
    {
        const QSignalBlocker platformSignals(window.m_platform);
        window.m_providers.clear();
        window.m_providers.push_back(std::make_unique<PlaylistSnapshotProvider>(MusicPlatform::Kugou,
            QVector<Playlist>{background}));
        window.m_platform->clear();
        window.m_platform->addItem(platformName(MusicPlatform::Kugou));
        window.m_platform->setCurrentIndex(0);
    }
    window.m_kugouAccountConnected = true; // Fixture state; no cookie or authorization is supplied.
    window.m_kugouPlaylists = {background};
    window.m_kugouTrackCache.insert(background.id, background.tracks);
    window.m_completedPlaylists.insert(background.id);
    { const QSignalBlocker sections(window.m_navigation); window.m_navigation->setCurrentRow(3); }
    window.scheduleLibraryView();
    events(1650);
    check(window.m_contentPages->currentWidget() == window.m_songPage
              && window.m_playbackQueue.current()->id == second.id,
          "A completed background library refresh must not steal the song page or its playing queue.");

    window.resize(760, 560);
    window.m_navigationButtons[0]->click();
    events(180);
    check(window.m_contentPages->currentWidget() == window.m_libraryPage,
          "The actual minimum-size window must still navigate to the library.");
    QHeaderView *header = window.m_tracks->horizontalHeader();
    const QFontMetrics metrics(header->font());
    check(header->height() >= metrics.height() + 12,
          "Library column headings must have readable vertical space at minimum window size.");
    for (int section = 0; section < window.m_tracks->columnCount(); ++section) {
        const QString title = window.m_tracks->horizontalHeaderItem(section)->text();
        check(header->sectionSize(section) >= metrics.horizontalAdvance(title) + 12,
              "Each column heading must fit its actual section without cutting its text.");
    }
    check(window.width() == 760
              && window.m_libraryPage->rect().contains(QRect(window.m_tracks->pos(), window.m_tracks->size())),
          "The library table must fit inside the minimum-width page without expanding the top-level window.");
    // No engine is launched: exercise the actual controller's capture pause
    // state while local music continues through its separate media player.
    check(window.m_player->playbackState() == QMediaPlayer::PlayingState,
          "The local music fixture must still be playing before background activity checks.");
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&window, &deactivate);
    events(30);
    check(window.m_wallpaperCapture->m_paused,
          "Switching away from the application must pause its external wallpaper capture.");
    QEvent activate(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&window, &activate);
    check(!window.m_wallpaperCapture->m_paused,
          "Reactivating the application must restore its wallpaper capture activity.");
    window.m_pages->setCurrentWidget(window.m_loginPage);
    check(window.m_wallpaperCapture->m_paused,
          "Opening the login page must pause the hidden player background even though the main window is visible.");
    window.m_pages->setCurrentWidget(window.m_playerPage);
    check(!window.m_wallpaperCapture->m_paused,
          "Returning to the visible player must restore background capture activity.");
    window.hide();
    check(window.m_wallpaperCapture->m_paused
              && window.m_player->playbackState() == QMediaPlayer::PlayingState,
          "Hiding the application must pause the wallpaper independently of the playing music.");
    window.m_player->stop();
    window.m_player->setSource(QUrl());
    return 0;
}
