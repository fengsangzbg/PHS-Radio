#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#include <functional>
#include <memory>
#include <cstdlib>
#include <cstdio>
#define private public
#include "../kugou_api_client.cpp"
#undef private

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::fflush(stderr);
        std::abort();
    }
}

void events(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool until(const std::function<bool()> &ready, int timeout = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < timeout)
        events(10);
    return ready();
}

struct Request {
    QString path;
    QByteArray headers;
    QJsonObject body;

    QByteArray header(const QByteArray &name) const
    {
        for (const QByteArray &line : headers.split('\n')) {
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }
};

class MockService final {
public:
    QTcpServer server;
    QVector<Request> requests;
    std::function<void(const Request &, QPointer<QTcpSocket>)> handle;

    MockService()
    {
        check(server.listen(QHostAddress::LocalHost, 0), "The isolated loopback fixture must bind.");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto bytes = std::make_shared<QByteArray>();
                auto answered = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes, answered] {
                    bytes->append(socket->readAll());
                    const int end = bytes->indexOf("\r\n\r\n");
                    if (*answered || end < 0)
                        return;
                    const QByteArray headers = bytes->left(end);
                    const auto length = QRegularExpression(QStringLiteral("content-length:\\s*(\\d+)"))
                        .match(QString::fromLatin1(headers).toLower());
                    const int count = length.hasMatch() ? length.captured(1).toInt() : 0;
                    if (bytes->size() < end + 4 + count)
                        return;
                    *answered = true;
                    const QUrl url(QString::fromLatin1(headers.split('\n').first().split(' ').value(1)));
                    const Request request{url.path(), headers,
                        QJsonDocument::fromJson(bytes->mid(end + 4, count)).object()};
                    requests.append(request);
                    check(bool(handle), "Every fixture request must have an explicit handler.");
                    handle(request, QPointer<QTcpSocket>(socket));
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    void route(KugouApiClient &client) const
    {
        const QNetworkProxy proxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), server.serverPort());
        client.m_network.setProxy(proxy);
        client.m_libraryNetwork.setProxy(proxy);
        client.m_playbackNetwork.setProxy(proxy);
    }

    static void respond(QPointer<QTcpSocket> socket, const QJsonObject &body,
                        const QList<QByteArray> &cookies = {})
    {
        check(bool(socket), "The delayed fixture response must still have an owned connection.");
        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        QByteArray header("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n");
        for (const QByteArray &cookie : cookies)
            header += "Set-Cookie: " + cookie + "; Path=/\r\n";
        header += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n";
        socket->write(header + payload);
        socket->disconnectFromHost();
    }
};

const QList<QByteArray> identityCookies{
    "KUGOU_API_GUID=fixture-guid", "KUGOU_API_MID=fixture-mid",
    "KUGOU_API_DEV=fixture-dev", "KUGOU_API_WEBGL=fixture-webgl"
};

bool hasIdentity(const Request &request)
{
    const QByteArray cookie = request.header("Cookie");
    for (const QByteArray &expected : identityCookies)
        if (!cookie.contains(expected))
            return false;
    return true;
}

void clearFixtureSettings()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    check(settings.status() == QSettings::NoError, "The isolated fixture settings must be writable.");
}

void request(KugouApiClient &client, const QString &path)
{
    bool done = false;
    client.requestJson(path, {}, client.m_authorization, [&](const QJsonObject &, const QString &error) {
        check(error.isEmpty() && !done, "A fake bridge request must complete successfully once.");
        done = true;
    });
    check(until([&] { return done; }), "A fake bridge request timed out.");
}

void sharedIdentityAndPersistenceTest()
{
    clearFixtureSettings();
    MockService firstBridge;
    firstBridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        if (call.path == QStringLiteral("/fixture/identity")) {
            QList<QByteArray> cookies = identityCookies;
            cookies.append("token=fixture-cookie-token-do-not-persist");
            cookies.append("userid=fixture-cookie-user-do-not-persist");
            cookies.append("dfid=fixture-cookie-dfid-do-not-persist");
            MockService::respond(socket, {{QStringLiteral("status"), 1}}, cookies);
        } else if (call.path == QStringLiteral("/user/playlist")) {
            check(hasIdentity(call), "The library network must carry the identity established on the QR/ordinary network.");
            check(!call.header("Cookie").contains("token=") && !call.header("Cookie").contains("userid=")
                      && !call.header("Cookie").contains("dfid="),
                  "Account cookies and dfid must not be automatically sent by the shared identity jar.");
            MockService::respond(socket, {{QStringLiteral("status"), 1}});
        } else if (call.path == QStringLiteral("/register/dev")) {
            check(hasIdentity(call), "Device registration must use the same GUID/MID/DEV/WEBGL as login.");
            check(call.header("Authorization").contains("token=fixture-session-token"),
                  "Device registration must use the current account separately from device identity.");
            MockService::respond(socket, {{QStringLiteral("status"), 1},
                {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("registered-fixture-device")}}}});
        } else if (call.path == QStringLiteral("/song/url")) {
            check(hasIdentity(call) && call.header("Authorization").contains("dfid=registered-fixture-device"),
                  "Audio resolution must retain the shared identity and the actually registered dfid.");
            MockService::respond(socket, {{QStringLiteral("status"), 1}, {QStringLiteral("data"),
                QJsonObject{{QStringLiteral("url"), QJsonArray{QStringLiteral("https://fixture.invalid/authorized.mp3")}}}}});
        } else {
            check(false, "Unexpected request in the device identity fixture.");
        }
    };
    QPointer<QNetworkCookieJar> jar;
    {
        auto client = std::make_unique<KugouApiClient>();
        firstBridge.route(*client);
        client->setAccountSession(QStringLiteral("fixture-session-token"), QStringLiteral("fixture-session-user"));
        jar = client->m_network.cookieJar();
        check(jar && jar == client->m_libraryNetwork.cookieJar() && jar == client->m_playbackNetwork.cookieJar(),
              "All three network managers must share one device identity cookie jar.");
        request(*client, QStringLiteral("/fixture/identity"));
        request(*client, QStringLiteral("/user/playlist"));
        int registrations = 0;
        client->ensurePlaybackDevice([&](const QString &error) {
            check(error.isEmpty(), "Valid fake device registration must succeed.");
            ++registrations;
        });
        check(until([&] { return registrations == 1; }), "The shared-identity registration timed out.");
        Track track;
        track.id = QStringLiteral("0123456789abcdef0123456789abcdef");
        bool audioReady = false;
        client->fetchAudioUrl(track, [&](const QUrl &url, const QString &error) {
            check(error.isEmpty() && url.host() == QStringLiteral("fixture.invalid") && !audioReady,
                  "The fake audio URL must complete once using the registered device.");
            audioReady = true;
        });
        check(until([&] { return audioReady; }), "Fake audio resolution timed out.");
        client.reset();
    }
    check(jar.isNull(), "Destroying the client must delete its shared cookie jar exactly once.");

    QSettings settings;
    settings.sync();
    const QVariantMap saved = settings.value(QStringLiteral("accounts/kugou/deviceCookies")).toMap();
    for (const QByteArray &entry : identityCookies) {
        const int equals = entry.indexOf('=');
        check(saved.value(QString::fromLatin1(entry.left(equals))).toString()
                  == QString::fromLatin1(entry.mid(equals + 1)),
              "Every core device identity field must persist into the isolated profile.");
    }
    check(!saved.contains(QStringLiteral("token")) && !saved.contains(QStringLiteral("userid"))
              && !saved.contains(QStringLiteral("dfid")),
          "Device persistence must exclude account credentials and per-session dfid.");
    QFile ini(settings.fileName());
    check(ini.open(QIODevice::ReadOnly), "The isolated settings file must be readable.");
    const QByteArray persisted = ini.readAll();
    check(!persisted.contains("fixture-session-token") && !persisted.contains("fixture-session-user")
              && !persisted.contains("fixture-cookie-token-do-not-persist")
              && !persisted.contains("fixture-cookie-user-do-not-persist")
              && !persisted.contains("fixture-cookie-dfid-do-not-persist"),
          "No account credential or dfid may be stored as plaintext in the device cookie profile.");

    MockService restartedBridge;
    restartedBridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        check(call.path == QStringLiteral("/register/dev") && hasIdentity(call),
              "A new client/bridge instance must reuse the persisted identity, rather than its new bridge defaults.");
        MockService::respond(socket, {{QStringLiteral("status"), 1},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("fresh-session-device")}}}});
    };
    KugouApiClient restored;
    restartedBridge.route(restored);
    restored.setAccountSession(QStringLiteral("fixture-restored-token"), QStringLiteral("fixture-restored-user"));
    check(restored.m_deviceId.isEmpty(), "A new account session must not restore a previously registered dfid.");
    bool restoredReady = false;
    restored.ensurePlaybackDevice([&](const QString &error) {
        check(error.isEmpty() && !restoredReady, "The restored identity registration must complete once.");
        restoredReady = true;
    });
    check(until([&] { return restoredReady; }), "Restored device registration timed out.");
}

void partialIdentityGroupTest()
{
    clearFixtureSettings();
    {
        QSettings settings;
        settings.setValue(QStringLiteral("accounts/kugou/deviceCookies"),
            QVariantMap{{QStringLiteral("KUGOU_API_GUID"), QStringLiteral("stored-partial-guid")}});
        settings.sync();
    }
    MockService bridge;
    int partialRequests = 0;
    int completeRequests = 0;
    bridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        if (call.path == QStringLiteral("/fixture/partial")) {
            check(!call.header("Cookie").contains("KUGOU_API_"),
                  "A stored GUID without its full identity group must not be sent on a new client.");
            ++partialRequests;
            MockService::respond(socket, {{QStringLiteral("status"), 1}},
                {"KUGOU_API_GUID=runtime-partial-guid", "KUGOU_API_MID=runtime-partial-mid"});
        } else if (call.path == QStringLiteral("/fixture/complete")) {
            check(!call.header("Cookie").contains("KUGOU_API_"),
                  "An incomplete response must not freeze a partial GUID/MID for the next bridge request.");
            ++completeRequests;
            MockService::respond(socket, {{QStringLiteral("status"), 1}}, identityCookies);
        } else if (call.path == QStringLiteral("/fixture/current")) {
            check(hasIdentity(call) && !call.header("Cookie").contains("partial"),
                  "The ordinary manager must send all four fields from the complete replacement identity.");
            MockService::respond(socket, {{QStringLiteral("status"), 1}});
        } else if (call.path == QStringLiteral("/user/playlist")) {
            check(hasIdentity(call) && !call.header("Cookie").contains("partial"),
                  "The library manager must receive the complete new identity without any earlier partial fields.");
            MockService::respond(socket, {{QStringLiteral("status"), 1}});
        } else if (call.path == QStringLiteral("/register/dev")) {
            check(hasIdentity(call) && !call.header("Cookie").contains("partial"),
                  "The playback manager must use the same complete identity group as the ordinary and library managers.");
            MockService::respond(socket, {{QStringLiteral("status"), 1},
                {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("complete-group-device")}}}});
        } else {
            check(false, "Unexpected request in the partial identity fixture.");
        }
    };
    KugouApiClient client;
    bridge.route(client);
    client.setAccountSession(QStringLiteral("fixture-group-token"), QStringLiteral("fixture-group-user"));
    request(client, QStringLiteral("/fixture/partial"));
    request(client, QStringLiteral("/fixture/complete"));
    request(client, QStringLiteral("/fixture/current"));
    request(client, QStringLiteral("/user/playlist"));
    int callbacks = 0;
    client.ensurePlaybackDevice([&](const QString &error) {
        check(error.isEmpty(), "A complete replacement identity group must allow device registration.");
        ++callbacks;
    });
    check(until([&] { return callbacks == 1; }) && partialRequests == 1 && completeRequests == 1,
          "The partial and complete identity fixtures must each run once and complete registration once.");
    QSettings settings;
    const QVariantMap saved = settings.value(QStringLiteral("accounts/kugou/deviceCookies")).toMap();
    check(saved.size() == identityCookies.size(), "Only the complete new identity group must be persisted.");
    for (const QByteArray &entry : identityCookies) {
        const int equals = entry.indexOf('=');
        check(saved.value(QString::fromLatin1(entry.left(equals))).toString()
                  == QString::fromLatin1(entry.mid(equals + 1)),
              "Persisted identity fields must all come from the same complete bridge response.");
    }
}

void transientAndRejectionTest()
{
    clearFixtureSettings();
    MockService bridge;
    bool reject = false;
    bool alwaysMissing = false;
    int registrations = 0;
    int missingResponses = 0;
    bridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        check(call.path == QStringLiteral("/register/dev"), "Only registration belongs in the retry fixture.");
        ++registrations;
        if (reject) {
            MockService::respond(socket, {{QStringLiteral("status"), 0}, {QStringLiteral("error_code"), 20010},
                {QStringLiteral("message"), QStringLiteral("设备信息验证未通过 token=fixture-response-token-must-not-leak userid=fixture-retry-user")},
                {QStringLiteral("token"), QStringLiteral("fixture-response-token-must-not-leak")}});
        } else if (alwaysMissing) {
            QJsonObject response{{QStringLiteral("status"), 1}};
            switch (++missingResponses) {
            case 1: response.insert(QStringLiteral("data"), QJsonValue(QJsonValue::Null)); break;
            case 2: break; // A success status with no data field still has no usable dfid.
            default: response.insert(QStringLiteral("data"), QJsonArray{}); break;
            }
            MockService::respond(socket, response);
        } else if (registrations == 1) {
            MockService::respond(socket, {{QStringLiteral("status"), 1}, {QStringLiteral("data"), QJsonValue(QJsonValue::Null)}});
        } else {
            MockService::respond(socket, {{QStringLiteral("status"), 1},
                {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("retry-fixture-device")}}}});
        }
    };
    KugouApiClient client;
    bridge.route(client);
    client.setAccountSession(QStringLiteral("fixture-retry-token"), QStringLiteral("fixture-retry-user"));
    int completed = 0;
    const auto successful = [&](const QString &error) {
        check(error.isEmpty(), "A temporary missing dfid must recover on its next bounded attempt.");
        ++completed;
    };
    client.ensurePlaybackDevice(successful);
    client.ensurePlaybackDevice(successful);
    check(until([&] { return completed == 2; }) && registrations == 2,
          "Concurrent registration requests must coalesce and a transient missing dfid must retry once.");

    reject = true;
    for (int operation = 0; operation < 3; ++operation) {
        client.setAccountSession(QStringLiteral("fixture-retry-token"), QStringLiteral("fixture-retry-user"));
        const int before = registrations;
        int callbacks = 0;
        QString failure;
        client.ensurePlaybackDevice([&](const QString &error) { failure = error; ++callbacks; });
        check(until([&] { return callbacks == 1; }) && registrations == before + 1
                  && failure.contains(QStringLiteral("20010"))
                  && failure.contains(QStringLiteral("设备信息验证未通过"))
                  && !failure.contains(QStringLiteral("fixture-response-token-must-not-leak"))
                  && !failure.contains(QStringLiteral("fixture-retry-user"))
                  && client.m_deviceId.isEmpty(),
              "An explicit 20010 rejection must preserve its safe reason and code without blind retries or credential leakage.");
    }
    reject = false;
    alwaysMissing = true;
    client.setAccountSession(QStringLiteral("fixture-missing-token"), QStringLiteral("fixture-missing-user"));
    const int beforeMissing = registrations;
    int failedCallbacks = 0;
    client.ensurePlaybackDevice([&](const QString &error) {
        check(!error.isEmpty(), "Exhausted transient retries must fail explicitly.");
        ++failedCallbacks;
    });
    check(until([&] { return failedCallbacks == 1; }) && registrations == beforeMissing + 3
              && missingResponses == 3
              && client.m_deviceWaiters.isEmpty(),
          "Transient registration retries must stay bounded and release all waiters once.");
}

void inFlightAccountChangeTest(bool sameCredentials)
{
    clearFixtureSettings();
    MockService bridge;
    QPointer<QTcpSocket> oldSocket;
    int requests = 0;
    const QString nextToken = sameCredentials ? QStringLiteral("fixture-account-a") : QStringLiteral("fixture-account-b");
    const QString nextUser = sameCredentials ? QStringLiteral("fixture-user-a") : QStringLiteral("fixture-user-b");
    bridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        check(call.path == QStringLiteral("/register/dev"), "Only registration belongs in the account-switch fixture.");
        if (++requests == 1) {
            oldSocket = socket;
        } else {
            check(call.header("Authorization").contains("token=" + nextToken.toUtf8()),
                  "A fresh registration must use the new session's account.");
            MockService::respond(socket, {{QStringLiteral("status"), 1},
                {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device-b")}}}});
        }
    };
    KugouApiClient client;
    bridge.route(client);
    client.setAccountSession(QStringLiteral("fixture-account-a"), QStringLiteral("fixture-user-a"));
    const quint64 oldGeneration = client.m_accountGeneration;
    int oldCallbacks = 0;
    client.ensurePlaybackDevice([&](const QString &error) {
        check(!error.isEmpty(), "An obsolete account registration must fail its old waiter.");
        ++oldCallbacks;
    });
    check(until([&] { return bool(oldSocket); }), "The old registration must become genuinely in flight.");
    client.setAccountSession(nextToken, nextUser);
    check(client.m_accountGeneration != oldGeneration && oldCallbacks == 1 && client.m_deviceId.isEmpty(),
          "Changing accounts must immediately finish old waiters and invalidate their device generation.");
    int freshCallbacks = 0;
    client.ensurePlaybackDevice([&](const QString &error) {
        check(error.isEmpty(), "The new account must register independently of the held old reply.");
        ++freshCallbacks;
    });
    check(until([&] { return freshCallbacks == 1; }) && client.m_deviceId == QStringLiteral("device-b"),
          "The new account's current registration must finish while the obsolete reply is still pending.");
    MockService::respond(oldSocket, {{QStringLiteral("status"), 1},
        {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("stale-device-a")}}}});
    events(100);
    check(oldCallbacks == 1 && freshCallbacks == 1 && requests == 2
              && client.m_deviceId == QStringLiteral("device-b") && client.m_deviceWaiters.isEmpty(),
          "A delayed old reply must not overwrite the new dfid, retry, or complete either account's callbacks twice.");
}

void contradictoryErrorResponsesTest()
{
    clearFixtureSettings();
    MockService bridge;
    bool rejectRegistration = true;
    int registrations = 0;
    int audioRequests = 0;
    bridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        if (call.path == QStringLiteral("/register/dev")) {
            ++registrations;
            if (rejectRegistration) {
                MockService::respond(socket, {{QStringLiteral("status"), 1}, {QStringLiteral("error_code"), 20010},
                    {QStringLiteral("data"), QJsonObject{
                        {QStringLiteral("dfid"), QStringLiteral("must-not-accept-rejected-device")},
                        {QStringLiteral("errmsg"), QStringLiteral("设备校验拒绝")}}}});
            } else {
                MockService::respond(socket, {{QStringLiteral("status"), 1},
                    {QStringLiteral("data"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("valid-audio-device")}}}});
            }
        } else if (call.path == QStringLiteral("/song/url")) {
            ++audioRequests;
            check(call.header("Authorization").contains("dfid=valid-audio-device"),
                  "The audio error fixture must start with an actually accepted device registration.");
            MockService::respond(socket, {{QStringLiteral("status"), 1}, {QStringLiteral("error_code"), 20010},
                {QStringLiteral("data"), QJsonObject{
                    {QStringLiteral("url"), QJsonArray{QStringLiteral("https://fixture.invalid/must-not-play.mp3")}},
                    {QStringLiteral("errmsg"), QStringLiteral("设备信息验证未通过 token=fixture-contradictory-secret")}}}});
        } else {
            check(false, "Unexpected request in the contradictory API error fixture.");
        }
    };
    KugouApiClient client;
    bridge.route(client);
    client.setAccountSession(QStringLiteral("fixture-contradictory-secret"), QStringLiteral("fixture-contradictory-user"));
    int rejectedCallbacks = 0;
    QString rejection;
    client.ensurePlaybackDevice([&](const QString &error) { rejection = error; ++rejectedCallbacks; });
    check(until([&] { return rejectedCallbacks == 1; }) && registrations == 1
              && client.m_deviceId.isEmpty() && rejection.contains(QStringLiteral("20010"))
              && rejection.contains(QStringLiteral("设备校验拒绝")),
          "An explicit registration error must override status1 and a plausible dfid, without retrying or storing it.");

    rejectRegistration = false;
    int validCallbacks = 0;
    client.ensurePlaybackDevice([&](const QString &error) {
        check(error.isEmpty(), "The second fake registration must establish a valid device before testing audio errors.");
        ++validCallbacks;
    });
    check(until([&] { return validCallbacks == 1; }) && registrations == 2
              && client.m_deviceId == QStringLiteral("valid-audio-device"),
          "The audio error regression must exercise the song endpoint independently of device registration rejection.");
    Track track;
    track.id = QStringLiteral("abcdef0123456789abcdef0123456789");
    int audioCallbacks = 0;
    QUrl audioUrl;
    QString audioError;
    client.fetchAudioUrl(track, [&](const QUrl &url, const QString &error) {
        audioUrl = url;
        audioError = error;
        ++audioCallbacks;
    });
    check(until([&] { return audioCallbacks == 1; }) && audioRequests == 1
              && audioUrl.isEmpty() && audioError.contains(QStringLiteral("20010"))
              && audioError.contains(QStringLiteral("设备信息验证未通过"))
              && !audioError.contains(QStringLiteral("fixture-contradictory-secret"))
              && !audioError.contains(QStringLiteral("会员")),
          "An audio API error must override status1 and an included URL, preserve its safe reason/code, and never become a VIP fallback.");
}

void waitingRetryAccountChangeTest()
{
    clearFixtureSettings();
    MockService bridge;
    bridge.handle = [&](const Request &call, QPointer<QTcpSocket> socket) {
        check(call.path == QStringLiteral("/register/dev"), "Only registration belongs in the delayed-retry fixture.");
        MockService::respond(socket, {{QStringLiteral("status"), 1}, {QStringLiteral("data"), QJsonValue(QJsonValue::Null)}});
    };
    KugouApiClient client;
    bridge.route(client);
    client.setAccountSession(QStringLiteral("fixture-identical-token"), QStringLiteral("fixture-identical-user"));
    const quint64 oldGeneration = client.m_accountGeneration;
    int responses = 0;
    QObject::connect(&client.m_playbackNetwork, &QNetworkAccessManager::finished, &client,
        [&](QNetworkReply *) { ++responses; });
    int callbacks = 0;
    client.ensurePlaybackDevice([&](const QString &error) {
        check(!error.isEmpty(), "Replacing a session during retry delay must cancel its old waiter.");
        ++callbacks;
    });
    check(until([&] { return responses == 1; }) && bridge.requests.size() == 1 && callbacks == 0,
          "A temporary response must schedule retry without prematurely completing its waiter.");
    // Identical credentials after a new QR confirmation are still a new session.
    client.setAccountSession(QStringLiteral("fixture-identical-token"), QStringLiteral("fixture-identical-user"));
    check(client.m_accountGeneration != oldGeneration && callbacks == 1,
          "A new session must invalidate retries even when its token/userid strings are unchanged.");
    events(650);
    check(bridge.requests.size() == 1 && responses == 1 && callbacks == 1
              && client.m_deviceId.isEmpty() && client.m_deviceWaiters.isEmpty(),
          "The old 500ms retry closure must not reissue using the replacement session or finish its waiter twice.");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "Device registration tests require an isolated settings directory.");
    app.setOrganizationName(QStringLiteral("PHSRadioDeviceRegistrationFixture"));
    app.setApplicationName(QStringLiteral("IsolatedDeviceTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    sharedIdentityAndPersistenceTest();
    partialIdentityGroupTest();
    transientAndRejectionTest();
    inFlightAccountChangeTest(false);
    inFlightAccountChangeTest(true);
    contradictoryErrorResponsesTest();
    waitingRetryAccountChangeTest();
    std::puts("shared identity, safe persistence, rejection policy, retries and account generations: passed");
    return 0;
}
