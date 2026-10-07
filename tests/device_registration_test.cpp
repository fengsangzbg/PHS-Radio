#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#define private public
#include "../kugou_api_client.cpp"
#undef private
#include <cstdlib>
#include <cstdio>

void check(bool condition) { if (!condition) std::abort(); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0));
    int requests = 0;
    bool reject = false;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes] {
                *bytes += socket->readAll();
                if (!bytes->contains("\r\n\r\n")) return;
                ++requests;
                const QByteArray body = !reject && requests == 2
                    ? R"({"status":1,"data":{"dfid":"test-device"}})"
                    : R"({"status":1,"data":null})";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    KugouApiClient client;
    client.m_authorization = QStringLiteral("test-session");
    client.m_playbackNetwork.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", server.serverPort()));
    int callbacks = 0;
    auto complete = [&](const QString &error) {
        check(error.isEmpty());
        if (++callbacks != 2) return;
        check(requests == 2);
        client.m_deviceId.clear();
        reject = true;
        const int initial = requests;
        client.ensurePlaybackDevice([&, initial](const QString &failure) {
            check(!failure.isEmpty());
            check(requests - initial == 3);
            check(client.m_deviceWaiters.isEmpty());
            std::printf("missing dfid retry, concurrent request coalescing, retry limit: passed\n");
            app.quit();
        });
    };
    client.ensurePlaybackDevice(complete);
    client.ensurePlaybackDevice(complete);
    QTimer::singleShot(10000, &app, [&app] { app.exit(1); });
    return app.exec();
}
