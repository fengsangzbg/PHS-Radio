#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#include <functional>
#include <memory>
#include <cstdlib>
#include <cstdio>
#define private public
#include "../kugou_api_client.cpp"
#include "../netease_api_client.cpp"
#undef private
#include "../playlist_snapshot_provider.cpp"
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

QJsonObject kugouRecommendation()
{
    return {{"hash", "fixture-recording-hash"}, {"songname", "Recommendation"},
        {"singername", "Artist"}, {"album_audio_id", 991}, {"album_id", 22},
        {"img", "https://imge.kugou.com/stdmusic/{size}/fixture.jpg"}};
}
QJsonObject neteaseRecommendation()
{
    return {{"id", 456}, {"name", "Original song"}, {"alia", QJsonArray{"Alias"}},
        {"ar", QJsonArray{QJsonObject{{"name", "Artist A"}}, QJsonObject{{"name", "Artist B"}}}},
        {"al", QJsonObject{{"id", 32}, {"name", "Album"},
             {"picUrl", "https://p1.music.126.net/original-cover.jpg"}}}};
}
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QCoreApplication app(argc, argv);
    app.setOrganizationName("PHSRadioTests"); app.setApplicationName("RecommendationApiTest");
    MockService service;
    int mode = 0;
    bool validProfile = false;
    service.respond = [&](const QString &path, const QJsonObject &body, const QByteArray &header) -> QJsonObject {
        if (path == "/everyday/recommend") {
            verify(body.value("platform") == "android" && header.contains("token=fixture"),
                   "Kugou recommendations must use the authorized account and existing mobile endpoint.");
            if (mode == 2) return {{"status", 0}, {"error_code", 152}};
            const QJsonArray songs = mode == 1 ? QJsonArray{} : QJsonArray{kugouRecommendation(), kugouRecommendation()};
            return {{"status", 1}, {"data", QJsonObject{{"songs", songs}}}};
        }
        if (path == "/search/lyric") {
            verify(body.value("hash") == "fixture-recording-hash" && body.value("album_audio_id") == "991"
                      && body.value("man") == "no", "Kugou lyric lookup must retain recording identifiers.");
            return {{"status", 200}, {"candidates", mode == 1 ? QJsonArray{} :
                QJsonArray{QJsonObject{{"id", "lyric-1"}, {"accesskey", "fixture-key"}}}}};
        }
        if (path == "/lyric" && body.contains("accesskey")) {
            verify(body.value("id") == "lyric-1" && body.value("accesskey") == "fixture-key"
                      && body.value("fmt") == "lrc" && body.value("decode").toBool(),
                   "Kugou lyric download must use the selected candidate and request decoded LRC.");
            const QString lrc = QStringLiteral("[00:01.250]第一句\n[00:02.50]Second");
            return mode == 3 ? QJsonObject{{"status", 200}, {"content", QString::fromLatin1(lrc.toUtf8().toBase64())}}
                             : QJsonObject{{"status", 200}, {"decodeContent", lrc}};
        }
        if (path == "/login/status")
            return {{"data", QJsonObject{{"code", 200}, {"profile", validProfile
                ? QJsonObject{{"userId", 42}} : QJsonObject{}}}}};
        if (path == "/recommend/songs") {
            verify(header.contains("MUSIC_U=fixture"), "NetEase recommendations must use the validated account session.");
            if (mode == 2) return {{"code", 301}};
            return {{"code", 200}, {"data", QJsonObject{{"dailySongs", mode == 1 ? QJsonArray{}
                : QJsonArray{neteaseRecommendation(), neteaseRecommendation()}}}}};
        }
        if (path == "/lyric") {
            verify(body.value("id") == "456", "NetEase lyrics must be routed by its own platform song ID.");
            return {{"code", 200}, {"lrc", QJsonObject{{"lyric", "[00:01.25]Original"}}},
                {"tlyric", QJsonObject{{"lyric", QStringLiteral("[00:01.250]译文")}}}};
        }
        if (path == "/song/url/v1") {
            verify(body.value("id") == "456" && !body.contains("unblock"),
                   "The client must request its own track and never ask for alternate sources.");
            return {{"code", 200}, {"data", QJsonArray{
                QJsonObject{{"id", 999}, {"url", "https://wrong.example/song.mp3"}},
                QJsonObject{{"id", 456}, {"url", mode == 4 ? QJsonValue(QJsonValue::Null)
                    : QJsonValue("https://authorized.example/song.mp3")}}}}};
        }
        verify(false, "Unexpected mocked API route."); return {};
    };

    KugouApiClient kugou;
    kugou.m_network.setProxy(service.proxy()); kugou.m_libraryNetwork.setProxy(service.proxy());
    kugou.m_playbackNetwork.setProxy(service.proxy());
    NeteaseApiClient netease;
    netease.m_network.setProxy(service.proxy());
    bool unavailable = false;
    kugou.fetchDailyRecommendations([&](const QVector<Track> &tracks, const QString &error) {
        unavailable = tracks.isEmpty() && !error.isEmpty();
    });
    verify(unavailable, "Unauthenticated Kugou recommendations must fail explicitly.");
    unavailable = false;
    netease.fetchDailyRecommendations([&](const QVector<Track> &tracks, const QString &error) {
        unavailable = tracks.isEmpty() && !error.isEmpty();
    });
    verify(unavailable && service.requests == 0, "Unauthenticated recommendations must not start a service or issue network requests.");

    kugou.m_authorization = "token=fixture;userid=1"; kugou.m_deviceId = "fixture-device";
    PlaylistSnapshotProvider kugouProvider(MusicPlatform::Kugou, {}, &kugou);
    Track kugouTrack;
    awaitCallback([&](auto done) { kugouProvider.fetchDailyRecommendations([&, done](const QVector<Track> &tracks, const QString &error) {
        verify(error.isEmpty() && tracks.size() == 1 && tracks[0].albumAudioId == "991",
               "Real Kugou recommendations must preserve playable IDs and coalesce duplicate recordings.");
        kugouTrack = tracks[0]; done();
    }); });
    verify(kugouTrack.coverUrl.toString().contains("/1000/"), "Kugou recommendation artwork must retain a high-resolution source.");
    QUrl cover;
    Track existing = kugouTrack; existing.coverUrl = QUrl("https://imge.kugou.com/stdmusic/100/fixture.jpg");
    kugou.fetchTrackCover(existing, [&](const QUrl &url) { cover = url; });
    verify(cover.path().contains("/1000/"), "Existing Kugou thumbnail URLs must be upgraded for the record center.");
    for (mode = 0; mode <= 3; ++mode) {
        if (mode == 2) continue;
        awaitCallback([&](auto done) { kugouProvider.fetchLyrics(kugouTrack, [&, done](const TrackLyrics &lyrics, const QString &error) {
            verify(error.isEmpty() && (mode == 1 ? lyrics.lines.isEmpty()
                : lyrics.lines.size() == 2 && lyrics.lines[0].timeMs == 1250 && lyrics.lines[1].timeMs == 2500),
                "Kugou must handle decoded/base64 LRC and honest no-lyric results."); done();
        }); });
    }
    mode = 1;
    awaitCallback([&](auto done) { kugouProvider.fetchDailyRecommendations([done](const QVector<Track> &tracks, const QString &error) {
        verify(tracks.isEmpty() && error.isEmpty(), "An empty daily recommendation result must remain successful and empty."); done();
    }); });
    mode = 2;
    awaitCallback([&](auto done) { kugouProvider.fetchDailyRecommendations([done](const QVector<Track> &tracks, const QString &error) {
        verify(tracks.isEmpty() && error.contains("152"), "Rejected recommendations must not display stale or invented songs."); done();
    }); });
    mode = 0;
    awaitCallback([&](auto done) {
        kugou.fetchDailyRecommendations([done](const QVector<Track> &tracks, const QString &error) {
            verify(tracks.isEmpty() && error.contains(QStringLiteral("账号已切换")), "Old-account recommendations must be discarded."); done();
        }); kugou.m_authorization = "token=other;userid=2";
    });

    netease.m_cookie = "MUSIC_U=fixture";
    awaitCallback([&](auto done) {
        netease.onError = [done](const QString &) { done(); };
        netease.validateSession(netease.m_generation, false);
    });
    netease.onError = {};
    verify(!netease.isLoggedIn() && netease.m_cookie.isEmpty(), "A cookie and code 200 without a real user profile must not be treated as login.");
    validProfile = true; netease.m_cookie = "MUSIC_U=fixture";
    awaitCallback([&](auto done) {
        netease.onLoginChanged = [done](bool loggedIn) { verify(loggedIn, "Validated NetEase profile should complete login."); done(); };
        netease.validateSession(netease.m_generation, false);
    });
    netease.onLoginChanged = {};
    verify(netease.isLoggedIn() && netease.m_userId == "42", "NetEase login must preserve its validated account identity.");
    NeteaseMusicProvider neteaseProvider(&netease);
    verify(neteaseProvider.platform() == MusicPlatform::NetEaseCloud && neteaseProvider.playlists().isEmpty()
              && neteaseProvider.likedTracks().isEmpty(), "A connected NetEase provider must not invent account playlists.");
    Track neteaseTrack;
    awaitCallback([&](auto done) { neteaseProvider.fetchDailyRecommendations([&, done](const QVector<Track> &tracks, const QString &error) {
        verify(error.isEmpty() && tracks.size() == 1 && tracks[0].id == "456" && tracks[0].artist == "Artist A / Artist B"
                  && tracks[0].searchAliases.contains("Alias") && tracks[0].coverUrl.toString() == "https://p1.music.126.net/original-cover.jpg",
               "NetEase recommendations must retain original metadata, aliases and full-resolution artwork.");
        neteaseTrack = tracks[0]; done();
    }); });
    awaitCallback([&](auto done) { neteaseProvider.fetchLyrics(neteaseTrack, [done](const TrackLyrics &lyrics, const QString &error) {
        verify(error.isEmpty() && lyrics.lines.size() == 1 && lyrics.lines[0].timeMs == 1250
                  && lyrics.lines[0].translation == QStringLiteral("译文"), "NetEase original and translated lyrics must share timing."); done();
    }); });
    for (int scenario : {0, 4}) {
        mode = scenario;
        awaitCallback([&](auto done) { netease.fetchAudioUrl(neteaseTrack, [&, done](const QUrl &url, const QString &error) {
            verify(mode == 4 ? url.isEmpty() && !error.isEmpty()
                : error.isEmpty() && url.host() == "authorized.example", "Audio must match its requested ID and fail when the platform provides no authorized URL."); done();
        }); });
    }
    mode = 0;
    awaitCallback([&](auto done) {
        neteaseProvider.fetchDailyRecommendations([done](const QVector<Track> &tracks, const QString &error) {
            verify(tracks.isEmpty() && error.contains(QStringLiteral("账号已切换")), "Late NetEase recommendations must respect account generation guards."); done();
        }); ++netease.m_generation;
    });
    mode = 2;
    awaitCallback([&](auto done) { neteaseProvider.fetchDailyRecommendations([done](const QVector<Track> &tracks, const QString &error) {
        verify(tracks.isEmpty() && error.contains("301"), "Expired NetEase sessions must produce a clear account error."); done();
    }); });
    verify(!netease.isLoggedIn() && netease.m_cookie.isEmpty(), "Expired platform credentials must reset the visible login state.");
    NeteaseMusicProvider missing(nullptr);
    missing.fetchDailyRecommendations([&](const QVector<Track> &tracks, const QString &error) {
        verify(tracks.isEmpty() && !error.isEmpty(), "Unavailable NetEase clients must fail explicitly.");
    });
    qInfo("Recommendation, account, artwork, audio and lyric API checks passed.");
    return 0;
}
