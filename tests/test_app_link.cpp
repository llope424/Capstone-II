#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include "Elm327Connection.h"
#include "emulator/Elm327EmulatorEngine.h"
#include "emulator/EmulatorTransport.h"
#include "emulator/ObdMessageModel.h"
#include "emulator/PidEncoders.h"

// End-to-end link test: the app's real ELM327 backend (Elm327Connection, the
// exact code the GUI uses) talking to the real in-app emulator over a real
// loopback TCP socket. Covers the init handshake, live-PID decoding, DTC
// parsing, freeze-frame retrieval, and multi-frame VIN reassembly - the same
// paths exercised against a physical adapter and car.
class TestAppLink : public QObject
{
    Q_OBJECT

private slots:
    void fullSessionAgainstEmulator();

private:
    // Spin the shared event loop until `done` returns true or timeout.
    static bool waitFor(const std::function<bool()> &done, int timeoutMs = 5000)
    {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    }
};

void TestAppLink::fullSessionAgainstEmulator()
{
    // Emulated vehicle: 1720 rpm, 92 km/h, a stored misfire, standard VIN.
    ObdMessageModel model;
    model.setPidRaw(0x0C, PidEncoders::encode(0x0C, 1720.0));
    model.setPidRaw(0x0D, PidEncoders::encode(0x0D, 92.0));
    model.setStoredDtcs({"P0301"});
    Elm327EmulatorEngine engine(&model);
    TcpEmulatorTransport transport(&engine, 0); // OS-assigned port
    QString err;
    QVERIFY2(transport.start(&err), qPrintable(err));

    Elm327Connection app;
    QSignalSpy connectedSpy(&app, &Elm327Connection::connected);
    QSignalSpy dtcSpy(&app, &Elm327Connection::dtcsReceived);
    QSignalSpy pidSpy(&app, &Elm327Connection::pidUpdated);
    QSignalSpy freezeDtcSpy(&app, &Elm327Connection::freezeFrameDtcReceived);
    QSignalSpy freezePidSpy(&app, &Elm327Connection::freezeFramePidReceived);
    QSignalSpy vinSpy(&app, &Elm327Connection::vinReceived);

    // 1. Connect: the full ATZ/ATE0/ATL0/ATH0/ATSP0 handshake must complete.
    app.openNetwork("127.0.0.1", transport.port());
    QVERIFY2(waitFor([&] { return connectedSpy.count() > 0; }), "init handshake");
    QVERIFY(app.isOpen());

    // 2. Stored DTCs decode to the injected misfire.
    app.readStoredDtcs();
    QVERIFY2(waitFor([&] { return dtcSpy.count() > 0; }), "DTC read");
    QCOMPARE(dtcSpy.first().at(0).toUInt(), 0x03u);
    QCOMPARE(dtcSpy.first().at(1).toStringList(), QStringList{"P0301"});

    // 3. Live monitoring round-trips the engineering values.
    app.startMonitoring(20);
    QVERIFY2(waitFor([&] {
                 for (const auto &args : pidSpy)
                     if (args.at(0).toUInt() == 0x0Cu && qFuzzyCompare(args.at(1).toDouble(), 1720.0))
                         return true;
                 return false;
             }),
             "RPM value decoded");
    app.stopMonitoring();

    // 4. Freeze frame: trigger DTC, captured RPM, and an uncaptured PID.
    app.readFreezeFrame();
    QVERIFY2(waitFor([&] { return freezeDtcSpy.count() > 0 && freezePidSpy.count() >= 10; }),
             "freeze frame sweep");
    QCOMPARE(freezeDtcSpy.first().at(0).toString(), QString("P0301"));
    QCOMPARE(freezeDtcSpy.first().at(1).toBool(), true);
    bool sawRpm = false, sawUncaptured = false;
    for (const auto &args : freezePidSpy) {
        if (args.at(0).toUInt() == 0x0Cu) {
            QVERIFY(args.at(2).toBool());
            QCOMPARE(args.at(1).toDouble(), 1720.0);
            sawRpm = true;
        }
        if (args.at(0).toUInt() == 0x05u) { // coolant not set in the model
            QCOMPARE(args.at(2).toBool(), false);
            sawUncaptured = true;
        }
    }
    QVERIFY(sawRpm);
    QVERIFY(sawUncaptured);

    // 5. VIN arrives via ELM327 multi-frame output and reassembles intact.
    app.readVin();
    QVERIFY2(waitFor([&] { return vinSpy.count() > 0; }), "VIN read");
    QCOMPARE(vinSpy.first().at(0).toString(), model.vin());

    app.close();
    transport.stop();
}

QTEST_MAIN(TestAppLink)
#include "test_app_link.moc"
