#include <QtCore>
#include <QtNetwork>
#include <QtGui>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#define private public
#include "../netease_api_client.cpp"
#undef private
#include "../netease_music_provider.cpp"
namespace {
void verify(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::fflush(stderr); std::abort(); }
}

template<class Operation> void awaitCallback(Operation operation)
{
    bool done = false;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    operation([&] { verify(!done, "Every API operation must complete once."); done = true; loop.quit(); });
    if (!done) { deadline.start(5000); loop.exec(); }
    verify(done, "A mocked local API operation timed out.");
}

struct MockService {
    QTcpServer server;
    int requests = 0;
    QStringList paths;
    std::function<QJsonObject(const QString &, const QJsonObject &, const QByteArray &)> respond;
    MockService()
    {
        verify(server.listen(QHostAddress::LocalHost), "A local mock server must bind.");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto bytes = std::make_shared<QByteArray>();
                auto answered = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes, answered] {
                    bytes->append(socket->readAll());
                    const int end = bytes->indexOf("\r\n\r\n");
                    if (*answered || end < 0) return;
                    const QByteArray header = bytes->left(end);
                    const auto length = QRegularExpression("content-length:\\s*(\\d+)")
                        .match(QString::fromLatin1(header).toLower());
                    const int count = length.hasMatch() ? length.captured(1).toInt() : 0;
                    if (bytes->size() < end + 4 + count) return;
                    *answered = true;
                    const QUrl url(QString::fromLatin1(header.split('\n').first().split(' ').value(1)));
                    const QString path = url.path();
                    paths.append(path); ++requests;
                    const auto body = QJsonDocument::fromJson(bytes->mid(end + 4, count)).object();
                    const QByteArray payload = QJsonDocument(respond(path, body, header)).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QNetworkProxy proxy() const { return {QNetworkProxy::HttpProxy, "127.0.0.1", server.serverPort()}; }
};


}
int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QCoreApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("PHSRadioTests"); app.setApplicationName("NeteaseLibraryApiTest");
    MockService service;
    bool missingMetadata = false;
    QVector<int> offsets;
    int detailBatches = 0;
    auto song = [](int id) {
        return QJsonObject{{"id", id}, {"name", QStringLiteral("歌曲 %1").arg(id)},
            {"tns", QJsonArray{QStringLiteral("无梦之梦")}},
            {"ar", QJsonArray{QJsonObject{{"name", QStringLiteral("春卷饭")}}}},
            {"al", QJsonObject{{"id", 9}, {"name", "Album"}, {"picUrl", "https://p1.music.126.net/original.jpg"}}}};
    };
    service.respond = [&](const QString &route, const QJsonObject &body, const QByteArray &header) -> QJsonObject {
        verify(header.contains("MUSIC_U=fixture") && !header.contains("token=kugou"),
               "NetEase operations must use only their own account credentials.");
        if (route == "/user/playlist") {
            const int offset = body.value("offset").toInt();
            offsets.append(offset);
            auto list = [](int id, int creator, int count, int special = 0) {
                return QJsonObject{{"id", id}, {"name", QString::number(id)}, {"trackCount", count},
                    {"specialType", special}, {"creator", QJsonObject{{"userId", creator}}}};
            };
            return {{"code", 200}, {"more", offset == 0}, {"playlist", offset == 0
                ? QJsonArray{list(11, 42, 1005), list(22, 7, 2)}
                : QJsonArray{list(22, 7, 2), list(33, 42, 0, 5)}}};
        }
        if (route == "/playlist/detail") {
            const QString id = body.value("id").toString();
            QJsonArray ids;
            const int count = id == "11" ? 1005 : id == "22" ? 2 : 0;
            for (int index = 1; index <= count; ++index)
                ids.append(QJsonObject{{"id", id == "22" ? 2000 + index : index}});
            return {{"code", 200}, {"playlist", QJsonObject{{"trackIds", ids}, {"trackCount", count},
                {"tracks", QJsonArray{song(1)}}}}}; // Metadata intentionally contains only the first song.
        }
        if (route == "/song/detail") {
            ++detailBatches;
            auto ids = body.value("ids").toString().split(',');
            verify(ids.size() <= 100, "Song detail requests must be bounded batches.");
            QJsonArray rows;
            for (auto it = ids.crbegin(); it != ids.crend(); ++it) {
                const int id = it->toInt();
                if (missingMetadata && id == 2002) continue;
                rows.append(song(id));
            }
            return {{"code", 200}, {"songs", rows}};
        }
        if (route == "/cloudsearch") {
            const int offset = body.value("offset").toInt();
            verify(body.value("keywords").toString() == QStringLiteral("初音未来"), "Search must preserve UTF-8 input.");
            QJsonArray rows;
            for (int index = offset; index < qMin(offset + 30, 31); ++index) rows.append(song(3000 + index));
            return {{"code", 200}, {"result", QJsonObject{{"songCount", 31}, {"songs", rows}}}};
        }
        verify(false, "The test received an unexpected NetEase operation.");
        return {};
    };
    NeteaseApiClient client;
    client.m_network.setProxy(service.proxy());
    client.m_loggedIn = true; client.m_userId = "42"; client.m_cookie = "MUSIC_U=fixture";
    awaitCallback([&](auto done) {
        client.onPlaylistsReady = [done](const QVector<Playlist> &lists) {
            verify(lists.size() == 3 && lists[0].kind == PlaylistKind::Created
                && lists[1].kind == PlaylistKind::Favorite && lists[2].isLiked,
                "Account pagination must retain owned, subscribed and liked playlists without duplicate IDs.");
            done();
        };
        client.loadPlaylists();
    });
    client.onPlaylistsReady = {};
    verify(offsets == QVector<int>{0, 2}, "Pagination must advance by the actual returned page length.");
    const Playlist large = client.playlists()[0], partial = client.playlists()[1], empty = client.playlists()[2];
    awaitCallback([&](auto done) { client.fetchPlaylistTracks(large, [&, done](const QVector<Track> &tracks, const QString &error) {
        verify(error.isEmpty() && tracks.size() == 1005 && tracks.first().id == "1" && tracks.last().id == "1005",
            "A playlist larger than 1000 songs must load every ID and restore playlist order from out-of-order metadata.");
        verify(tracks.first().searchAliases.contains(QStringLiteral("无梦之梦"))
            && tracks.first().coverUrl.toString().endsWith("/original.jpg"), "Translations and original artwork must be retained.");
        done();
    }); });
    verify(detailBatches == 11, "1005 tracks must use eleven bounded song-detail batches.");
    const int beforeCached = service.requests;
    awaitCallback([&](auto done) { client.fetchPlaylistTracks(large, [done](const QVector<Track> &tracks, const QString &error) {
        verify(error.isEmpty() && tracks.size() == 1005, "A complete playlist cache must retain every song."); done();
    }); });
    verify(service.requests == beforeCached, "A completed playlist should not redownload metadata on every click.");
    missingMetadata = true;
    awaitCallback([&](auto done) { client.fetchPlaylistTracks(partial, [done](const QVector<Track> &tracks, const QString &error) {
        verify(!error.isEmpty() && tracks.size() == 2 && tracks[1].id == "2002"
            && tracks[1].title.contains(QStringLiteral("暂不可用")), "Unavailable metadata must retain its song position with an explicit partial-load error."); done();
    }); });
    verify(!client.m_loadedPlaylists.contains(partial.id), "Partial results must remain retryable.");
    missingMetadata = false;
    awaitCallback([&](auto done) { client.fetchPlaylistTracks(partial, [done](const QVector<Track> &tracks, const QString &error) {
        verify(error.isEmpty() && tracks.size() == 2 && !tracks[1].title.contains(QStringLiteral("暂不可用")), "Retry must replace missing metadata with actual platform data."); done();
    }); });
    const int beforeEmpty = detailBatches;
    awaitCallback([&](auto done) { client.fetchPlaylistTracks(empty, [done](const QVector<Track> &tracks, const QString &error) {
        verify(error.isEmpty() && tracks.isEmpty(), "An empty playlist must load successfully."); done();
    }); });
    verify(detailBatches == beforeEmpty, "An empty playlist must never query an empty song-ID batch.");
    NeteaseMusicProvider provider(&client);
    verify(provider.likedTracks().size() == 1007 && !provider.search(QStringLiteral("无梦之梦")).isEmpty(),
        "The account provider must expose all loaded playlists and translated-name local search.");
    for (int page : {1, 2}) awaitCallback([&](auto done) {
        provider.searchCatalog(QStringLiteral("初音未来"), page, [done, page](const CatalogSearchPage &result, const QString &error) {
            verify(error.isEmpty() && result.total == 31 && result.tracks.size() == (page == 1 ? 30 : 1)
                && result.hasMore == (page == 1) && result.tracks.first().platform == MusicPlatform::NetEaseCloud,
                "Catalog search must paginate and tag every song with its actual platform."); done();
        });
    });
    client.m_loadedPlaylists.remove(large.id);
    awaitCallback([&](auto done) {
        client.fetchPlaylistTracks(large, [done](const QVector<Track> &tracks, const QString &error) {
            verify(!error.isEmpty() && tracks.isEmpty(), "An old-account playlist reply must be discarded after logout."); done();
        });
        client.logout();
    });
    verify(client.playlists().isEmpty() && provider.playlists().isEmpty(), "Logout must clear only this platform's account library.");
    return 0;
}
