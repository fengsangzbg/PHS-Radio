#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#include <algorithm>
#include <functional>
#include <memory>
#define private public
#include "../kugou_api_client.cpp"
#undef private
#include "../playlist_snapshot_provider.cpp"
#include <cstdlib>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

QJsonObject searchSong()
{
    return {{"Hash", "ABCDEF0123456789"}, {"SongName", QStringLiteral("日语原名")},
            {"SingerName", "Example Artist"}, {"AlbumName", "Example Album"},
            {"AlbumID", 777}, {"MixSongID", 987654},
            {"FileName", QStringLiteral("Example Artist - 日语原名")},
            {"aliases", QJsonArray{QStringLiteral("中文别名")}}};
}

void verifySearchMetadata()
{
    const auto parsed = parseTracks(QJsonObject{{"data", QJsonObject{{"info", QJsonArray{searchSong()}}}}});
    check(parsed.size() == 1, "Capitalized search metadata must produce a track.");
    const Track &track = parsed.first();
    check(track.title == QStringLiteral("日语原名") && track.artist == "Example Artist"
              && track.album == "Example Album", "Search display metadata must parse correctly.");
    check(track.hash == "ABCDEF0123456789" && track.albumId == "777"
              && track.albumAudioId == "987654", "Search recording identifiers must be retained exactly.");
    check(track.searchAliases.contains(QStringLiteral("中文别名"))
              && track.searchAliases.contains(QStringLiteral("Example Artist - 日语原名")),
          "Explicit aliases and alternate file names must be searchable metadata.");

    QJsonObject mixed = searchSong();
    mixed.insert("songname", "Original Playlist Title");
    mixed.insert("author_name", "Original Playlist Artist");
    mixed.insert("album_name", "Original Playlist Album");
    mixed.insert("hash", "original-hash");
    mixed.insert("album_id", "original-album");
    mixed.insert("album_audio_id", "original-audio");
    const auto playlist = parseTracks(QJsonObject{{"info", QJsonArray{mixed}}});
    check(playlist.size() == 1 && playlist.first().title == "Original Playlist Title"
              && playlist.first().artist == "Original Playlist Artist"
              && playlist.first().album == "Original Playlist Album",
          "Search compatibility must not replace ordinary playlist display fields.");
    check(playlist.first().hash == "original-hash" && playlist.first().albumId == "original-album"
              && playlist.first().albumAudioId == "original-audio",
          "Existing playlist identifiers must keep their original precedence.");
    check(playlist.first().searchAliases.contains(QStringLiteral("日语原名")),
          "A capitalized alternate title must remain available as an alias.");
    const auto onlyId = parseTracks(QJsonObject{{"payload", QJsonArray{QJsonObject{
        {"MixSongID", "123"}, {"SongName", "ID-only result"}}}}});
    check(onlyId.size() == 1 && onlyId.first().albumAudioId == "123",
          "Search results identified by MixSongID alone must be discoverable.");
    check(catalogSearchTotal(QJsonObject{{"data", QJsonObject{{"total", "61"}}}}) == 61
              && catalogSearchTotal(QJsonObject{{"total", 0}}) == 0,
          "Catalog totals must accept nested string counts and top-level numeric zero.");
    for (const QJsonValue &invalid : {QJsonValue(true), QJsonValue(-1), QJsonValue(2.5),
                                     QJsonValue("invalid"), QJsonValue(3000000000.0)})
        check(catalogSearchTotal(QJsonObject{{"data", QJsonObject{{"total", invalid}}}}) == -1,
              "Invalid or overflowing totals must remain unknown instead of breaking paging.");
}

void verifyUnavailableCatalogs(KugouApiClient *client)
{
    for (MusicPlatform platform : {MusicPlatform::QQMusic, MusicPlatform::NetEaseCloud}) {
        const PlaylistSnapshotProvider provider(platform, {}, client);
        bool completed = false;
        provider.searchCatalog("query", 2, [&](const CatalogSearchPage &page, const QString &error) {
            check(page.page == 2 && page.tracks.isEmpty() && !page.hasMore
                      && error.contains(platformName(platform)) && error.contains(QStringLiteral("尚未接入")),
                  "Unsupported platforms must explicitly report that their online catalog is not connected.");
            completed = true;
        });
        check(completed, "Unavailable providers must complete their callback.");
    }
    const PlaylistSnapshotProvider disconnected(MusicPlatform::Kugou, {});
    bool completed = false;
    disconnected.searchCatalog("query", 0, [&](const CatalogSearchPage &page, const QString &error) {
        check(page.page == 1 && page.tracks.isEmpty() && !error.isEmpty(),
              "A Kugou snapshot without a live API client must fail without a dangling dereference.");
        completed = true;
    });
    check(completed, "A disconnected catalog must finish its callback.");
    auto *temporaryClient = new KugouApiClient;
    const PlaylistSnapshotProvider guarded(MusicPlatform::Kugou, {}, temporaryClient);
    delete temporaryClient;
    guarded.searchCatalog("query", 1, [&](const CatalogSearchPage &page, const QString &error) {
        check(page.tracks.isEmpty() && !error.isEmpty(), "Destroying a catalog client must safely disconnect its snapshot.");
    });
}

QJsonArray catalogSongs(int count, int page)
{
    QJsonArray songs;
    for (int i = 0; i < count; ++i) {
        QJsonObject song = searchSong();
        song.insert("MixSongID", 100000 + (page - 1) * 30 + i);
        song.insert("SongName", QStringLiteral("曲库歌曲 %1").arg((page - 1) * 30 + i));
        songs.append(song);
    }
    return songs;
}

} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QCoreApplication app(argc, argv);
    verifySearchMetadata();
    QTcpServer server, wrongRoute;
    check(server.listen(QHostAddress::LocalHost, 0), "The local mock must listen.");
    check(wrongRoute.listen(QHostAddress::LocalHost, 0), "The routing sentinel must listen.");
    QObject::connect(&wrongRoute, &QTcpServer::newConnection, &app, [] {
        check(false, "Search or device lookup unexpectedly used the general metadata network manager.");
    });

    int deviceRequests = 0, searchRequests = 0, catalogRequests = 0;
    bool rejectDevice = false;
    QSet<QString> requestIds;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            auto responded = std::make_shared<bool>(false);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes, responded] {
                if (*responded)
                    return;
                *bytes += socket->readAll();
                const qsizetype boundary = bytes->indexOf("\r\n\r\n");
                if (boundary < 0)
                    return;
                const QByteArray header = bytes->left(boundary);
                int length = 0;
                for (const QByteArray &line : header.split('\n'))
                    if (line.trimmed().toLower().startsWith("content-length:"))
                        length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                if (bytes->size() < boundary + 4 + length)
                    return;
                *responded = true;
                const QUrl url = QUrl::fromEncoded(header.split(' ').at(1));
                const QUrlQuery parameters(url);
                const QString requestId = parameters.queryItemValue("request_id");
                check(!requestId.isEmpty() && !requestIds.contains(requestId),
                      "Authenticated search requests must use distinct cache identifiers.");
                requestIds.insert(requestId);
                check(header.contains("token=fixture;userid=1"),
                      "The mock authorization header must accompany every request.");
                QByteArray status("200 OK");
                QJsonObject response;
                QByteArray payload;
                if (url.path() == "/register/dev") {
                    ++deviceRequests;
                    response = rejectDevice
                        ? QJsonObject{{"status", 0}, {"error_code", 152}}
                        : QJsonObject{{"status", 1}, {"data", QJsonObject{{"dfid", "fixture-device"}}}};
                } else {
                    check(url.path() == "/search", "Name lookup must call the documented search route.");
                    check(header.startsWith("POST "), "Bridge search parameters must be JSON request data.");
                    check(header.contains(";dfid=fixture-device"),
                          "Name lookup must carry the registered device identity.");
                    const QJsonObject body = QJsonDocument::fromJson(bytes->mid(boundary + 4, length)).object();
                    const bool catalog = body.value("keywords").toString() == QStringLiteral("在线 曲库");
                    check((catalog || body.value("keywords").toString() == QStringLiteral("名称 补全"))
                              && !body.contains("keyword"),
                          "The module consumes plural keywords with surrounding whitespace removed.");
                    check(body.value("type").toString() == "song" && (catalog || body.value("page").toInt() == 1)
                              && body.value("pagesize").toInt() == 30,
                          "Name lookup must use the documented song-search defaults.");
                    check(!body.contains("show_author_alias"),
                          "Unsupported module parameters must not imply alias coverage.");
                    const int scenario = catalog ? catalogRequests++ : searchRequests++;
                    if (catalog) {
                        const int expectedPage = scenario == 0 || scenario == 3 ? 1
                            : scenario == 2 || scenario == 5 ? 3 : 2;
                        check(body.value("page").toInt() == expectedPage,
                              "Catalog pagination must forward the requested page instead of repeating page one.");
                        if (scenario == 7) {
                            response = {{"status", 0}, {"error_code", 152}, {"error_msg", "Rejected"},
                                        {"data", QJsonObject{{"info", catalogSongs(1, expectedPage)}}}};
                        } else if (scenario == 8) {
                            status = "503 Service Unavailable";
                            response = {{"status", 0}, {"error_code", 503}, {"message", "Busy"}};
                        } else if (scenario == 9) {
                            payload = "invalid-json";
                        } else {
                            const int count = scenario == 3 || scenario == 6 ? 0
                                : scenario == 2 || scenario == 5 ? 1 : 30;
                            QJsonObject data{{"info", catalogSongs(count, expectedPage)}};
                            if (scenario != 4 && scenario != 5)
                                data.insert("total", scenario == 3 ? QJsonValue(0) : QJsonValue("61"));
                            response = {{"status", 1}, {"data", data}};
                        }
                    } else if (scenario == 1) {
                        response = {{"status", 1}, {"data", QJsonObject{{"info", QJsonArray{}}}}};
                    } else if (scenario == 2) {
                        response = {{"status", 0}, {"error_code", 152}, {"error_msg", "Rejected"},
                                    {"data", QJsonObject{{"info", QJsonArray{searchSong()}}}}};
                    } else if (scenario == 3) {
                        status = "503 Service Unavailable";
                        response = {{"status", 0}, {"error_code", 503}, {"message", "Busy"}};
                    } else if (scenario == 4) {
                        payload = "invalid-json";
                    } else {
                        response = {{"status", 1}, {"data", QJsonObject{{"info", QJsonArray{searchSong()}}}}};
                    }
                }
                if (payload.isEmpty())
                    payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    KugouApiClient client;
    const QNetworkProxy mock(QNetworkProxy::HttpProxy, "127.0.0.1", server.serverPort());
    client.m_libraryNetwork.setProxy(mock);
    client.m_playbackNetwork.setProxy(mock);
    client.m_network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", wrongRoute.serverPort()));
    verifyUnavailableCatalogs(&client);
    bool emptyCatalogDone = false, unauthenticatedCatalogDone = false;
    client.searchCatalog(QStringLiteral(" \t "), 2,
                         [&](const CatalogSearchPage &page, const QString &error) {
        check(page.page == 2 && page.pageSize == 30 && page.total == -1 && page.tracks.isEmpty()
                  && !page.hasMore && error.isEmpty(), "An empty catalog query must return an empty requested page.");
        emptyCatalogDone = true;
    });
    client.searchCatalog("query", 4, [&](const CatalogSearchPage &page, const QString &error) {
        check(page.page == 4 && page.tracks.isEmpty() && !page.hasMore && !error.isEmpty(),
              "An unauthenticated catalog query must fail without starting a service or registering a device.");
        unauthenticatedCatalogDone = true;
    });
    bool emptyDone = false, unauthenticatedDone = false;
    client.searchTrackNames(QStringLiteral(" \t "), [&](const QVector<Track> &tracks, const QString &error) {
        check(tracks.isEmpty() && error.isEmpty(), "An empty lookup must finish without a network request.");
        emptyDone = true;
    });
    client.searchTrackNames(QStringLiteral("名称"), [&](const QVector<Track> &tracks, const QString &error) {
        check(tracks.isEmpty() && !error.isEmpty(), "An unauthenticated lookup must clearly fail.");
        unauthenticatedDone = true;
    });
    check(emptyDone && unauthenticatedDone && emptyCatalogDone && unauthenticatedCatalogDone
              && deviceRequests == 0 && searchRequests == 0 && catalogRequests == 0,
          "Empty and unauthorized input must not register devices or issue searches.");
    client.m_authorization = "token=fixture;userid=1";
    const PlaylistSnapshotProvider catalogProvider(MusicPlatform::Kugou, {}, &client);
    std::function<void(int)> runCatalogScenario;
    runCatalogScenario = [&](int scenario) {
        client.m_authorization = "token=fixture;userid=1";
        if (scenario == 11 || scenario == 12) {
            client.m_deviceId.clear();
            rejectDevice = scenario == 11;
        }
        const int requestedPage = scenario == 0 ? 0 : scenario == 3 ? 1
            : scenario == 2 || scenario == 5 ? 3 : 2;
        const int expectedPage = qMax(1, requestedPage);
        catalogProvider.searchCatalog(QStringLiteral("  在线 曲库  "), requestedPage,
                                      [&, scenario, expectedPage](const CatalogSearchPage &page, const QString &error) {
            check(page.page == expectedPage && page.pageSize == 30,
                  "Every catalog callback, including errors, must preserve its normalized pagination metadata.");
            if (scenario <= 6) {
                const int count = scenario == 3 || scenario == 6 ? 0
                    : scenario == 2 || scenario == 5 ? 1 : 30;
                const int total = scenario == 4 || scenario == 5 ? -1 : scenario == 3 ? 0 : 61;
                const bool more = scenario == 0 || scenario == 1 || scenario == 4;
                check(error.isEmpty() && page.tracks.size() == count && page.total == total && page.hasMore == more,
                      "Catalog pages must expose upstream totals, infer unknown totals, and stop on an empty or final page.");
                for (int i = 0; i < page.tracks.size(); ++i)
                    check(page.tracks[i].platform == MusicPlatform::Kugou
                              && page.tracks[i].track.albumAudioId == QString::number(100000 + (expectedPage - 1) * 30 + i),
                          "Catalog tracks must identify their real platform and retain unique playable recording IDs.");
                check(catalogProvider.likedTracks().isEmpty() && catalogProvider.playlists().isEmpty(),
                      "Online catalog results must not mutate the account's local playlist snapshot.");
            } else if (scenario == 7 || scenario == 8) {
                check(page.tracks.isEmpty() && !page.hasMore && error.contains(scenario == 7 ? "152" : "503"),
                      "Rejected catalog data must discard rows and preserve its upstream error code.");
            } else if (scenario == 9) {
                check(page.tracks.isEmpty() && !error.isEmpty(), "Malformed catalog data must fail explicitly.");
            } else if (scenario == 11) {
                check(page.tracks.isEmpty() && !error.isEmpty() && error.contains(QStringLiteral("设备验证"))
                          && deviceRequests == 4 && catalogRequests == 11,
                      "Failed device verification must exhaust its existing retries without issuing a catalog search.");
            } else {
                check(page.tracks.isEmpty() && !page.hasMore && error.contains(QStringLiteral("账号已切换")),
                      "Account changes during search or device validation must discard previous-account results.");
                if (scenario == 12) {
                    check(deviceRequests == 5 && catalogRequests == 11 && searchRequests == 6,
                          "An account change during registration must complete once without searching as the new account.");
                    app.quit();
                    return;
                }
            }
            runCatalogScenario(scenario + 1);
        });
        if (scenario == 10 || scenario == 12)
            client.m_authorization = "token=changed;userid=2";
    };
    std::function<void(int)> runScenario;
    runScenario = [&](int scenario) {
        client.searchTrackNames(QStringLiteral("  名称 补全  "), [&, scenario](const QVector<Track> &tracks, const QString &error) {
            if (scenario == 0) {
                check(error.isEmpty() && tracks.size() == 1 && tracks.first().albumAudioId == "987654",
                      "Name lookup must return parsed exact identifiers after device registration.");
            } else if (scenario == 1) {
                check(error.isEmpty() && tracks.isEmpty(), "A valid empty search must remain a successful empty result.");
            } else if (scenario == 2) {
                check(tracks.isEmpty() && error.contains("152"),
                      "Structured rejection must return its code and discard response song rows.");
            } else if (scenario == 3) {
                check(tracks.isEmpty() && error.contains("503"),
                      "HTTP errors with structured rejection data must preserve the error code.");
            } else if (scenario == 4) {
                check(tracks.isEmpty() && !error.isEmpty(), "Malformed data must not be treated as a successful empty result.");
            } else {
                check(tracks.isEmpty() && error.contains(QStringLiteral("账号已切换")),
                      "A late result must not survive an account change.");
                check(deviceRequests == 1 && searchRequests == 6,
                      "The registered identity must be reused and each lookup must complete once.");
                runCatalogScenario(0);
                return;
            }
            runScenario(scenario + 1);
        });
        if (scenario == 5)
            client.m_authorization = "token=changed;userid=2";
    };
    runScenario(0);
    QTimer::singleShot(10000, &app, [&] { app.exit(1); });
    return app.exec();
}
