#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>

#include "emulator/Elm327EmulatorEngine.h"
#include "emulator/EmulatorTransport.h"
#include "emulator/ObdMessageModel.h"
#include "emulator/PidEncoders.h"

class TestEmulatorTransport : public QObject
{
    Q_OBJECT

private slots:
    void tcpServesRealSocket();
};

// Drives the TCP transport through a real loopback socket, exactly as an external
// ELM327 client (including the app) would: send CR-terminated commands, read until
// the '>' prompt. Proves the engine works end-to-end over the wire.
void TestEmulatorTransport::tcpServesRealSocket()
{
    ObdMessageModel model;
    model.setPidRaw(0x0C, PidEncoders::encode(0x0C, 1720.0));
    model.setStoredDtcs({"P0301"});
    Elm327EmulatorEngine engine(&model);
    TcpEmulatorTransport transport(&engine, 0); // 0 => OS-assigned port

    QString err;
    QVERIFY2(transport.start(&err), qPrintable(err));
    const quint16 port = transport.port();
    QVERIFY(port != 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY2(client.waitForConnected(3000), qPrintable(client.errorString()));

    // Spin the real event loop (drives both client and server, which share this
    // thread) until the '>' prompt arrives or we time out.
    const auto request = [&](const QByteArray &cmd) -> QByteArray {
        client.write(cmd + "\r");
        client.flush();
        QByteArray resp;
        QElapsedTimer t;
        t.start();
        while (!resp.contains('>') && t.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            resp += client.readAll();
        }
        return resp;
    };

    QCOMPARE(request("ATZ"), QByteArray("ELM327 v1.5\r\r>"));
    QCOMPARE(request("ATE0"), QByteArray("OK\r\r>"));
    QCOMPARE(request("010C"), QByteArray("41 0C 1A E0\r\r>"));
    QCOMPARE(request("03"), QByteArray("43 01 03 01\r\r>"));

    client.disconnectFromHost();
    transport.stop();
    QVERIFY(!transport.running());
}

QTEST_MAIN(TestEmulatorTransport)
#include "test_emulator_transport.moc"
