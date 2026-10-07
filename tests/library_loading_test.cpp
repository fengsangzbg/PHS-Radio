#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#define private public
#include "../kugou_api_client.cpp"
#undef private
#include "../playlist_snapshot_provider.cpp"
#include <cstdlib>

void check(bool value) { if (!value) std::abort(); }

void verifyPortableResources()
{
    QTemporaryDir temporary;
    check(temporary.isValid());
    const QString applicationRoot = temporary.filePath(QStringLiteral("软件 发行版"));
    const QString developmentRoot = temporary.filePath(QStringLiteral("development/services/kugou"));
    const QString bundledRoot = QDir(applicationRoot).filePath(QStringLiteral("services/kugou"));
    check(QDir().mkpath(applicationRoot) && QDir().mkpath(developmentRoot));
    check(resolveKugouServiceRoot(applicationRoot, developmentRoot) == developmentRoot);
    check(QDir().mkpath(bundledRoot));
    check(resolveKugouServiceRoot(applicationRoot, developmentRoot) == bundledRoot);
    check(resolveKugouServiceRoot(applicationRoot, {}) == bundledRoot);

#ifdef Q_OS_WIN
    const QString nodeName = QStringLiteral("node.exe");
#else
    const QString nodeName = QStringLiteral("node");
#endif
    const auto executable = [](const QString &path) {
        check(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly));
        check(file.write("portable test fixture") > 0);
        file.close();
        check(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        return path;
    };
    const QString systemBin = temporary.filePath(QStringLiteral("system bin"));
    const QString systemNode = executable(QDir(systemBin).filePath(nodeName));
    const QString configuredNode = executable(temporary.filePath(QStringLiteral("configured/") + nodeName));
    const QString bundledNode = executable(QDir(applicationRoot).filePath(nodeName));
    const QString runtimeNode = executable(QDir(applicationRoot).filePath(QStringLiteral("runtime/") + nodeName));

    // A release's runtime wins over stale developer settings and another Node on PATH.
    check(resolveNodeExecutable(applicationRoot, configuredNode, {systemBin}) == runtimeNode);
    check(QFile::remove(runtimeNode));
    check(resolveNodeExecutable(applicationRoot, configuredNode, {systemBin}) == bundledNode);
    check(QFile::remove(bundledNode));
    check(resolveNodeExecutable(applicationRoot, configuredNode, {systemBin}) == configuredNode);
    check(resolveNodeExecutable(applicationRoot, {}, {systemBin}) == systemNode);
    check(resolveNodeExecutable(applicationRoot, temporary.filePath(QStringLiteral("missing-node.exe")),
                                {systemBin}) == systemNode);

    // Resource discovery uses the executable's supplied root, never the caller's cwd.
    const QString originalDirectory = QDir::currentPath();
    check(QDir::setCurrent(systemBin));
    const bool independent = resolveKugouServiceRoot(applicationRoot, developmentRoot) == bundledRoot;
    check(QDir::setCurrent(originalDirectory));
    check(independent);
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QCoreApplication app(argc, argv);
    verifyPortableResources();
    const QJsonObject song{{"hash", "abcdef"}, {"mixsongid", 123},
        {"singerinfo", QJsonArray{QJsonObject{{"name", "Artist"}}}},
        {"albuminfo", QJsonObject{{"name", "Album"}}}};
    const auto parsed = parseTracks(QJsonObject{{"data", QJsonObject{{"info", QJsonArray{song}}}}});
    check(parsed.size() == 1 && !parsed.first().title.isEmpty());
    check(parsed.first().artist == "Artist" && parsed.first().album == "Album");
    const QJsonObject aliasSong{
        {"hash", "alias-hash"}, {"songname", "Primary Title"}, {"audio_name", "Alternate Title"},
        {"ori_audio_name", "Original Title"}, {"author_name", "Displayed Artist"},
        {"song_alias", QJsonArray{"Translated Title", " Alternate Title ", ""}},
        {"aliases", QJsonArray{QJsonObject{{"name", "Nickname"}}, 123, false,
                               QJsonObject{{"id", 99}}, QJsonObject{{"value", "English Title"}}}},
        {"file", QJsonObject{{"alias", "File Alternate Title"}}},
        {"singerinfo", QJsonArray{QJsonObject{{"name", "Original Artist"},
            {"aliases", QJsonArray{"Artist Nickname", "artist nickname"}}}}},
        {"albuminfo", QJsonObject{{"name", "Displayed Album"}, {"alias", "Album Translation"}}},
        {"album_info", QJsonObject{{"translated_name", "Other Album Translation"}}}};
    const auto aliasTracks = parseTracks(QJsonObject{{"info", QJsonArray{aliasSong}}});
    check(aliasTracks.size() == 1);
    const Track &aliasTrack = aliasTracks.first();
    check(aliasTrack.title == "Primary Title" && aliasTrack.artist == "Displayed Artist"
          && aliasTrack.album == "Displayed Album");
    for (const QString &alias : {QStringLiteral("Alternate Title"), QStringLiteral("Original Title"),
        QStringLiteral("Translated Title"), QStringLiteral("Nickname"), QStringLiteral("English Title"),
        QStringLiteral("File Alternate Title"), QStringLiteral("Original Artist"),
        QStringLiteral("Artist Nickname"), QStringLiteral("Album Translation"),
        QStringLiteral("Other Album Translation")})
        check(aliasTrack.searchAliases.contains(alias));
    check(aliasTrack.searchAliases.size() == 10); // Empty/duplicate values and unrelated IDs are excluded.
    const auto alternateAlbum = parseTracks(QJsonObject{{"info", QJsonArray{QJsonObject{
        {"hash", "album-names"}, {"songname", "Song"}, {"album_name", "Primary Album"},
        {"album", "Alternate Album"}}}}});
    check(alternateAlbum.first().album == "Primary Album"
          && alternateAlbum.first().searchAliases.contains("Alternate Album"));
    Playlist first, second;
    first.tracks = parsed;
    second.tracks = parsed;
    second.tracks.first().albumAudioId = "456";
    second.tracks.first().title = "Other playlist only";
    PlaylistSnapshotProvider provider(MusicPlatform::Kugou, {first, first, second});
    check(provider.likedTracks().size() == 2); // All playlists; distinct recordings stay distinct.
    const auto matches = provider.search("OTHER PLAYLIST");
    check(matches.size() == 1 && matches.first().albumAudioId == "456");
    check(provider.search("no matching song").isEmpty());

    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0));
    int pages = 0, fallbacks = 0, albumPages = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes] {
                *bytes += socket->readAll();
                const auto boundary = bytes->indexOf("\r\n\r\n");
                if (boundary < 0) return;
                const QByteArray header = bytes->left(boundary);
                int length = 0;
                for (const auto &line : header.split('\n'))
                    if (line.trimmed().toLower().startsWith("content-length:"))
                        length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                if (bytes->size() < boundary + 4 + length) return;
                const auto body = QJsonDocument::fromJson(bytes->mid(boundary + 4, length)).object();
                QJsonArray info;
                int total = 3;
                if (body.value("id").toString() == "42") {
                    ++albumPages;
                    check(header.contains("/album/songs"));
                    check(body.value("pagesize").toInt() == 30);
                    total = 31;
                    const int count = body.value("page").toInt() == 1 ? 30 : 1;
                    for (int i = 0; i < count; ++i) info.append(song);
                } else if (body.value("listid").toString() == "album-copy"
                    || body.value("id").toString() == "album-mirror") {
                    total = 31;
                } else if (body.value("listid").toString() == "paged") {
                    ++pages;
                    if (body.value("page").toInt() == 2)
                        check(body.value("pagesize").toInt() == 2);
                    info = body.value("page").toInt() == 1 ? QJsonArray{song, song} : QJsonArray{song};
                } else {
                    ++fallbacks;
                    total = 2;
                    if (body.value("id").toString() == "original") info = {song, song};
                }
                const QByteArray response = QJsonDocument(QJsonObject{{"status", 1},
                    {"data", QJsonObject{{"count", total}, {"info", info}}}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(response.size()) + "\r\n\r\n" + response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    KugouApiClient client;
#ifdef Q_OS_WIN
    QProcess::CreateProcessArguments serviceArguments{};
    const auto processModifier = client.m_serviceProcess.createProcessArgumentsModifier();
    check(static_cast<bool>(processModifier));
    processModifier(&serviceArguments);
    check(serviceArguments.flags & CREATE_NO_WINDOW);
#endif
    client.m_authorization = "test";
    client.m_libraryNetwork.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", server.serverPort()));
    Playlist paged;
    paged.id = "paged";
    paged.expectedTrackCount = 3;
    client.fetchPlaylistTracks(paged, [&](const QVector<Track> &tracks, const QString &error) {
        check(error.isEmpty() && tracks.size() == 3 && pages == 2); // Repeated songs do not truncate paging.
        Playlist fallback;
        fallback.id = "copy";
        fallback.externalId = "mirror";
        fallback.originalId = "original";
        fallback.expectedTrackCount = 2;
        client.fetchPlaylistTracks(fallback, [&](const QVector<Track> &full, const QString &failure) {
            check(failure.isEmpty() && full.size() == 2 && fallbacks == 3);
            Playlist album;
            album.id = "album-copy";
            album.externalId = "album-mirror";
            album.originalAlbumId = "42";
            album.source = 2;
            album.expectedTrackCount = 31;
            client.fetchPlaylistTracks(album, [&](const QVector<Track> &songs, const QString &albumError) {
                check(albumError.isEmpty() && songs.size() == 31 && albumPages == 2);
                app.quit();
            });
        });
    });
    QTimer::singleShot(10000, &app, [&app] { app.exit(1); });
    return app.exec();
}
