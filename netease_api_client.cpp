#include "netease_api_client.h"
#include "lyrics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrlQuery>
#include <QUuid>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

#ifndef PHSRADIO_NETEASE_SERVICE_DIR
#define PHSRADIO_NETEASE_SERVICE_DIR ""
#endif

namespace {
QString neString(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const auto value = object.value(key);
        if (value.isString() && !value.toString().isEmpty()) return value.toString();
        if (value.isDouble()) return QString::number(value.toVariant().toLongLong());
    }
    return {};
}

QVector<Track> parseNeteaseTracks(const QJsonArray &songs)
{
    QVector<Track> tracks;
    QSet<QString> known;
    for (const auto &value : songs) {
        const auto song = value.toObject();
        Track track;
        track.id = neString(song, {QStringLiteral("id")});
        if (track.id.isEmpty() || track.id == QStringLiteral("0") || known.contains(track.id)) continue;
        known.insert(track.id);
        track.title = neString(song, {QStringLiteral("name")});
        const QJsonObject album = song.value(QStringLiteral("al")).isObject()
            ? song.value(QStringLiteral("al")).toObject() : song.value(QStringLiteral("album")).toObject();
        track.album = neString(album, {QStringLiteral("name")});
        track.albumId = neString(album, {QStringLiteral("id")});
        // Preserve the original high-resolution art. The UI creates its own thumbnails.
        track.coverUrl = QUrl(neString(album, {QStringLiteral("picUrl")}));
        QStringList names;
        const auto artists = song.value(QStringLiteral("ar")).isArray()
            ? song.value(QStringLiteral("ar")).toArray() : song.value(QStringLiteral("artists")).toArray();
        for (const auto &artist : artists) {
            const QString name = neString(artist.toObject(), {QStringLiteral("name")});
            if (!name.isEmpty()) names.append(name);
        }
        track.artist = names.join(QStringLiteral(" / "));
        for (const auto &alias : song.value(QStringLiteral("alia")).toArray())
            if (alias.isString() && !alias.toString().trimmed().isEmpty()) track.searchAliases.append(alias.toString());
        for (const auto &key : {QStringLiteral("alias"), QStringLiteral("tns")})
            for (const auto &alias : song.value(key).toArray())
                if (alias.isString() && !alias.toString().trimmed().isEmpty()
                    && !track.searchAliases.contains(alias.toString())) track.searchAliases.append(alias.toString());
        tracks.append(track);
    }
    return tracks;
}

QString neteaseError(const QJsonObject &response, const QString &transport)
{
    const int code = response.value(QStringLiteral("code")).toInt();
    if (code == 301 || code == 302 || code == 401)
        return QStringLiteral("网易云登录已失效，请重新扫码登录（%1）。").arg(code);
    if (!transport.isEmpty()) {
        const QString reason = response.value(QStringLiteral("reason")).toString();
        if (reason == QStringLiteral("upstream_unavailable"))
            return QStringLiteral("网易云播放服务暂时不可用，请稍后重试（%1）。").arg(code);
        if (reason == QStringLiteral("playback_protocol_unavailable"))
            return QStringLiteral("网易云播放协议初始化失败，请退出旧版播放器后重启当前版本。");
        return code ? QStringLiteral("网易云请求失败（错误码 %1），请稍后重试。").arg(code)
                    : QStringLiteral("无法连接本机网易云服务，请重启播放器后重试。");
    }
    if (code == 200) return {};
    return QStringLiteral("网易云拒绝了请求（错误码 %1）。").arg(code);
}

QString neteaseSession(const QString &source, bool protect)
{
#ifdef Q_OS_WIN
    const QByteArray inputBytes = protect ? source.toUtf8() : QByteArray::fromBase64(source.toLatin1());
    if (inputBytes.isEmpty()) return {};
    DATA_BLOB input{static_cast<DWORD>(inputBytes.size()), reinterpret_cast<BYTE *>(const_cast<char *>(inputBytes.constData()))};
    const QByteArray entropyBytes("PHS Radio NetEase session v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()), reinterpret_cast<BYTE *>(const_cast<char *>(entropyBytes.constData()))};
    DATA_BLOB output{};
    const BOOL success = protect
        ? CryptProtectData(&input, L"PHS Radio NetEase session", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!success) return {};
    const QByteArray result(reinterpret_cast<const char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return protect ? QString::fromLatin1(result.toBase64()) : QString::fromUtf8(result);
#else
    Q_UNUSED(source) Q_UNUSED(protect) return {};
#endif
}
} // namespace

NeteaseApiClient::NeteaseApiClient(QObject *parent)
    : QObject(parent), m_network(this), m_serviceProcess(this), m_qrTimer(this)
{
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("services/netease"));
    m_serviceRoot = QFileInfo(bundled).isDir() ? bundled : QString::fromUtf8(PHSRADIO_NETEASE_SERVICE_DIR);
    m_qrTimer.setInterval(2000);
    connect(&m_qrTimer, &QTimer::timeout, this, [this] { pollQrStatus(); });
#ifdef Q_OS_WIN
    m_serviceProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
}

NeteaseApiClient::~NeteaseApiClient()
{
    m_qrTimer.stop();
    if (m_serviceStartedByApp && m_serviceProcess.state() != QProcess::NotRunning) {
        m_serviceProcess.terminate();
        if (!m_serviceProcess.waitForFinished(1500)) {
            m_serviceProcess.kill();
            m_serviceProcess.waitForFinished(500);
        }
    }
}

bool NeteaseApiClient::isLoggedIn() const { return m_loggedIn; }
void NeteaseApiClient::setLoggedIn(bool loggedIn)
{
    if (m_loggedIn == loggedIn) return;
    m_loggedIn = loggedIn;
    if (onLoginChanged) onLoginChanged(loggedIn);
}
void NeteaseApiClient::reportStatus(const QString &status) const { if (onStatusChanged) onStatusChanged(status); }
void NeteaseApiClient::reportError(const QString &error) const { if (onError) onError(error); }

void NeteaseApiClient::logout()
{
    ++m_generation;
    ++m_libraryRevision;
    m_playlists.clear(); m_loadedPlaylists.clear();
    m_qrTimer.stop();
    m_loginInProgress = false;
    m_pollPending = false;
    m_qrKey.clear(); m_cookie.clear(); m_userId.clear();
    QSettings().remove(QStringLiteral("accounts/netease/session"));
    setLoggedIn(false);
}

void NeteaseApiClient::startQrLogin()
{
    const int generation = ++m_generation;
    ++m_libraryRevision;
    m_playlists.clear(); m_loadedPlaylists.clear();
    m_qrTimer.stop();
    m_cookie.clear(); m_userId.clear(); m_qrKey.clear();
    m_loginInProgress = true; m_pollPending = false;
    setLoggedIn(false);
    reportStatus(QStringLiteral("正在连接本机网易云服务…"));
    ensureService([this, generation](const QString &error) {
        if (generation != m_generation) return;
        if (!error.isEmpty()) { m_loginInProgress = false; reportError(error); return; }
        requestQrKey(generation);
    });
}

void NeteaseApiClient::restoreSession()
{
    const QString cookie = neteaseSession(QSettings().value(QStringLiteral("accounts/netease/session")).toString(), false);
    if (cookie.isEmpty()) return;
    const int generation = ++m_generation;
    m_cookie = cookie;
    ensureService([this, generation](const QString &error) {
        if (generation != m_generation) return;
        if (!error.isEmpty()) { reportError(error); return; }
        validateSession(generation, false);
    });
}

void NeteaseApiClient::ensureService(std::function<void(const QString &)> callback)
{
    auto attempt = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weak = attempt;
    *attempt = [this, callback = std::move(callback), weak](int number) {
        const auto keepAlive = weak.lock();
        if (!keepAlive) return;
        requestJson(QStringLiteral("/health"), {}, {}, [this, callback, keepAlive, number](const QJsonObject &body, const QString &error) {
            if (error.isEmpty() && body.value(QStringLiteral("service")).toString() == QStringLiteral("phs-radio-netease")) {
                if (body.value(QStringLiteral("protocol")).toInt() < 3) {
                    callback(QStringLiteral("本机网易云服务是旧版本，请先退出旧播放器，再重新打开当前版本。")); return;
                }
                callback({}); return;
            }
            if (number == 0) startLocalService();
            if (number >= 20) { callback(QStringLiteral("无法启动网易云本机服务，请检查软件服务文件与 Node.js。")); return; }
            QTimer::singleShot(300, this, [keepAlive, number] { (*keepAlive)(number + 1); });
        });
    };
    (*attempt)(0);
}

void NeteaseApiClient::startLocalService()
{
    if (m_serviceProcess.state() != QProcess::NotRunning) return;
    const QString script = QDir(m_serviceRoot).absoluteFilePath(QStringLiteral("server.js"));
    if (m_serviceRoot.isEmpty() || !QFileInfo(script).isFile()
        || !QFileInfo(QDir(m_serviceRoot).filePath(QStringLiteral("node_modules/@neteasecloudmusicapienhanced/api"))).isDir()) return;
    const QDir app(QCoreApplication::applicationDirPath());
    QString node;
    for (const QString &candidate : {app.filePath(QStringLiteral("runtime/node.exe")), app.filePath(QStringLiteral("node.exe")),
                                     qEnvironmentVariable("PHSRADIO_NODE_EXECUTABLE")})
        if (node.isEmpty() && QFileInfo(candidate).isFile()) node = QFileInfo(candidate).absoluteFilePath();
    if (node.isEmpty()) node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty() && QFileInfo::exists(QStringLiteral("D:/DevTools/MSYS2/ucrt64/bin/node.exe")))
        node = QStringLiteral("D:/DevTools/MSYS2/ucrt64/bin/node.exe");
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString directory = QDir(cache).filePath(QStringLiteral("netease-service"));
    if (node.isEmpty() || cache.isEmpty() || !QDir().mkpath(directory)) return;
    QTemporaryFile writable(QDir(directory).filePath(QStringLiteral(".write-check-XXXXXX")));
    if (!writable.open()) return;
    writable.close(); writable.remove();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ENABLE_GENERAL_UNBLOCK"), QStringLiteral("false"));
    environment.insert(QStringLiteral("ENABLE_RANDOM_CN_IP"), QStringLiteral("false"));
    m_serviceProcess.setProcessEnvironment(environment);
    m_serviceProcess.setWorkingDirectory(directory);
    m_serviceProcess.setProgram(node);
    m_serviceProcess.setArguments({script});
    m_serviceProcess.start();
    m_serviceStartedByApp = true;
}

void NeteaseApiClient::requestJson(const QString &path, const QJsonObject &body,
                                 const QString &cookie, JsonCallback callback)
{
    QUrl url(QStringLiteral("http://127.0.0.1:3738") + path);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("request_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("Cache-Control", "no-store");
    if (!cookie.isEmpty()) request.setRawHeader("Authorization", cookie.toUtf8());
    QNetworkReply *reply;
    if (path == QStringLiteral("/health")) reply = m_network.get(request);
    else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        const QByteArray bytes = reply->readAll();
        QJsonParseError parse{};
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        QString error;
        if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else if (parse.error != QJsonParseError::NoError || !document.isObject()) error = QStringLiteral("服务返回了无法识别的数据。");
        const auto body = document.object();
        reply->deleteLater();
        callback(body, error);
    });
}

void NeteaseApiClient::requestQrKey(int generation)
{
    requestJson(QStringLiteral("/login/qr/key"), {}, {}, [this, generation](const QJsonObject &body, const QString &transport) {
        if (generation != m_generation) return;
        const QString error = neteaseError(body, transport);
        m_qrKey = neString(body.value(QStringLiteral("data")).toObject(), {QStringLiteral("unikey")});
        if (!error.isEmpty() || m_qrKey.isEmpty()) { reportError(error.isEmpty() ? QStringLiteral("网易云没有返回二维码 key。") : error); return; }
        requestQrImage(generation);
    });
}

void NeteaseApiClient::requestQrImage(int generation)
{
    requestJson(QStringLiteral("/login/qr/create"), {{QStringLiteral("key"), m_qrKey}, {QStringLiteral("qrimg"), true}}, {},
                [this, generation](const QJsonObject &body, const QString &transport) {
        if (generation != m_generation) return;
        const QString error = neteaseError(body, transport);
        const QString data = neString(body.value(QStringLiteral("data")).toObject(), {QStringLiteral("qrimg")});
        QImage image;
        image.loadFromData(QByteArray::fromBase64(data.section(QLatin1Char(','), -1).toLatin1()));
        if (!error.isEmpty() || image.isNull()) { reportError(error.isEmpty() ? QStringLiteral("网易云没有返回有效二维码。") : error); return; }
        if (onQrCodeReady) onQrCodeReady(image);
        reportStatus(QStringLiteral("请使用网易云音乐 App 扫码，并在手机上确认登录。"));
        m_qrTimer.start();
    });
}

void NeteaseApiClient::pollQrStatus()
{
    if (!m_loginInProgress || m_qrKey.isEmpty() || m_pollPending) return;
    m_pollPending = true;
    const int generation = m_generation;
    requestJson(QStringLiteral("/login/qr/check"), {{QStringLiteral("key"), m_qrKey}}, {},
                [this, generation](const QJsonObject &body, const QString &error) {
        if (generation != m_generation) return;
        m_pollPending = false;
        if (!error.isEmpty()) { reportStatus(QStringLiteral("网易云二维码状态暂时不可用，正在重试…")); return; }
        const int code = body.value(QStringLiteral("code")).toInt();
        if (code == 800) { m_qrTimer.stop(); m_loginInProgress = false; reportStatus(QStringLiteral("网易云二维码已过期，请重新生成。")); }
        else if (code == 801) reportStatus(QStringLiteral("等待扫描网易云二维码…"));
        else if (code == 802) reportStatus(QStringLiteral("已扫码，请在网易云音乐 App 确认登录。"));
        else if (code == 803) {
            m_qrTimer.stop(); m_loginInProgress = false;
            m_cookie = neString(body, {QStringLiteral("cookie")});
            if (m_cookie.isEmpty()) { reportError(QStringLiteral("网易云登录响应缺少账号凭据，请重新扫码。")); return; }
            validateSession(generation, true);
        } else reportStatus(QStringLiteral("网易云暂未确认登录，正在重试…"));
    });
}

void NeteaseApiClient::validateSession(int generation, bool persist)
{
    requestJson(QStringLiteral("/login/status"), {}, m_cookie,
                [this, generation, persist](const QJsonObject &body, const QString &transport) {
        if (generation != m_generation) return;
        const QJsonObject data = body.value(QStringLiteral("data")).toObject();
        m_userId = neString(data.value(QStringLiteral("profile")).toObject(), {QStringLiteral("userId")});
        const int code = data.value(QStringLiteral("code")).toInt(body.value(QStringLiteral("code")).toInt());
        if (!transport.isEmpty() || code != 200 || m_userId.isEmpty() || m_userId == QStringLiteral("0")
            || !m_cookie.contains(QStringLiteral("MUSIC_U="))) {
            m_cookie.clear(); m_userId.clear(); setLoggedIn(false);
            reportError(QStringLiteral("网易云登录未通过账号验证，请重新扫码登录。")); return;
        }
        if (persist) {
            const QString encrypted = neteaseSession(m_cookie, true);
            if (encrypted.isEmpty()) { m_cookie.clear(); setLoggedIn(false); reportError(QStringLiteral("无法使用 Windows DPAPI 安全保存网易云登录状态。")); return; }
            QSettings().setValue(QStringLiteral("accounts/netease/session"), encrypted);
        }
        setLoggedIn(true);
        reportStatus(QStringLiteral("网易云音乐已登录，正在读取歌单与每日推荐…"));
        loadPlaylists();
    });
}

void NeteaseApiClient::fetchDailyRecommendations(TracksCallback callback)
{
    if (!m_loggedIn || m_cookie.isEmpty()) { callback({}, QStringLiteral("请先扫码登录网易云音乐，再读取每日推荐。")); return; }
    const int generation = m_generation;
    requestJson(QStringLiteral("/recommend/songs"), {}, m_cookie,
                [this, generation, callback = std::move(callback)](const QJsonObject &body, const QString &transport) {
        if (generation != m_generation) { callback({}, QStringLiteral("网易云账号已切换，请重新读取推荐。")); return; }
        const QString error = neteaseError(body, transport);
        if (!error.isEmpty()) {
            const int code = body.value(QStringLiteral("code")).toInt();
            if (code == 301 || code == 302 || code == 401) {
                ++m_generation; m_cookie.clear(); m_userId.clear(); setLoggedIn(false);
            }
            callback({}, error); return;
        }
        const auto songs = body.value(QStringLiteral("data")).toObject().value(QStringLiteral("dailySongs"));
        if (!songs.isArray()) { callback({}, QStringLiteral("网易云未返回有效的每日推荐列表。")); return; }
        callback(parseNeteaseTracks(songs.toArray()), {});
    });
}

void NeteaseApiClient::fetchLyrics(const Track &track, LyricsCallback callback)
{
    if (track.id.isEmpty()) { callback({}, QStringLiteral("网易云歌曲缺少歌词查询 ID。")); return; }
    const int generation = m_generation;
    requestJson(QStringLiteral("/lyric"), {{QStringLiteral("id"), track.id}}, m_cookie,
                [this, generation, callback = std::move(callback)](const QJsonObject &body, const QString &transport) {
        if (generation != m_generation) { callback({}, QStringLiteral("网易云账号已切换，请重新加载歌词。")); return; }
        const QString error = neteaseError(body, transport);
        if (!error.isEmpty()) { callback({}, error); return; }
        const QString original = neString(body.value(QStringLiteral("lrc")).toObject(), {QStringLiteral("lyric")});
        const QString translated = neString(body.value(QStringLiteral("tlyric")).toObject(), {QStringLiteral("lyric")});
        callback(parseLyrics(original, translated), {});
    });
}

void NeteaseApiClient::fetchAudioUrl(const Track &track, AudioCallback callback)
{
    fetchAudioUrl(track, m_audioQuality, std::move(callback));
}

void NeteaseApiClient::fetchAudioUrl(const Track &track, AudioQuality quality, AudioCallback callback)
{
    if (!m_loggedIn || m_cookie.isEmpty()) { callback({}, QStringLiteral("请先登录网易云音乐再播放歌曲。")); return; }
    if (track.id.isEmpty()) { callback({}, QStringLiteral("网易云歌曲缺少播放 ID。")); return; }
    const int generation = m_generation;
    requestJson(QStringLiteral("/song/url/v1"), {{QStringLiteral("id"), track.id}, {QStringLiteral("level"), neteaseQualityLevel(quality)}}, m_cookie,
                [this, generation, requestedId = track.id, callback = std::move(callback)](const QJsonObject &body, const QString &transport) {
        if (generation != m_generation) { callback({}, QStringLiteral("网易云账号已切换，请重新播放。")); return; }
        const QString error = neteaseError(body, transport);
        if (!error.isEmpty()) {
            const int code = body.value(QStringLiteral("code")).toInt();
            if (code == 301 || code == 302 || code == 401) {
                ++m_generation; m_cookie.clear(); m_userId.clear(); setLoggedIn(false);
            }
            callback({}, error); return;
        }
        const auto rows = body.value(QStringLiteral("data")).toArray();
        QUrl url;
        for (const auto &row : rows) {
            const auto entry = row.toObject();
            if (neString(entry, {QStringLiteral("id")}) == requestedId) {
                url = QUrl(neString(entry, {QStringLiteral("url")}));
                break;
            }
        }
        if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
            callback({}, QStringLiteral("网易云未提供授权音源，可能需要会员、购买或当前地区没有版权。")); return;
        }
        callback(url, {});
    });
}

void NeteaseApiClient::loadPlaylists()
{
    if (!m_loggedIn || m_userId.isEmpty()) return;
    m_loadedPlaylists.clear();
    const int revision = ++m_libraryRevision;
    reportStatus(QStringLiteral("正在读取网易云账号全部歌单…"));
    requestPlaylistPage(m_generation, revision, 0, {});
}

void NeteaseApiClient::requestPlaylistPage(int generation, int revision, int offset,
                                          QVector<Playlist> playlists)
{
    requestJson(QStringLiteral("/user/playlist"),
        {{QStringLiteral("uid"), m_userId}, {QStringLiteral("limit"), 100}, {QStringLiteral("offset"), offset}},
        m_cookie, [this, generation, revision, offset, playlists = std::move(playlists)]
        (const QJsonObject &body, const QString &transport) mutable {
        if (generation != m_generation || revision != m_libraryRevision) return;
        QString error = neteaseError(body, transport);
        const auto rows = body.value(QStringLiteral("playlist"));
        if (error.isEmpty() && !rows.isArray())
            error = QStringLiteral("网易云没有返回有效的歌单列表。");
        if (!error.isEmpty()) {
            if (!playlists.isEmpty()) {
                m_playlists = std::move(playlists);
                if (onPlaylistsReady) onPlaylistsReady(m_playlists);
            }
            reportError(error); return;
        }
        QSet<QString> known;
        for (const Playlist &playlist : playlists) known.insert(playlist.id);
        const int before = playlists.size();
        for (const auto &value : rows.toArray()) {
            const auto row = value.toObject();
            const QString id = neString(row, {QStringLiteral("id")});
            if (id.isEmpty() || id == QStringLiteral("0")) continue;
            Playlist playlist;
            playlist.id = QStringLiteral("netease:") + id;
            if (known.contains(playlist.id)) continue;
            known.insert(playlist.id);
            playlist.externalId = id;
            playlist.originalId = id;
            playlist.name = neString(row, {QStringLiteral("name")});
            playlist.coverUrl = QUrl(neString(row, {QStringLiteral("coverImgUrl")}));
            QString creator = neString(row.value(QStringLiteral("creator")).toObject(), {QStringLiteral("userId")});
            if (creator.isEmpty()) creator = neString(row, {QStringLiteral("userId")});
            playlist.kind = creator == m_userId ? PlaylistKind::Created : PlaylistKind::Favorite;
            playlist.isLiked = row.value(QStringLiteral("specialType")).toInt() == 5;
            playlist.expectedTrackCount = row.value(QStringLiteral("trackCount")).toInt(-1);
            playlists.append(std::move(playlist));
        }
        const bool more = body.value(QStringLiteral("more")).toBool(rows.toArray().size() >= 100);
        if (more && playlists.size() > before && offset < 100000) {
            requestPlaylistPage(generation, revision, offset + rows.toArray().size(), std::move(playlists));
            return;
        }
        // Reuse account-local tracks on refresh; never reuse them across logins.
        QHash<QString, QVector<Track>> previous;
        for (const auto &playlist : m_playlists) previous.insert(playlist.id, playlist.tracks);
        for (auto &playlist : playlists) {
            playlist.tracks = previous.value(playlist.id);
            if (playlist.expectedTrackCount != playlist.tracks.size()) m_loadedPlaylists.remove(playlist.id);
        }
        m_playlists = std::move(playlists);
        if (onPlaylistsReady) onPlaylistsReady(m_playlists);
        if (more)
            reportError(QStringLiteral("网易云歌单分页未前进，已保留 %1 个歌单，请重试读取歌单。").arg(m_playlists.size()));
        else
            reportStatus(QStringLiteral("已读取网易云 %1 个歌单，正在加载全部歌曲。").arg(m_playlists.size()));
    });
}

struct NeteaseApiClient::TrackFetchState {
    int generation = 0;
    int revision = 0;
    QString playlistId;
    QVector<QString> ids;
    QHash<QString, Track> tracks;
    int offset = 0;
    TracksCallback callback;
};

void NeteaseApiClient::fetchPlaylistTracks(const Playlist &playlist, TracksCallback callback)
{
    if (!m_loggedIn || m_cookie.isEmpty()) { callback({}, QStringLiteral("请先登录网易云音乐读取歌单。")); return; }
    if (m_loadedPlaylists.contains(playlist.id)) {
        for (const auto &entry : m_playlists)
            if (entry.id == playlist.id) { callback(entry.tracks, {}); return; }
    }
    const QString id = playlist.externalId.isEmpty() ? playlist.originalId : playlist.externalId;
    if (id.isEmpty()) { callback({}, QStringLiteral("网易云歌单缺少 ID。")); return; }
    auto state = std::make_shared<TrackFetchState>();
    state->generation = m_generation;
    state->revision = m_libraryRevision;
    state->playlistId = playlist.id;
    state->callback = std::move(callback);
    requestJson(QStringLiteral("/playlist/detail"), {{QStringLiteral("id"), id}}, m_cookie,
        [this, state](const QJsonObject &body, const QString &transport) {
        if (state->generation != m_generation || state->revision != m_libraryRevision) {
            state->callback({}, QStringLiteral("网易云账号或歌单已更新，本次加载已取消。")); return;
        }
        QString error = neteaseError(body, transport);
        const auto detail = body.value(QStringLiteral("playlist")).toObject();
        const auto ids = detail.value(QStringLiteral("trackIds"));
        if (error.isEmpty() && !ids.isArray()) error = QStringLiteral("网易云未返回完整歌曲 ID 列表，请重试。");
        if (!error.isEmpty()) { state->callback({}, error); return; }
        QSet<QString> seen;
        for (const auto &value : ids.toArray()) {
            const QString id = neString(value.toObject(), {QStringLiteral("id")});
            if (!id.isEmpty() && id != QStringLiteral("0") && !seen.contains(id)) {
                seen.insert(id); state->ids.append(id);
            }
        }
        const int expected = detail.value(QStringLiteral("trackCount")).toInt(state->ids.size());
        if (expected > state->ids.size()) {
            // Preserve the returned IDs while explicitly reporting truncation.
            for (const auto &track : parseNeteaseTracks(detail.value(QStringLiteral("tracks")).toArray()))
                state->tracks.insert(track.id, track);
            finishTrackFetch(state, QStringLiteral("网易云只返回了 %1 / %2 首歌曲的 ID，请重试。")
                .arg(state->ids.size()).arg(expected));
            return;
        }
        requestTrackBatch(state);
    });
}

void NeteaseApiClient::requestTrackBatch(const std::shared_ptr<TrackFetchState> &state)
{
    if (state->generation != m_generation || state->revision != m_libraryRevision) {
        state->callback({}, QStringLiteral("网易云账号或歌单已更新，本次加载已取消。")); return;
    }
    if (state->offset >= state->ids.size()) { finishTrackFetch(state, {}); return; }
    QStringList batch;
    const int end = qMin(state->offset + 100, int(state->ids.size()));
    for (int index = state->offset; index < end; ++index) batch.append(state->ids.at(index));
    requestJson(QStringLiteral("/song/detail"), {{QStringLiteral("ids"), batch.join(QLatin1Char(','))}}, m_cookie,
        [this, state, end](const QJsonObject &body, const QString &transport) {
        if (state->generation != m_generation || state->revision != m_libraryRevision) {
            state->callback({}, QStringLiteral("网易云账号或歌单已更新，本次加载已取消。")); return;
        }
        QString error = neteaseError(body, transport);
        if (error.isEmpty() && !body.value(QStringLiteral("songs")).isArray())
            error = QStringLiteral("网易云没有返回有效的歌曲信息。");
        if (!error.isEmpty()) { finishTrackFetch(state, error); return; }
        for (const auto &track : parseNeteaseTracks(body.value(QStringLiteral("songs")).toArray()))
            state->tracks.insert(track.id, track);
        state->offset = end;
        requestTrackBatch(state);
    });
}

void NeteaseApiClient::finishTrackFetch(const std::shared_ptr<TrackFetchState> &state, const QString &error)
{
    QVector<Track> tracks;
    tracks.reserve(state->ids.size());
    int unavailable = 0;
    for (const auto &id : state->ids) {
        auto track = state->tracks.value(id);
        if (track.id.isEmpty()) {
            track.id = id;
            track.title = QStringLiteral("歌曲信息暂不可用（ID %1）").arg(id);
            ++unavailable;
        }
        tracks.append(std::move(track));
    }
    QString detail = error;
    if (detail.isEmpty() && unavailable)
        detail = QStringLiteral("有 %1 首歌曲暂未返回信息，已保留歌曲位置，可重试加载。").arg(unavailable);
    for (auto &playlist : m_playlists) {
        if (playlist.id != state->playlistId) continue;
        playlist.tracks = tracks;
        if (error.isEmpty()) playlist.expectedTrackCount = tracks.size();
        break;
    }
    if (detail.isEmpty()) m_loadedPlaylists.insert(state->playlistId);
    state->callback(tracks, detail);
}

void NeteaseApiClient::searchCatalog(const QString &query, int page, CatalogSearchCallback callback)
{
    CatalogSearchPage result;
    result.page = qMax(1, page);
    if (query.trimmed().isEmpty()) { result.hasMore = false; result.total = 0; callback(result, {}); return; }
    const int generation = m_generation;
    requestJson(QStringLiteral("/cloudsearch"), {{QStringLiteral("keywords"), query.trimmed()},
        {QStringLiteral("limit"), result.pageSize}, {QStringLiteral("offset"), (result.page - 1) * result.pageSize}},
        m_cookie, [this, generation, result, callback = std::move(callback)]
        (const QJsonObject &body, const QString &transport) mutable {
        if (generation != m_generation) { callback(result, QStringLiteral("网易云账号已切换，请重新搜索。")); return; }
        QString error = neteaseError(body, transport);
        const auto data = body.value(QStringLiteral("result")).toObject();
        if (error.isEmpty() && !body.value(QStringLiteral("result")).isObject())
            error = QStringLiteral("网易云没有返回有效的搜索结果。");
        if (!error.isEmpty()) { callback(result, error); return; }
        const auto songs = parseNeteaseTracks(data.value(QStringLiteral("songs")).toArray());
        for (const auto &track : songs) result.tracks.append({track, MusicPlatform::NetEaseCloud});
        result.total = data.value(QStringLiteral("songCount")).toInt(-1);
        result.hasMore = !songs.isEmpty() && (result.total >= 0
            ? result.page * result.pageSize < result.total : songs.size() >= result.pageSize);
        callback(result, {});
    });
}
