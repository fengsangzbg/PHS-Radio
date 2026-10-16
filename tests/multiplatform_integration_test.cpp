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
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::fflush(stderr);
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

bool waitUntil(const std::function<bool()> &condition, int timeout = 5000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout)
        waitForEvents(10);
    return condition();
}

struct Request {
    QString query;
    int page = 0;
    CatalogSearchCallback callback;
};

struct Requests {
    QVector<Request> calls;
};

class DeferredProvider final : public MusicProvider {
public:
    DeferredProvider(MusicPlatform platform, QVector<Playlist> playlists,
                     std::shared_ptr<Requests> requests)
        : m_platform(platform), m_playlists(std::move(playlists)), m_requests(std::move(requests)) {}
    MusicPlatform platform() const override { return m_platform; }
    QVector<Playlist> playlists() const override { return m_playlists; }
    QVector<Track> likedTracks() const override { return allPlaylistTracks(m_playlists); }
    QVector<Track> search(const QString &query) const override
    {
        return MusicSearchIndex(allPlaylistTracks(m_playlists)).search(query);
    }
    void searchCatalog(const QString &query, int page, CatalogSearchCallback callback) const override
    {
        m_requests->calls.append({query, page, std::move(callback)});
    }
private:
    MusicPlatform m_platform;
    QVector<Playlist> m_playlists;
    std::shared_ptr<Requests> m_requests;
};

QUrl silentWave(const QString &path)
{
    constexpr quint32 sampleRate = 16000;
    constexpr quint32 size = sampleRate * 8 * sizeof(qint16);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "The local silent WAV fixture must be writable.");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + size);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << sampleRate
           << quint32(sampleRate * sizeof(qint16)) << quint16(sizeof(qint16)) << quint16(16);
    stream.writeRawData("data", 4);
    stream << size;
    const QByteArray silence(size, '\0');
    stream.writeRawData(silence.constData(), silence.size());
    check(stream.status() == QDataStream::Ok, "The silent WAV fixture must contain valid PCM data.");
    file.close();
    return QUrl::fromLocalFile(path);
}


class QualityAudioFixture {
public:
    QTcpServer server;
    QByteArray wave;
    bool fail = false;
    QString requestedLevel;
    explicit QualityAudioFixture(const QString &wavePath)
    {
        QFile file(wavePath);
        check(file.open(QIODevice::ReadOnly), "Quality fixture needs the local silent WAV.");
        wave = file.readAll();
        check(server.listen(QHostAddress::LocalHost), "Quality fixture must bind locally.");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto bytes = std::make_shared<QByteArray>();
                auto sent = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes, sent] {
                    bytes->append(socket->readAll());
                    const int end = bytes->indexOf("\r\n\r\n");
                    if (*sent || end < 0) return;
                    const QByteArray header = bytes->left(end);
                    const auto length = QRegularExpression("content-length:\\s*(\\d+)")
                        .match(QString::fromLatin1(header).toLower());
                    const int count = length.hasMatch() ? length.captured(1).toInt() : 0;
                    if (bytes->size() < end + 4 + count) return;
                    *sent = true;
                    const QUrl url(QString::fromLatin1(header.split('\n').first().split(' ').value(1)));
                    if (url.path() == "/audio.wav") {
                        const auto range = QRegularExpression("range: bytes=(\\d+)-(\\d*)")
                            .match(QString::fromLatin1(header).toLower());
                        const int start = range.hasMatch() ? range.captured(1).toInt() : 0;
                        const int last = range.hasMatch() && !range.captured(2).isEmpty()
                            ? qMin(range.captured(2).toInt(), int(wave.size()) - 1) : int(wave.size()) - 1;
                        const QByteArray payload = wave.mid(start, last - start + 1);
                        QByteArray response = range.hasMatch() ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                        response += "Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\n";
                        if (range.hasMatch()) response += "Content-Range: bytes " + QByteArray::number(start) + "-"
                            + QByteArray::number(last) + "/" + QByteArray::number(wave.size()) + "\r\n";
                        response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n";
                        socket->write(response + payload);
                    } else {
                        const QJsonObject request = QJsonDocument::fromJson(bytes->mid(end + 4, count)).object();
                        requestedLevel = request.value("level").toString();
                        const QJsonObject body = fail ? QJsonObject{{"code", 502}, {"reason", "upstream_unavailable"}}
                            : QJsonObject{{"code", 200}, {"data", QJsonArray{QJsonObject{{"id", "shared"},
                                {"url", QString("http://127.0.0.1:%1/audio.wav").arg(server.serverPort())}}}}};
                        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
                        socket->write((fail ? QByteArray("HTTP/1.1 502 Bad Gateway\r\n") : QByteArray("HTTP/1.1 200 OK\r\n"))
                            + "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(payload.size())
                            + "\r\nConnection: close\r\n\r\n" + payload);
                    }
                    socket->disconnectFromHost();
                });
            }
        });
    }
};
}
int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("PHSRadioTests"); app.setApplicationName("MultiplatformIntegrationTest");
    check(ReleaseConfig::availablePlatforms() == QVector<MusicPlatform>{MusicPlatform::Kugou, MusicPlatform::NetEaseCloud},
          "The new production build must expose exactly Kugou and NetEase.");
    PlayerWindow window({MusicPlatform::Kugou, MusicPlatform::NetEaseCloud}, true, true);
    window.resize(1120, 820);
    auto kg = std::make_shared<Requests>(), ne = std::make_shared<Requests>();
    const QUrl audio = silentWave(settings.filePath("silent.wav"));
    QImage cover(80, 80, QImage::Format_RGB32); cover.fill(QColor("#e44255"));
    check(cover.save(settings.filePath("cover.png")), "A local artwork fixture must be available.");
    Track k; k.id = "shared"; k.title = QStringLiteral("酷狗歌曲"); k.artist = "A";
    k.coverUrl = QUrl::fromLocalFile(settings.filePath("cover.png")); k.audioUrl = audio;
    Track n = k; n.title = QStringLiteral("网易云歌曲"); n.artist = "B";
    Playlist kl; kl.id = "11"; kl.name = QStringLiteral("酷狗歌单"); kl.tracks = {k};
    Playlist nl; nl.id = "netease:11"; nl.name = QStringLiteral("网易云歌单"); nl.tracks = {n};
    {
        const QSignalBlocker block(window.m_platform);
        window.m_providers.clear();
        window.m_providers.push_back(std::make_unique<DeferredProvider>(MusicPlatform::Kugou, QVector<Playlist>{kl}, kg));
        window.m_providers.push_back(std::make_unique<DeferredProvider>(MusicPlatform::NetEaseCloud, QVector<Playlist>{nl}, ne));
        window.m_platform->clear();
        window.m_platform->addItems({platformName(MusicPlatform::Kugou), platformName(MusicPlatform::NetEaseCloud)});
    }
    window.m_kugouAccountConnected = true;
    window.m_kugouPlaylists = {kl};
    window.m_kugouTrackCache.insert(kl.id, kl.tracks);
    window.m_completedPlaylists.insert(kl.id);
    window.m_neteaseApi.m_loggedIn = true;
    window.m_kugouGeneration = 7; window.m_neteaseGeneration = 9;
    window.m_audioOutput->setMuted(true); window.m_audioOutput->setVolume(0);
    window.m_drawerSignature.clear();
    window.show(); window.showLibraryPage(); window.refresh();
    const auto search = [&] {
        window.m_search->setText(QStringLiteral("同名歌曲"));
        window.m_searchTimer.stop(); window.startCatalogSearch(true);
    };
    search();
    check(kg->calls.size() == 1 && ne->calls.size() == 1, "One query must dispatch to both logged-in catalog sources.");
    CatalogSearchPage np; np.tracks = {{n, MusicPlatform::NetEaseCloud}}; np.total = 2; np.hasMore = true;
    CatalogSearchPage kp; kp.tracks = {{k, MusicPlatform::Kugou}}; kp.total = 1; kp.hasMore = false;
    ne->calls[0].callback(np, {});
    check(window.m_catalogTracks.size() == 1 && window.m_catalogBusy, "An earlier source reply must appear without waiting for the slower platform.");
    kg->calls[0].callback(kp, {});
    check(!window.m_catalogBusy && window.m_catalogTracks.size() == 2
        && window.m_catalogTracks[0].platform == MusicPlatform::Kugou
        && window.m_catalogTracks[1].platform == MusicPlatform::NetEaseCloud,
        "Mixed results must keep same-ID songs from different platforms and interleave source badges.");
    const auto *badge = window.m_catalogPanel->resultsTable()->item(1, 3);
    check(!badge->icon().isNull() && badge->data(Qt::UserRole).toInt() == int(MusicPlatform::NetEaseCloud),
          "NetEase search rows must show their actual nonempty bundled platform logo.");
    window.loadMoreCatalogResults();
    check(kg->calls.size() == 1 && ne->calls.size() == 2 && ne->calls[1].page == 2,
          "Pagination must request only unfinished platforms at their own next page.");
    ne->calls[1].callback({}, QStringLiteral("网易云暂时不可用"));
    check(window.m_catalogTracks.size() == 2 && !window.m_catalogBusy && window.m_catalogHasMore,
          "One platform's page failure must preserve all previously loaded songs and remain retryable.");
    window.loadMoreCatalogResults();
    check(ne->calls.size() == 3 && ne->calls[2].page == 2 && kg->calls.size() == 1,
          "Retry must repeat the failed platform page without redownloading an exhausted source.");
    Track second = n; second.id = "second"; second.title = QStringLiteral("网易云第二首");
    CatalogSearchPage next; next.page = 2; next.total = 2; next.tracks = {{second, MusicPlatform::NetEaseCloud}};
    ne->calls[2].callback(next, {});
    check(window.m_catalogTracks.size() == 3 && !window.m_catalogHasMore, "Retry success must append the missing source page once.");
    window.playCatalogTrack(1);
    check(waitUntil([&] { return window.m_player->playbackState() == QMediaPlayer::PlayingState; }, 7000),
          "A NetEase result must use its own playback queue and play an actual local silent media fixture.");
    const QUrl playing = window.m_player->source();
    const int requestCount = kg->calls.size() + ne->calls.size();
    window.m_drawerPlatformSwitch->click();
    check(window.provider()->platform() == MusicPlatform::NetEaseCloud
        && window.m_visiblePlaylists.size() == 1 && window.m_visiblePlaylists.first().id == nl.id,
        "The drawer button must switch from Kugou to the NetEase account playlist view.");
    window.m_drawerPlatformSwitch->click();
    check(window.provider()->platform() == MusicPlatform::Kugou && window.m_kugouAccountConnected
        && window.m_neteaseApi.isLoggedIn() && window.m_player->source() == playing
        && window.m_player->playbackState() == QMediaPlayer::PlayingState
        && window.m_queuePlatform == MusicPlatform::NetEaseCloud
        && requestCount == kg->calls.size() + ne->calls.size(),
        "Switching back must preserve both logins, independent playback and the completed aggregate search.");
    QualityAudioFixture qualityFixture(audio.toLocalFile());
    window.m_neteaseApi.m_cookie = "MUSIC_U=fixture";
    window.m_neteaseApi.m_network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", qualityFixture.server.serverPort()));
    Track resolving = n;
    resolving.audioUrl = QUrl();
    window.m_playbackQueue.setTracks({resolving}, 0);
    window.m_player->pause();
    window.m_player->setPosition(2100);
    qualityFixture.fail = true;
    window.changePlaybackQuality(AudioQuality::Lossless);
    check(waitUntil([&] { return !window.m_sourceRequestPending; })
        && qualityFixture.requestedLevel == "lossless" && window.m_player->source() == playing
        && window.m_player->playbackState() == QMediaPlayer::PausedState
        && window.statusBar()->currentMessage().contains(QStringLiteral("保留当前播放")),
        "A failed quality request must preserve source, position and paused state.");
    qualityFixture.fail = false;
    window.changePlaybackQuality(AudioQuality::High);
    const bool qualityRestored = waitUntil([&] { return !window.m_sourceRequestPending && window.m_qualityResumeRequest < 0
            && window.m_player->source().path() == "/audio.wav"
            && window.m_player->playbackState() == QMediaPlayer::PausedState
            && qAbs(window.m_player->position() - 2100) < 350; }, 7000);
    if (!qualityRestored) std::fprintf(stderr, "Quality resume: pos=%lld requested=%lld paused=%d state=%d seekable=%d request=%d current=%d\n",
        window.m_player->position(), window.m_qualityResumePosition, int(window.m_qualityResumePaused), int(window.m_player->playbackState()),
        int(window.m_player->isSeekable()), window.m_qualityResumeRequest, window.m_playRequest);
    check(qualityRestored
        && qualityFixture.requestedLevel == "exhigh" && qAbs(window.m_player->position() - 2100) < 350,
        "Successful quality changes must play real media while preserving position and pause.");
    check(window.m_neteaseApi.audioQuality() == AudioQuality::High
        && window.m_kugouApi.audioQuality() == AudioQuality::Standard
        && QSettings().value("audio/neteaseQuality").toInt() == int(AudioQuality::High)
        && window.m_dock->m_quality->toolTip().contains(QStringLiteral("网易云")),
        "Quality preferences must follow the playing platform rather than the browsed platform and persist independently.");
    const int oldKg = kg->calls.size(), oldNe = ne->calls.size();
    search();
    window.m_search->setText(QStringLiteral("新查询")); window.m_searchTimer.stop(); window.startCatalogSearch(true);
    kg->calls[oldKg].callback(kp, {}); ne->calls[oldNe].callback(np, {});
    check(window.m_catalogTracks.isEmpty(), "Responses from an earlier query must not leak into the new mixed results.");
    kg->calls[oldKg + 1].callback({}, QStringLiteral("酷狗暂时不可用"));
    np.hasMore = false; np.total = 1;
    ne->calls[oldNe + 1].callback(np, {});
    check(window.m_catalogTracks.size() == 1 && window.m_catalogTracks.first().platform == MusicPlatform::NetEaseCloud,
          "A failing platform must not hide the other platform's successful search.");
    window.m_drawer->setExpanded(true);
    window.m_drawer->setPinned(true);
    window.m_catalogPanel->dismiss();
    waitForEvents(250);
    check(window.grab().save(QCoreApplication::applicationDirPath() + "/multiplatform-drawer-preview.png"),
          "The new drawer must have a reviewable UI preview.");
    window.m_search->setText(QStringLiteral("预览")); window.m_searchTimer.stop(); window.startCatalogSearch(true);
    const int ki = kg->calls.size() - 1, ni = ne->calls.size() - 1;
    kg->calls[ki].callback(kp, {}); ne->calls[ni].callback(np, {});
    waitForEvents(250);
    check(window.grab().save(QCoreApplication::applicationDirPath() + "/multiplatform-search-preview.png"),
          "Mixed source badges and glass results must have a reviewable UI preview.");
    return 0;
}
