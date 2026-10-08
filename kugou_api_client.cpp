#include "kugou_api_client.h"
#include "lyrics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QCryptographicHash>

#include <memory>
#include <algorithm>
#include <limits>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

#ifndef PHSRADIO_KUGOU_SERVICE_DIR
#define PHSRADIO_KUGOU_SERVICE_DIR ""
#endif

namespace {

QString apiBaseUrl()
{
    return QStringLiteral("http://127.0.0.1:3737");
}

// Only the bridge's platform identity is shared and persisted. Account cookies
// and dfid stay out of the jar: credentials use DPAPI and explicit Authorization,
// and a registration is valid only for the current account session.
class KugouDeviceCookieJar final : public QNetworkCookieJar {
public:
    explicit KugouDeviceCookieJar(QObject *parent) : QNetworkCookieJar(parent)
    {
        QSettings settings;
        const QVariantMap stored = settings.value(settingsKey()).toMap();
        const QUrl origin(apiBaseUrl());
        QList<QNetworkCookie> cookies;
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            const QByteArray name = it.key().toLatin1();
            const QByteArray value = it.value().toString().toLatin1();
            if (allowedName(name) && validValue(value))
                cookies.push_back(QNetworkCookie(name, value));
        }
        // A partial GUID/MID pair must not be combined with a newly generated
        // server identity. The first bridge response supplies the full profile.
        if (completeProfile(cookies))
            QNetworkCookieJar::setCookiesFromUrl(cookies, origin);
    }

    bool setCookiesFromUrl(const QList<QNetworkCookie> &cookies, const QUrl &url) override
    {
        const QUrl origin(apiBaseUrl());
        if (url.scheme() != origin.scheme() || url.host() != origin.host()
            || url.port() != origin.port())
            return false;
        QList<QNetworkCookie> accepted;
        const QList<QNetworkCookie> existing = cookiesForUrl(origin);
        for (const QNetworkCookie &cookie : cookies) {
            if (!allowedName(cookie.name()) || !validValue(cookie.value()))
                continue;
            // Concurrent responses must not rotate an established identity.
            if (std::any_of(existing.cbegin(), existing.cend(), [&](const auto &saved) {
                    return saved.name() == cookie.name();
                }))
                continue;
            QNetworkCookie normalized(cookie.name(), cookie.value());
            normalized.setPath(QStringLiteral("/"));
            accepted.push_back(normalized);
        }
        // Adopt the core identity as one profile from one response. Otherwise
        // a partial reply before a bridge restart could freeze an old GUID and
        // combine it with a new server's MID/DEV on the next reply.
        if (!completeProfile(existing) && !completeProfile(accepted))
            return false;
        const bool changed = !accepted.isEmpty()
            && QNetworkCookieJar::setCookiesFromUrl(accepted, origin);
        if (changed) {
            const QList<QNetworkCookie> profile = cookiesForUrl(origin);
            if (completeProfile(profile)) {
                QVariantMap stored;
                for (const QNetworkCookie &cookie : profile)
                    stored.insert(QString::fromLatin1(cookie.name()), QString::fromLatin1(cookie.value()));
                QSettings settings;
                settings.setValue(settingsKey(), stored);
            }
        }
        return changed;
    }

private:
    static QString settingsKey() { return QStringLiteral("accounts/kugou/deviceCookies"); }
    static bool allowedName(const QByteArray &name)
    {
        return name == "KUGOU_API_GUID" || name == "KUGOU_API_MID" || name == "KUGOU_API_DEV"
            || name == "KUGOU_API_WEBGL" || name == "KUGOU_API_MAC" || name == "KUGOU_API_PLATFORM";
    }
    static bool validValue(const QByteArray &value)
    {
        return !value.isEmpty() && value.size() <= 256
            && std::all_of(value.cbegin(), value.cend(), [](unsigned char ch) {
                return ch > 32 && ch < 127 && ch != ';' && ch != ',';
            });
    }
    static bool completeProfile(const QList<QNetworkCookie> &cookies)
    {
        for (const QByteArray &name : {QByteArray("KUGOU_API_GUID"), QByteArray("KUGOU_API_MID"),
                                      QByteArray("KUGOU_API_DEV"), QByteArray("KUGOU_API_WEBGL")}) {
            if (std::none_of(cookies.cbegin(), cookies.cend(), [&](const auto &cookie) {
                    return cookie.name() == name;
                }))
                return false;
        }
        return true;
    }
};

QString resolveKugouServiceRoot(const QString &applicationRoot, const QString &developmentRoot)
{
    const QString bundledRoot = QDir(applicationRoot).filePath(QStringLiteral("services/kugou"));
    if (QFileInfo(bundledRoot).isDir())
        return QFileInfo(bundledRoot).absoluteFilePath();
    if (!developmentRoot.isEmpty())
        return QFileInfo(developmentRoot).absoluteFilePath();
    return QFileInfo(bundledRoot).absoluteFilePath();
}

QString resolveNodeExecutable(const QString &applicationRoot, const QString &configuredExecutable,
                              const QStringList &searchPaths = {})
{
#ifdef Q_OS_WIN
    const QString executableName = QStringLiteral("node.exe");
#else
    const QString executableName = QStringLiteral("node");
#endif
    // Release folders supply their own runtime and do not depend on the user's PATH.
    for (const QString &relativePath : {QStringLiteral("runtime/") + executableName, executableName}) {
        const QFileInfo candidate(QDir(applicationRoot).filePath(relativePath));
        if (candidate.isFile())
            return candidate.absoluteFilePath();
    }
    if (!configuredExecutable.isEmpty()) {
        const QFileInfo configured(configuredExecutable);
        if (configured.isFile())
            return configured.absoluteFilePath();
        const QString located = QStandardPaths::findExecutable(configuredExecutable, searchPaths);
        if (!located.isEmpty())
            return located;
    }
    const QString located = QStandardPaths::findExecutable(QStringLiteral("node"), searchPaths);
    if (!located.isEmpty())
        return located;
    const QString developmentNode = QStringLiteral("D:/DevTools/MSYS2/ucrt64/bin/node.exe");
    return QFileInfo(developmentNode).isFile() ? developmentNode : QString();
}

QString objectString(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().isEmpty())
            return value.toString();
        if (value.isDouble())
            return QString::number(value.toVariant().toLongLong());
    }
    return {};
}

QString apiFailureDetails(const QJsonObject &response, const QStringList &secrets)
{
    QString code = objectString(response, {QStringLiteral("error_code"), QStringLiteral("errcode"),
                                          QStringLiteral("code")});
    if (!code.isEmpty() && !QRegularExpression(QStringLiteral("^-?[0-9]{1,12}$")).match(code).hasMatch())
        code = QStringLiteral("未知");
    const QStringList messageKeys{QStringLiteral("errmsg"), QStringLiteral("error_msg"),
                                  QStringLiteral("msg"), QStringLiteral("message")};
    QString message = objectString(response.value(QStringLiteral("data")).toObject(), messageKeys);
    if (message.isEmpty())
        message = objectString(response, messageKeys);
    // Never dump response objects, request headers or credentials into the UI.
    for (const QString &secret : secrets) {
        if (secret.size() >= 4)
            message.replace(secret, QStringLiteral("[已隐藏]"));
    }
    message.replace(QRegularExpression(QStringLiteral(
        "(?:token|userid|user_id|dfid|authorization|cookie|KUGOU_API_\\w+)\\s*[:=]\\s*[^\\s;,]+"),
        QRegularExpression::CaseInsensitiveOption), QStringLiteral("[已隐藏]"));
    message.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")), QStringLiteral(" "));
    message = message.simplified().left(200);
    QString details;
    if (!code.isEmpty())
        details = QStringLiteral("（错误码 %1）").arg(code);
    if (!message.isEmpty())
        details += QStringLiteral("：%1").arg(message);
    return details;
}

bool hasApiError(const QJsonObject &response)
{
    const QString code = objectString(response, {QStringLiteral("error_code"), QStringLiteral("errcode"),
                                                 QStringLiteral("code")});
    return !code.isEmpty() && code != QStringLiteral("0");
}

QUrl objectCoverUrl(const QJsonObject &object)
{
    QString cover = objectString(object, {QStringLiteral("cover_url"), QStringLiteral("cover"),
                                           QStringLiteral("imgurl"), QStringLiteral("img_url"),
                                           QStringLiteral("pic_url"), QStringLiteral("pic"), QStringLiteral("img"),
                                           QStringLiteral("image_url"), QStringLiteral("image"),
                                           QStringLiteral("album_img"), QStringLiteral("album_cover"),
                                           QStringLiteral("sizable_cover")});
    if (cover.isEmpty()) {
        const QJsonObject album = object.value(QStringLiteral("album")).toObject();
        cover = objectString(album, {QStringLiteral("cover"), QStringLiteral("imgurl"),
                                     QStringLiteral("img_url"), QStringLiteral("pic"),
                                     QStringLiteral("sizable_cover")});
    }
    cover.replace(QStringLiteral("{size}"), QStringLiteral("1000"), Qt::CaseInsensitive);
    if (cover.startsWith(QStringLiteral("//")))
        cover.prepend(QStringLiteral("https:"));
    const QUrl url(cover);
    return url.isValid() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
        ? url : QUrl();
}

QUrl highResolutionKugouCover(QUrl url)
{
    if (url.host() == QStringLiteral("kugou.com") || url.host().endsWith(QStringLiteral(".kugou.com"))) {
        QString path = url.path();
        path.replace(QStringLiteral("{size}"), QStringLiteral("1000"), Qt::CaseInsensitive);
        path.replace(QRegularExpression(QStringLiteral("/(?:100|120|150|200|240|400|500)/")), QStringLiteral("/1000/"));
        url.setPath(path);
    }
    return url;
}

QJsonArray findArray(const QJsonValue &value, const QStringList &preferredKeys,
                     const QStringList &itemKeys)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty())
            return array;
        if (array.first().isObject()) {
            const QJsonObject first = array.first().toObject();
            for (const QString &key : itemKeys) {
                if (first.contains(key))
                    return array;
            }
        }
        for (const QJsonValue &child : array) {
            const QJsonArray nested = findArray(child, preferredKeys, itemKeys);
            if (!nested.isEmpty())
                return nested;
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString &key : preferredKeys) {
            if (object.value(key).isArray())
                return object.value(key).toArray();
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QJsonArray nested = findArray(it.value(), preferredKeys, itemKeys);
            if (!nested.isEmpty())
                return nested;
        }
    }
    return {};
}

QString protectedSession(const QString &token, const QString &userId)
{
#ifdef Q_OS_WIN
    const QByteArray plain = (token + QLatin1Char('\n') + userId).toUtf8();
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    const QByteArray entropyBytes("PHS Radio Kugou session v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(entropyBytes.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"PHS Radio Kugou session", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    const QByteArray encrypted(reinterpret_cast<const char *>(output.pbData),
                               static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return QString::fromLatin1(encrypted.toBase64());
#else
    Q_UNUSED(token)
    Q_UNUSED(userId)
    return {};
#endif
}

bool unprotectSession(const QString &stored, QString *token, QString *userId)
{
#ifdef Q_OS_WIN
    const QByteArray encrypted = QByteArray::fromBase64(stored.toLatin1());
    if (encrypted.isEmpty())
        return false;
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()))};
    const QByteArray entropyBytes("PHS Radio Kugou session v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(entropyBytes.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;
    const QByteArray plain(reinterpret_cast<const char *>(output.pbData),
                           static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    const int separator = plain.indexOf('\n');
    if (separator <= 0)
        return false;
    *token = QString::fromUtf8(plain.left(separator));
    *userId = QString::fromUtf8(plain.mid(separator + 1));
    return !token->isEmpty() && !userId->isEmpty();
#else
    Q_UNUSED(stored)
    Q_UNUSED(token)
    Q_UNUSED(userId)
    return false;
#endif
}

QJsonArray trackRows(const QJsonObject &response)
{
    return findArray(response,
                                      {QStringLiteral("info"), QStringLiteral("list"),
                                       QStringLiteral("songs"), QStringLiteral("files"), QStringLiteral("data")},
                                      {QStringLiteral("hash"), QStringLiteral("FileHash"),
                                       QStringLiteral("Hash"), QStringLiteral("MixSongID"),
                                       QStringLiteral("album_audio_id"),
                                       QStringLiteral("filename"), QStringLiteral("songname"),
                                       QStringLiteral("SongName"), QStringLiteral("FileName"),
                                       QStringLiteral("file"), QStringLiteral("audio_name")});
}

int catalogSearchTotal(const QJsonObject &response)
{
    const QJsonObject data = response.value(QStringLiteral("data")).toObject();
    for (const QJsonValue &value : {data.value(QStringLiteral("total")), response.value(QStringLiteral("total"))}) {
        bool valid = false;
        qint64 total = -1;
        if (value.isString()) {
            total = value.toString().toLongLong(&valid);
        } else if (value.isDouble()) {
            const double number = value.toDouble();
            valid = std::isfinite(number) && number >= 0 && number <= std::numeric_limits<int>::max()
                && std::floor(number) == number;
            if (valid)
                total = static_cast<qint64>(number);
        }
        if (valid && total >= 0 && total <= std::numeric_limits<int>::max())
            return static_cast<int>(total);
    }
    return -1;
}

QVector<Track> parseTracks(const QJsonObject &response)
{
    const QJsonArray rows = trackRows(response);
    QVector<Track> tracks;
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        QJsonObject item = value.toObject();
        const QJsonObject file = item.value(QStringLiteral("file")).toObject();
        for (auto it = file.constBegin(); it != file.constEnd(); ++it)
            if (!item.contains(it.key()))
                item.insert(it.key(), it.value());
        Track track;
        track.hash = objectString(item, {QStringLiteral("hash"), QStringLiteral("FileHash"),
                                         QStringLiteral("filehash"), QStringLiteral("Hash")});
        track.albumId = objectString(item, {QStringLiteral("album_id"), QStringLiteral("albumid"),
                                           QStringLiteral("AlbumID")});
        track.albumAudioId = objectString(item, {QStringLiteral("album_audio_id"), QStringLiteral("mixsongid"),
                                                QStringLiteral("MixSongID")});
        track.id = objectString(item, {QStringLiteral("hash"), QStringLiteral("FileHash"),
                                       QStringLiteral("filehash"), QStringLiteral("Hash"),
                                       QStringLiteral("album_audio_id"), QStringLiteral("mixsongid"),
                                       QStringLiteral("MixSongID")});
        track.title = objectString(item, {QStringLiteral("songname"), QStringLiteral("song_name"), QStringLiteral("audio_name"),
                                          QStringLiteral("SongName"), QStringLiteral("filename"),
                                          QStringLiteral("FileName"), QStringLiteral("name")});
        track.artist = objectString(item, {QStringLiteral("author_name"), QStringLiteral("singername"),
                                           QStringLiteral("artist"), QStringLiteral("SingerName")});
        track.album = objectString(item, {QStringLiteral("album_name"), QStringLiteral("album"),
                                          QStringLiteral("AlbumName")});
        if (track.album.isEmpty())
            track.album = objectString(item.value(QStringLiteral("albuminfo")).toObject(),
                                        {QStringLiteral("name"), QStringLiteral("album_name")});
        if (track.artist.isEmpty()) {
            QStringList artists;
            for (const auto &singer : item.value(QStringLiteral("singerinfo")).toArray()) {
                const QString name = objectString(singer.toObject(), {QStringLiteral("name"), QStringLiteral("singername")});
                if (!name.isEmpty())
                    artists.push_back(name);
            }
            track.artist = artists.join(QStringLiteral(" / "));
        }
        // Keep explicit alternate metadata for searching without changing the displayed names.
        const auto appendAlias = [&track](const QString &text) {
            const QString alias = text.trimmed();
            if (alias.isEmpty() || alias.compare(track.title, Qt::CaseInsensitive) == 0
                || alias.compare(track.artist, Qt::CaseInsensitive) == 0
                || alias.compare(track.album, Qt::CaseInsensitive) == 0
                || track.searchAliases.contains(alias, Qt::CaseInsensitive))
                return;
            track.searchAliases.push_back(alias);
        };
        const auto appendAliasValue = [&appendAlias](const auto &self, const QJsonValue &alias) -> void {
            if (alias.isString()) {
                appendAlias(alias.toString());
            } else if (alias.isArray()) {
                for (const QJsonValue &entry : alias.toArray())
                    self(self, entry);
            } else if (alias.isObject()) {
                const QJsonObject object = alias.toObject();
                for (const QString &key : {QStringLiteral("name"), QStringLiteral("alias"),
                                          QStringLiteral("value"), QStringLiteral("text")})
                    self(self, object.value(key));
            }
        };
        const auto collectAliases = [&appendAliasValue](const QJsonObject &object) {
            // Providers use several shapes; only explicit name fields become search terms.
            const QStringList keys{
                QStringLiteral("songname"), QStringLiteral("song_name"), QStringLiteral("audio_name"),
                QStringLiteral("ori_audio_name"), QStringLiteral("filename"), QStringLiteral("name"),
                QStringLiteral("author_name"), QStringLiteral("singername"), QStringLiteral("artist"),
                QStringLiteral("album_name"), QStringLiteral("album"),
                QStringLiteral("SongName"), QStringLiteral("FileName"),
                QStringLiteral("SingerName"), QStringLiteral("AlbumName"),
                QStringLiteral("alias"), QStringLiteral("aliases"),
                QStringLiteral("alias_name"), QStringLiteral("aliasname"),
                QStringLiteral("song_alias"), QStringLiteral("song_aliases"),
                QStringLiteral("songname_alias"), QStringLiteral("song_name_alias"),
                QStringLiteral("audio_alias"), QStringLiteral("audio_name_alias"),
                QStringLiteral("author_alias"), QStringLiteral("author_name_alias"),
                QStringLiteral("singer_alias"), QStringLiteral("singer_aliases"),
                QStringLiteral("singername_alias"), QStringLiteral("artist_alias"),
                QStringLiteral("album_alias"), QStringLiteral("album_aliases"),
                QStringLiteral("album_name_alias"), QStringLiteral("translated_name"),
                QStringLiteral("trans_name"), QStringLiteral("translation"), QStringLiteral("translations")};
            for (const QString &key : keys)
                appendAliasValue(appendAliasValue, object.value(key));
        };
        collectAliases(item);
        collectAliases(file);
        for (const QJsonValue &singer : item.value(QStringLiteral("singerinfo")).toArray())
            collectAliases(singer.toObject());
        collectAliases(item.value(QStringLiteral("albuminfo")).toObject());
        collectAliases(item.value(QStringLiteral("album_info")).toObject());
        collectAliases(item.value(QStringLiteral("album")).toObject());
        track.coverUrl = objectCoverUrl(item);
        if (track.title.isEmpty())
            track.title = QStringLiteral("未命名曲目");
        tracks.push_back(track);
    }
    return tracks;
}

}

KugouApiClient::KugouApiClient(QObject *parent)
    : QObject(parent), m_network(this), m_playbackNetwork(this), m_libraryNetwork(this), m_serviceProcess(this), m_qrTimer(this),
      m_serviceRoot(resolveKugouServiceRoot(QCoreApplication::applicationDirPath(),
                                          QString::fromUtf8(PHSRADIO_KUGOU_SERVICE_DIR)))
{
    auto *deviceCookies = new KugouDeviceCookieJar(this);
    m_network.setCookieJar(deviceCookies);
    m_playbackNetwork.setCookieJar(deviceCookies);
    m_libraryNetwork.setCookieJar(deviceCookies);
    // setCookieJar reparents the jar. Keep it owned by the client so that it
    // outlives all three member managers (Qt's documented shared-jar pattern).
    deviceCookies->setParent(this);
    m_qrTimer.setInterval(2000);
    connect(&m_qrTimer, &QTimer::timeout, this, [this] { pollQrStatus(); });
#ifdef Q_OS_WIN
    m_serviceProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(&m_serviceProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            reportError(QStringLiteral("无法启动 Node.js，请检查软件文件是否完整，或配置系统 Node.js。"));
    });
}

KugouApiClient::~KugouApiClient()
{
    m_qrTimer.stop();
    if (m_serviceStartedByApp && m_serviceProcess.state() != QProcess::NotRunning) {
        m_serviceProcess.terminate();
        if (!m_serviceProcess.waitForFinished(1500))
            m_serviceProcess.kill();
    }
}

void KugouApiClient::startQrLogin()
{
    const quint64 generation = ++m_qrGeneration;
    m_loginInProgress = true;
    m_qrKey.clear();
    m_qrTimer.stop();
    reportStatus(QStringLiteral("正在连接本机酷狗服务…"));
    ensureService([this, generation] {
        if (generation == m_qrGeneration && m_loginInProgress)
            requestQrKey();
    });
}

void KugouApiClient::restoreSession()
{
    QSettings settings;
    QString token;
    QString userId;
    if (!unprotectSession(settings.value(QStringLiteral("accounts/kugou/session")).toString(),
                          &token, &userId))
        return;
    setAccountSession(token, userId);
    ensureService([this] { requestUserPlaylists(); });
}

void KugouApiClient::setAccountSession(const QString &token, const QString &userId)
{
    ++m_accountGeneration;
    m_token = token;
    m_userId = userId;
    m_authorization = token.isEmpty() || userId.isEmpty() ? QString()
        : QStringLiteral("token=%1;userid=%2").arg(token, userId);
    m_deviceId.clear();
    auto waiters = std::move(m_deviceWaiters);
    m_deviceWaiters.clear();
    for (const auto &callback : waiters)
        callback(QStringLiteral("酷狗账号已切换，请重新点击播放或搜索。"));
}

void KugouApiClient::fetchPlaylists()
{
    if (m_authorization.isEmpty()) {
        reportError(QStringLiteral("酷狗尚未登录，请先扫码连接账号。"));
        return;
    }
    ensureService([this] { requestUserPlaylists(); });
}

QVector<Playlist> KugouApiClient::playlists() const
{
    return m_playlists;
}

void KugouApiClient::fetchTrackCover(const Track &track, std::function<void(const QUrl &)> callback)
{
    if (!track.coverUrl.isEmpty()) {
        callback(highResolutionKugouCover(track.coverUrl));
        return;
    }
    if (track.id.isEmpty()) {
        callback({});
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), track.id);
    query.addQueryItem(QStringLiteral("count"), QStringLiteral("1"));
    requestJson(QStringLiteral("/images?") + query.toString(QUrl::FullyEncoded), {}, {},
                [callback = std::move(callback)](const QJsonObject &response, const QString &error) {
        QUrl cover;
        if (error.isEmpty()) {
            const QJsonArray rows = response.value(QStringLiteral("data")).toArray();
            if (!rows.isEmpty()) {
                const QJsonObject songImages = rows.first().toObject();
                const QJsonArray albums = songImages.value(QStringLiteral("album")).toArray();
                if (!albums.isEmpty())
                    cover = objectCoverUrl(albums.first().toObject());
            }
        }
        callback(cover);
    });
}

void KugouApiClient::fetchPlaylistCover(const Playlist &playlist,
                                       std::function<void(const QUrl &)> callback)
{
    if (!playlist.coverUrl.isEmpty()) {
        callback(playlist.coverUrl);
        return;
    }
    if (playlist.externalId.isEmpty()) {
        callback({});
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ids"), playlist.externalId);
    requestJson(QStringLiteral("/playlist/detail?") + query.toString(QUrl::FullyEncoded), {}, {},
                [callback = std::move(callback)](const QJsonObject &response, const QString &error) {
        QUrl cover;
        if (error.isEmpty()) {
            const QJsonArray rows = findArray(response,
                                              {QStringLiteral("info"), QStringLiteral("list"),
                                               QStringLiteral("data")},
                                              {QStringLiteral("global_collection_id"),
                                               QStringLiteral("imgurl"), QStringLiteral("listname")});
            if (!rows.isEmpty())
                cover = objectCoverUrl(rows.first().toObject());
            if (cover.isEmpty())
                cover = objectCoverUrl(response.value(QStringLiteral("data")).toObject());
        }
        callback(cover);
    });
}

void KugouApiClient::ensureService(std::function<void()> ready)
{
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [this, ready = std::move(ready), attempt](int number) {
        const QString path = QStringLiteral("/server/now?timestamp=%1")
                                 .arg(QDateTime::currentMSecsSinceEpoch());
        requestJson(path, {}, {}, [this, ready, attempt, number](const QJsonObject &, const QString &error) {
            if (error.isEmpty()) {
                ready();
                return;
            }
            if (number == 0) {
                startLocalService();
            }
            if (number >= 20) {
                reportError(QStringLiteral("无法启动酷狗本机服务：%1").arg(error));
                return;
            }
            QTimer::singleShot(500, this, [attempt, number] { (*attempt)(number + 1); });
        });
    };
    (*attempt)(0);
}

void KugouApiClient::startLocalService()
{
    if (m_serviceProcess.state() != QProcess::NotRunning)
        return;
    const QString serviceEntry = QDir(m_serviceRoot).filePath(QStringLiteral("server.js"));
    if (!QFileInfo(serviceEntry).isFile()
        || !QFileInfo(QDir(m_serviceRoot).filePath(QStringLiteral("node_modules/kugoumusicapi"))).isDir()) {
        reportError(QStringLiteral("酷狗服务文件不完整，请检查软件目录中的 services/kugou；开发环境请执行 npm install。"));
        return;
    }

    const QString nodeExecutable = resolveNodeExecutable(QCoreApplication::applicationDirPath(),
                                                          qEnvironmentVariable("PHSRADIO_NODE_EXECUTABLE"));
    if (nodeExecutable.isEmpty()) {
        reportError(QStringLiteral("找不到 Node.js。请保留软件自带的 runtime/node.exe，或将 Node.js 加入 PATH。"));
        return;
    }

    // The bridge resolves its modules from the absolute entry path. Keep its
    // current directory writable without writing into a read-only installation
    // or loading an unrelated development .env alongside the shipped resources.
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString workingDirectory = QDir(cacheRoot).filePath(QStringLiteral("kugou-service"));
    if (cacheRoot.isEmpty() || !QDir().mkpath(workingDirectory)) {
        reportError(QStringLiteral("无法创建酷狗服务缓存目录，请检查当前用户的目录权限。"));
        return;
    }
    QTemporaryFile writeCheck(QDir(workingDirectory).filePath(QStringLiteral(".write-check-XXXXXX")));
    if (!writeCheck.open()) {
        reportError(QStringLiteral("酷狗服务缓存目录无法写入，请检查当前用户的目录权限。"));
        return;
    }
    writeCheck.close();
    writeCheck.remove();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HOST"), QStringLiteral("127.0.0.1"));
    environment.insert(QStringLiteral("PORT"), QStringLiteral("3737"));
    m_serviceProcess.setProcessEnvironment(environment);
    m_serviceProcess.setWorkingDirectory(workingDirectory);
    m_serviceProcess.setProgram(nodeExecutable);
    m_serviceProcess.setArguments({serviceEntry});
    m_serviceProcess.start();
    m_serviceStartedByApp = true;
    reportStatus(QStringLiteral("正在启动本机酷狗服务…"));
}

void KugouApiClient::requestJson(const QString &path, const QJsonObject &body,
                                 const QString &authorization, JsonCallback callback)
{
    QUrl url(apiBaseUrl() + path);
    // The bridge caches by URL, ignoring POST bodies and Authorization.
    // Account requests and distinct POST parameters must never share a cache entry.
    if (!body.isEmpty() || !authorization.isEmpty()) {
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("timestamp"),
                           QString::number(QDateTime::currentMSecsSinceEpoch()));
        query.addQueryItem(QStringLiteral("request_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    const bool playbackRequest = path.startsWith(QStringLiteral("/register/dev"))
        || path.startsWith(QStringLiteral("/song/url"));
    const bool libraryRequest = path.startsWith(QStringLiteral("/user/playlist"))
        || path.startsWith(QStringLiteral("/playlist/track/"))
        || path.startsWith(QStringLiteral("/album/songs"))
        || path.startsWith(QStringLiteral("/search"));
    QNetworkAccessManager &network = playbackRequest ? m_playbackNetwork
        : libraryRequest ? m_libraryNetwork : m_network;
    request.setTransferTimeout(playbackRequest || libraryRequest ? 20000 : 10000);
    if (!authorization.isEmpty())
        request.setRawHeader("Authorization", authorization.toUtf8());

    QNetworkReply *reply = body.isEmpty()
        ? network.get(request)
        : ([&] {
              request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
              return network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
          })();
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const QString error = reply->errorString();
            const QJsonObject response = QJsonDocument::fromJson(payload).object();
            reply->deleteLater();
            callback(response, error);
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            reply->deleteLater();
            callback({}, QStringLiteral("酷狗服务返回了无法识别的数据。"));
            return;
        }
        reply->deleteLater();
        callback(document.object(), {});
    });
}

void KugouApiClient::requestQrKey()
{
    const quint64 generation = m_qrGeneration;
    const QString path = QStringLiteral("/login/qr/key?timestamp=%1")
                             .arg(QDateTime::currentMSecsSinceEpoch());
    requestJson(path, {}, {}, [this, generation](const QJsonObject &response, const QString &error) {
        if (generation != m_qrGeneration || !m_loginInProgress)
            return;
        if (!error.isEmpty()) {
            reportError(error);
            return;
        }
        m_qrKey = objectString(response.value(QStringLiteral("data")).toObject(),
                               {QStringLiteral("key"), QStringLiteral("qrcode")});
        if (m_qrKey.isEmpty()) {
            reportError(QStringLiteral("酷狗没有返回二维码 key。"));
            return;
        }
        requestQrImage();
    });
}

void KugouApiClient::requestQrImage()
{
    const quint64 generation = m_qrGeneration;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), m_qrKey);
    query.addQueryItem(QStringLiteral("qrimg"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    requestJson(QStringLiteral("/login/qr/create?") + query.toString(QUrl::FullyEncoded), {}, {},
                [this, generation](const QJsonObject &response, const QString &error) {
        if (generation != m_qrGeneration || !m_loginInProgress)
            return;
        if (!error.isEmpty()) {
            reportError(error);
            return;
        }
        const QString encoded = objectString(response.value(QStringLiteral("data")).toObject(),
                                             {QStringLiteral("base64")});
        const QByteArray imageData = QByteArray::fromBase64(encoded.section(QLatin1Char(','), -1).toLatin1());
        QImage image;
        image.loadFromData(imageData);
        if (image.isNull()) {
            reportError(QStringLiteral("酷狗未返回有效二维码图片。"));
            return;
        }
        if (onQrCodeReady)
            onQrCodeReady(image);
        reportStatus(QStringLiteral("请使用酷狗 App 扫描二维码，并在手机上确认登录。"));
        m_qrTimer.start();
    });
}

void KugouApiClient::pollQrStatus()
{
    if (!m_loginInProgress || m_qrKey.isEmpty())
        return;
    const quint64 generation = m_qrGeneration;
    const QString qrKey = m_qrKey;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), m_qrKey);
    query.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    requestJson(QStringLiteral("/login/qr/check?") + query.toString(QUrl::FullyEncoded), {}, {},
                [this, generation, qrKey](const QJsonObject &response, const QString &error) {
        if (generation != m_qrGeneration || !m_loginInProgress || qrKey != m_qrKey)
            return;
        if (!error.isEmpty()) {
            reportStatus(QStringLiteral("二维码状态暂时无法读取，正在重试…"));
            return;
        }
        const QJsonObject data = response.value(QStringLiteral("data")).toObject();
        const int status = data.value(QStringLiteral("status")).toInt();
        if (status == 0) {
            m_qrTimer.stop();
            m_loginInProgress = false;
            reportStatus(QStringLiteral("二维码已过期，请重新生成。"));
        } else if (status == 1) {
            reportStatus(QStringLiteral("等待扫描二维码…"));
        } else if (status == 2) {
            reportStatus(QStringLiteral("已扫码，请在酷狗 App 确认登录。"));
        } else if (status == 4) {
            m_qrTimer.stop();
            m_loginInProgress = false;
            const QString token = objectString(data, {QStringLiteral("token")});
            const QString userId = objectString(data, {QStringLiteral("userid"), QStringLiteral("user_id")});
            if (token.isEmpty() || userId.isEmpty()) {
                reportError(QStringLiteral("登录响应缺少 token 或用户 ID。"));
                return;
            }
            setAccountSession(token, userId);
            QSettings settings;
            const QString encrypted = protectedSession(m_token, m_userId);
            if (encrypted.isEmpty()) {
                reportError(QStringLiteral("无法使用 Windows DPAPI 安全保存酷狗登录状态。"));
                return;
            }
            settings.setValue(QStringLiteral("accounts/kugou/session"), encrypted);
            reportStatus(QStringLiteral("酷狗已登录，正在读取歌单…"));
            requestUserPlaylists();
        }
    });
}

void KugouApiClient::requestUserPlaylists(int page, QVector<Playlist> accumulated)
{
    if (m_authorization.isEmpty()) {
        reportError(QStringLiteral("没有可用的酷狗登录会话。"));
        return;
    }
    constexpr int pageSize = 100;
    requestJson(QStringLiteral("/user/playlist"),
                {{QStringLiteral("page"), page}, {QStringLiteral("pagesize"), pageSize}},
                m_authorization,
                [this, page, accumulated = std::move(accumulated)](const QJsonObject &response,
                                                                   const QString &error) mutable {
        if (!error.isEmpty()) {
            reportError(QStringLiteral("获取酷狗歌单失败：%1").arg(error));
            return;
        }
        if (response.contains(QStringLiteral("status"))
            && response.value(QStringLiteral("status")).toVariant().toInt() == 0) {
            reportError(QStringLiteral("酷狗拒绝了歌单请求，请重新登录后重试。"));
            return;
        }
        const QJsonArray rows = findArray(response,
                                          {QStringLiteral("info"), QStringLiteral("list"),
                                           QStringLiteral("lists"), QStringLiteral("playlist"),
                                           QStringLiteral("data")},
                                          {QStringLiteral("listid"), QStringLiteral("listname"),
                                           QStringLiteral("list_name")});
        const auto previousSize = accumulated.size();
        for (const QJsonValue &value : rows) {
            if (!value.isObject())
                continue;
            const QJsonObject item = value.toObject();
            Playlist playlist;
            playlist.id = objectString(item, {QStringLiteral("listid"), QStringLiteral("list_id"),
                                               QStringLiteral("id")});
            playlist.externalId = objectString(item, {QStringLiteral("global_collection_id"),
                                                       QStringLiteral("global_id"),
                                                       QStringLiteral("globalid")});
            playlist.name = objectString(item, {QStringLiteral("listname"), QStringLiteral("list_name"),
                                                 QStringLiteral("name")});
            playlist.coverUrl = objectCoverUrl(item);
            playlist.originalId = objectString(item, {QStringLiteral("list_create_gid")});
            playlist.source = item.value(QStringLiteral("source")).toVariant().toInt();
            if (playlist.source == 2)
                playlist.originalAlbumId = objectString(item, {QStringLiteral("list_create_listid")});
            playlist.isLiked = item.value(QStringLiteral("is_def")).toVariant().toInt() > 0;
            if (item.contains(QStringLiteral("count")))
                playlist.expectedTrackCount = item.value(QStringLiteral("count")).toVariant().toInt();
            const int type = item.contains(QStringLiteral("type"))
                ? item.value(QStringLiteral("type")).toVariant().toInt()
                : item.value(QStringLiteral("list_type")).toVariant().toInt();
            playlist.kind = type == 1 ? PlaylistKind::Favorite : PlaylistKind::Created;
            // is_mine is zero even for this account's own lists; type identifies ownership.
            if (playlist.id.isEmpty())
                playlist.id = playlist.externalId;
            if (playlist.id.isEmpty() || playlist.name.isEmpty())
                continue;
            const bool alreadyAdded = std::any_of(accumulated.cbegin(), accumulated.cend(),
                                                  [&playlist](const Playlist &existing) {
                return existing.id == playlist.id;
            });
            if (!alreadyAdded)
                accumulated.push_back(playlist);
        }

        if (!rows.isEmpty() && accumulated.size() > previousSize && page < 100) {
            requestUserPlaylists(page + 1, std::move(accumulated));
            return;
        }
        if (!rows.isEmpty()) {
            reportError(QStringLiteral("歌单分页重复或超出上限，未完整加载，请重试。"));
            return;
        }

        m_playlists = std::move(accumulated);
        if (m_playlists.isEmpty()) {
            if (onPlaylistsReady)
                onPlaylistsReady(m_playlists);
            reportStatus(QStringLiteral("已登录，但酷狗没有返回可显示的歌单。"));
            return;
        }
        if (onPlaylistsReady)
            onPlaylistsReady(m_playlists);
        reportStatus(QStringLiteral("歌单列表已加载，正在后台同步全部歌曲。"));
    });
}

void KugouApiClient::fetchPlaylistTracks(const Playlist &playlist, TracksCallback callback)
{
    if (m_authorization.isEmpty()) {
        callback({}, QStringLiteral("酷狗尚未登录。"));
        return;
    }
    fetchPlaylistCandidate(playlist, 0, {}, std::move(callback));
}

void KugouApiClient::fetchDailyRecommendations(TracksCallback callback)
{
    if (m_authorization.isEmpty()) {
        callback({}, QStringLiteral("请先扫码登录酷狗，再读取每日推荐。"));
        return;
    }
    const QString authorization = m_authorization;
    if (m_deviceId.isEmpty()) {
        ensurePlaybackDevice([this, authorization, callback = std::move(callback)](const QString &error) {
            if (authorization != m_authorization) { callback({}, QStringLiteral("酷狗账号已切换，请重新读取推荐。")); return; }
            if (!error.isEmpty()) { callback({}, QStringLiteral("酷狗推荐无法完成设备验证：%1").arg(error)); return; }
            fetchDailyRecommendations(std::move(callback));
        });
        return;
    }
    requestJson(QStringLiteral("/everyday/recommend"), {{QStringLiteral("platform"), QStringLiteral("android")}},
                authorization + QStringLiteral(";dfid=%1").arg(m_deviceId),
                [this, authorization, callback = std::move(callback)](const QJsonObject &body, const QString &error) {
        if (authorization != m_authorization) { callback({}, QStringLiteral("酷狗账号已切换，请重新读取推荐。")); return; }
        if (!error.isEmpty()) { callback({}, QStringLiteral("酷狗每日推荐请求失败：%1").arg(error)); return; }
        if (body.value(QStringLiteral("status")).toVariant().toInt() != 1) {
            callback({}, QStringLiteral("酷狗拒绝了每日推荐请求（错误码 %1），请检查登录状态。")
                .arg(objectString(body, {QStringLiteral("error_code"), QStringLiteral("errcode")}))); return;
        }
        QVector<Track> tracks;
        QSet<QString> known;
        for (const Track &track : parseTracks(body)) {
            if (track.id.isEmpty() && track.hash.isEmpty() && track.albumAudioId.isEmpty()) continue;
            const QString key = trackKey(track);
            if (!known.contains(key)) { known.insert(key); tracks.append(track); }
        }
        callback(tracks, {});
    });
}

void KugouApiClient::fetchLyrics(const Track &track, LyricsCallback callback)
{
    const QString hash = track.hash.isEmpty() ? track.id : track.hash;
    if (hash.isEmpty() && track.albumAudioId.isEmpty()) {
        callback({}, QStringLiteral("酷狗歌曲缺少歌词查询标识。")); return;
    }
    const QString authorization = m_authorization;
    requestJson(QStringLiteral("/search/lyric"),
                {{QStringLiteral("hash"), hash}, {QStringLiteral("album_audio_id"), track.albumAudioId},
                 {QStringLiteral("keywords"), track.artist + QLatin1Char(' ') + track.title},
                 {QStringLiteral("man"), QStringLiteral("no")}}, authorization,
                [this, authorization, callback = std::move(callback)](const QJsonObject &body, const QString &error) {
        if (authorization != m_authorization) { callback({}, QStringLiteral("酷狗账号已切换，请重新加载歌词。")); return; }
        const int status = body.value(QStringLiteral("status")).toVariant().toInt();
        if (!error.isEmpty() || (status != 200 && status != 1)) {
            callback({}, error.isEmpty() ? QStringLiteral("酷狗歌词查询被拒绝。") : QStringLiteral("酷狗歌词查询失败：%1").arg(error)); return;
        }
        QJsonArray candidates = body.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) candidates = body.value(QStringLiteral("data")).toObject().value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) { callback({}, {}); return; }
        const QJsonObject chosen = candidates.first().toObject();
        const QString id = objectString(chosen, {QStringLiteral("id")});
        const QString key = objectString(chosen, {QStringLiteral("accesskey")});
        if (id.isEmpty() || key.isEmpty()) { callback({}, QStringLiteral("酷狗歌词响应缺少下载标识。")); return; }
        requestJson(QStringLiteral("/lyric"),
                    {{QStringLiteral("id"), id}, {QStringLiteral("accesskey"), key},
                     {QStringLiteral("fmt"), QStringLiteral("lrc")}, {QStringLiteral("decode"), true}}, authorization,
                    [this, authorization, callback](const QJsonObject &lyric, const QString &error) {
            if (authorization != m_authorization) { callback({}, QStringLiteral("酷狗账号已切换，请重新加载歌词。")); return; }
            const int status = lyric.value(QStringLiteral("status")).toVariant().toInt();
            if (!error.isEmpty() || (status != 200 && status != 1)) {
                callback({}, error.isEmpty() ? QStringLiteral("酷狗歌词下载被拒绝。") : QStringLiteral("酷狗歌词下载失败：%1").arg(error)); return;
            }
            QString source = objectString(lyric, {QStringLiteral("decodeContent")});
            if (source.isEmpty()) {
                const QByteArray encoded = objectString(lyric, {QStringLiteral("content")}).toLatin1();
                source = QString::fromUtf8(QByteArray::fromBase64(encoded));
            }
            callback(parseLyrics(source), {});
        });
    });
}

void KugouApiClient::searchTrackNames(const QString &query, TracksCallback callback)
{
    searchCatalog(query, 1, [callback = std::move(callback)](const CatalogSearchPage &page, const QString &error) {
        QVector<Track> tracks;
        tracks.reserve(page.tracks.size());
        for (const CatalogTrack &result : page.tracks)
            tracks.push_back(result.track);
        callback(tracks, error);
    });
}

void KugouApiClient::searchCatalog(const QString &query, int page, CatalogSearchCallback callback)
{
    CatalogSearchPage emptyPage;
    emptyPage.page = qMax(1, page);
    const QString keywords = query.trimmed();
    if (keywords.isEmpty()) {
        callback(emptyPage, {});
        return;
    }
    if (m_authorization.isEmpty()) {
        callback(emptyPage, QStringLiteral("酷狗尚未登录，无法搜索在线曲库。"));
        return;
    }
    const QString authorization = m_authorization;
    const quint64 generation = m_accountGeneration;
    const QString searchAuthorization = m_deviceId.isEmpty() ? authorization
        : authorization + QStringLiteral(";dfid=%1").arg(m_deviceId);
    // module/search.js consumes "keywords", not "keyword", and defaults to 30.
    // It does not forward show_author_alias. The bridge accepts JSON parameters
    // and converts Authorization into cookie credentials required by /search.
    // Let /search validate its own authenticated request. A playback registration
    // failure must not prevent a separate search from reaching that endpoint.
    requestJson(QStringLiteral("/search"),
                {{QStringLiteral("keywords"), keywords}, {QStringLiteral("type"), QStringLiteral("song")},
                 {QStringLiteral("page"), emptyPage.page}, {QStringLiteral("pagesize"), emptyPage.pageSize}},
                searchAuthorization,
                [this, authorization, generation, emptyPage, callback = std::move(callback)]
                (const QJsonObject &response, const QString &error) {
        if (generation != m_accountGeneration || authorization != m_authorization) {
            callback(emptyPage, QStringLiteral("酷狗账号已切换，请重新搜索。"));
            return;
        }
        if (!error.isEmpty() && response.isEmpty()) {
            callback(emptyPage, QStringLiteral("酷狗在线曲库搜索失败：%1").arg(error));
            return;
        }
        if (response.value(QStringLiteral("status")).toVariant().toInt() != 1 || hasApiError(response)) {
            callback(emptyPage, QStringLiteral("酷狗在线曲库搜索被拒绝%1")
                .arg(apiFailureDetails(response, {m_token, m_userId, m_deviceId})));
            return;
        }
        if (!error.isEmpty()) {
            callback(emptyPage, QStringLiteral("酷狗在线曲库搜索失败：%1").arg(error));
            return;
        }
        CatalogSearchPage result = emptyPage;
        const QVector<Track> tracks = parseTracks(response);
        result.tracks.reserve(tracks.size());
        for (const Track &track : tracks)
            result.tracks.push_back({track, MusicPlatform::Kugou});
        result.total = catalogSearchTotal(response);
        const int rowCount = trackRows(response).size();
        result.hasMore = rowCount > 0 && (result.total >= 0
            ? qint64(result.page) * result.pageSize < result.total
            : rowCount >= result.pageSize);
        callback(result, {});
    });
}

void KugouApiClient::fetchPlaylistCandidate(Playlist playlist, int candidate, QVector<Track> best,
                                           TracksCallback callback)
{
    if (candidate == 1 && (playlist.externalId.isEmpty() || playlist.externalId == QStringLiteral("0")))
        candidate = 2;
    const QString original = playlist.source == 2 ? playlist.originalAlbumId : playlist.originalId;
    if (candidate == 2 && (original.isEmpty() || original == QStringLiteral("0")
        || original == playlist.externalId)) {
        callback(best, QStringLiteral("仅返回 %1 / %2 首歌曲，接口未提供其余记录。")
            .arg(best.size()).arg(playlist.expectedTrackCount));
        return;
    }
    Playlist requestPlaylist = playlist;
    if (candidate == 2)
        requestPlaylist.externalId = original;
    requestPlaylistTracks(requestPlaylist, 1, {},
        [this, playlist, candidate, best = std::move(best), callback]
        (const QVector<Track> &tracks, const QString &error) mutable {
        if (tracks.size() > best.size())
            best = tracks;
        if (error.isEmpty()) {
            callback(tracks, {});
            return;
        }
        if (candidate < 2) {
            fetchPlaylistCandidate(playlist, candidate + 1, std::move(best), callback);
            return;
        }
        callback(best, error);
    }, candidate != 0);
}

void KugouApiClient::requestPlaylistTracks(Playlist playlist, int page, QVector<Track> tracks,
                                          TracksCallback callback, bool publicEndpoint,
                                          QByteArray previousPage, int retry, int pageSize)
{
    const bool albumEndpoint = publicEndpoint && playlist.source == 2
        && playlist.externalId == playlist.originalAlbumId;
    if (albumEndpoint)
        pageSize = qMin(pageSize, 30);
    QJsonObject body{{QStringLiteral("page"), page}, {QStringLiteral("pagesize"), pageSize}};
    body.insert(publicEndpoint ? QStringLiteral("id") : QStringLiteral("listid"),
                publicEndpoint ? playlist.externalId : playlist.id);
    requestJson(albumEndpoint ? QStringLiteral("/album/songs")
                : publicEndpoint ? QStringLiteral("/playlist/track/all")
                               : QStringLiteral("/playlist/track/all/new"), body, m_authorization,
        [this, playlist, page, tracks = std::move(tracks), callback, publicEndpoint, previousPage, retry, pageSize]
        (const QJsonObject &response, const QString &error) mutable {
        QString failure = error;
        if (!error.isEmpty() && retry == 0) {
            QTimer::singleShot(350, this, [this, playlist, page, tracks = std::move(tracks),
                callback, publicEndpoint, previousPage, pageSize] () mutable {
                requestPlaylistTracks(playlist, page, std::move(tracks), callback, publicEndpoint, previousPage, 1, pageSize);
            });
            return;
        }
        if (failure.isEmpty() && response.contains(QStringLiteral("status"))
            && response.value(QStringLiteral("status")).toVariant().toInt() == 0)
            failure = QStringLiteral("酷狗拒绝了曲目请求。" );
        const QJsonArray rows = findArray(response,
            {QStringLiteral("info"), QStringLiteral("list"), QStringLiteral("songs"),
             QStringLiteral("files"), QStringLiteral("data")},
            {QStringLiteral("hash"), QStringLiteral("FileHash"), QStringLiteral("filename"),
             QStringLiteral("songname"), QStringLiteral("file"), QStringLiteral("audio_name")});
        QVector<Track> batch = failure.isEmpty() ? parseTracks(response) : QVector<Track>{};
        const QJsonObject data = response.value(QStringLiteral("data")).toObject();
        if (data.contains(QStringLiteral("count")))
            playlist.expectedTrackCount = qMax(playlist.expectedTrackCount,
                                               data.value(QStringLiteral("count")).toVariant().toInt());
        if (data.contains(QStringLiteral("total")))
            playlist.expectedTrackCount = qMax(playlist.expectedTrackCount,
                                               data.value(QStringLiteral("total")).toVariant().toInt());
        if (!failure.isEmpty()) {
            callback(tracks, failure);
            return;
        }
        if (batch.size() != rows.size()) {
            callback(tracks, QStringLiteral("曲目响应包含无法识别的记录，未完整加载。"));
            return;
        }
        const QByteArray fingerprint = QCryptographicHash::hash(
            QJsonDocument(rows).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
        if (!rows.isEmpty() && fingerprint == previousPage) {
            callback(tracks, QStringLiteral("接口重复返回同一页，未完整加载。"));
            return;
        }
        for (Track &track : batch)
            if (track.id.isEmpty())
                track.id = QStringLiteral("unavailable:%1:%2").arg(playlist.id).arg(tracks.size() + (&track - batch.data()));
        tracks += batch; // Preserve repeated entries; deduplicate only across the library view.
        const bool reachedTotal = playlist.expectedTrackCount >= 0 && tracks.size() >= playlist.expectedTrackCount;
        if (!rows.isEmpty() && !reachedTotal) {
            if (page >= 1000) {
                callback(tracks, QStringLiteral("超过分页上限，未完整加载。"));
                return;
            }
            // Honor a server cap on the first batch instead of skipping records.
            const int nextPageSize = page == 1 ? qMin(pageSize, static_cast<int>(rows.size())) : pageSize;
            requestPlaylistTracks(playlist, page + 1, std::move(tracks), callback, publicEndpoint, fingerprint, 0, nextPageSize);
            return;
        }
        if (playlist.expectedTrackCount > tracks.size()) {
            callback(tracks, QStringLiteral("接口仅返回 %1 / %2 首歌曲，可能已删除或不可访问。")
                .arg(tracks.size()).arg(playlist.expectedTrackCount));
            return;
        }
        callback(tracks, {});
    });
}

void KugouApiClient::reportStatus(const QString &status) const
{
    if (onStatusChanged)
        onStatusChanged(status);
}

void KugouApiClient::reportError(const QString &error) const
{
    if (onError)
        onError(error);
}

void KugouApiClient::fetchAudioUrl(const Track &track, AudioCallback callback)
{
    if (m_authorization.isEmpty()) {
        callback({}, QStringLiteral("请先登录酷狗账号。"));
        return;
    }
    const QString authorization = m_authorization;
    const quint64 generation = m_accountGeneration;
    // A numeric song ID is not a file hash and cannot be passed as one.
    const QString hash = !track.hash.isEmpty() ? track.hash : track.id;
    if (hash.size() != 32 || std::any_of(hash.cbegin(), hash.cend(), [](QChar ch) {
            return !QStringLiteral("0123456789abcdefABCDEF").contains(ch);
        })) {
        callback({}, QStringLiteral("这首歌曲缺少有效的音频标识，无法获取音源。"));
        return;
    }
    if (m_deviceId.isEmpty()) {
        ensurePlaybackDevice([this, track, callback, authorization, generation](const QString &error) {
            if (generation != m_accountGeneration || authorization != m_authorization) {
                callback({}, QStringLiteral("酷狗账号已切换，请重新点击播放。"));
                return;
            }
            if (!error.isEmpty()) {
                callback({}, error);
                return;
            }
            fetchAudioUrl(track, callback);
        });
        return;
    }
    QJsonObject body{{QStringLiteral("hash"), hash}, {QStringLiteral("quality"), 128}};
    if (!track.albumId.isEmpty())
        body.insert(QStringLiteral("album_id"), track.albumId);
    if (!track.albumAudioId.isEmpty())
        body.insert(QStringLiteral("album_audio_id"), track.albumAudioId);
    requestJson(QStringLiteral("/song/url"), body,
                authorization + QStringLiteral(";dfid=%1").arg(m_deviceId),
                [this, callback, authorization, generation](const QJsonObject &response, const QString &error) {
        if (generation != m_accountGeneration || authorization != m_authorization) {
            callback({}, QStringLiteral("酷狗账号已切换，请重新点击播放。"));
            return;
        }
        const QString details = apiFailureDetails(response, {m_token, m_userId, m_deviceId});
        if (!error.isEmpty()) {
            callback({}, details.isEmpty() ? QStringLiteral("获取音源失败：%1").arg(error)
                                          : QStringLiteral("酷狗获取音源失败%1").arg(details));
            return;
        }
        if ((response.contains(QStringLiteral("status"))
             && response.value(QStringLiteral("status")).toVariant().toInt() == 0)
            || hasApiError(response)) {
            callback({}, QStringLiteral("酷狗拒绝了播放请求%1").arg(details));
            return;
        }
        QJsonObject data = response.value(QStringLiteral("data")).toObject();
        if (data.isEmpty())
            data = response;
        QJsonValue urls = data.value(QStringLiteral("url"));
        if (urls.isUndefined() || urls.isNull())
            urls = data.value(QStringLiteral("play_url"));
        const QJsonArray candidates = urls.isArray() ? urls.toArray() : QJsonArray{urls};
        for (const QJsonValue &value : candidates) {
            const QUrl url(value.toString());
            if (url.isValid() && !url.host().isEmpty()
                && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))) {
                callback(url, {});
                return;
            }
        }
        callback({}, QStringLiteral("酷狗未提供可播放音源，可能需要会员、购买或当前地区无版权。"));
    });
}

void KugouApiClient::ensurePlaybackDevice(std::function<void(const QString &)> callback)
{
    if (!m_deviceId.isEmpty()) {
        callback({});
        return;
    }
    m_deviceWaiters.push_back(std::move(callback));
    if (m_deviceWaiters.size() == 1)
        registerPlaybackDevice(1, m_accountGeneration);
}

void KugouApiClient::registerPlaybackDevice(int attempt, quint64 generation)
{
    if (generation != m_accountGeneration || m_deviceWaiters.isEmpty())
        return;
    const QString authorization = m_authorization;
    requestJson(QStringLiteral("/register/dev"), {}, authorization,
        [this, attempt, authorization, generation](const QJsonObject &response, const QString &error) {
        // setAccountSession has already completed the old waiters. A late
        // response must neither overwrite dfid nor drain the new account queue.
        if (generation != m_accountGeneration)
            return;
        const bool accountChanged = authorization != m_authorization;
        QString device = objectString(response.value(QStringLiteral("data")).toObject(),
                                       {QStringLiteral("dfid")}).trimmed();
        if (device == QStringLiteral("undefined") || device == QStringLiteral("null")
            || device.contains(QRegularExpression(QStringLiteral("[\\s;\\x00-\\x1f\\x7f]"))))
            device.clear();
        const bool accepted = response.value(QStringLiteral("status")).toVariant().toInt() == 1;
        const bool rejected = (response.contains(QStringLiteral("status")) && !accepted)
            || hasApiError(response);
        const bool success = !accountChanged && error.isEmpty() && accepted && !rejected && !device.isEmpty();
        // A transport failure or incomplete success can be transient. Explicit
        // API rejections (including 20010) should not be hammered with retries.
        if (!success && !accountChanged && !rejected && attempt < 3) {
            QTimer::singleShot(500 * attempt, this, [this, attempt, generation, authorization] {
                if (generation == m_accountGeneration && authorization == m_authorization)
                    registerPlaybackDevice(attempt + 1, generation);
            });
            return;
        }
        if (success)
            m_deviceId = device;
        QString failure;
        if (!success) {
            if (accountChanged)
                failure = QStringLiteral("账号已切换，请重新点击播放。" );
            else if (rejected)
                failure = QStringLiteral("酷狗设备注册被拒绝%1。")
                    .arg(apiFailureDetails(response, {m_token, m_userId, m_deviceId}));
            else if (!error.isEmpty())
                failure = QStringLiteral("播放设备验证失败：%1").arg(error);
            else if (accepted)
                failure = QStringLiteral("酷狗设备验证缺少设备标识，已自动重试三次，请重新点击播放。" );
            else
                failure = QStringLiteral("酷狗设备注册返回了无法识别的结果%1，请稍后重试。")
                    .arg(apiFailureDetails(response, {m_token, m_userId, m_deviceId}));
        }
        auto waiters = std::move(m_deviceWaiters);
        m_deviceWaiters.clear();
        for (const auto &callback : waiters)
            callback(failure);
    });
}
