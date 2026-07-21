#include <QtTest>
#include <QRegularExpression>

#include "emulator/Elm327EmulatorEngine.h"
#include "emulator/ObdMessageModel.h"
#include "emulator/PidEncoders.h"

class TestEmulatorEngine : public QObject
{
    Q_OBJECT

private slots:
    void atInit();
    void atEcho();
    void atIdentifyDescribeVoltage();
    void atProtocolReport();
    void atLinefeeds();
    void atSpacesToggleAck();
    void atUnknownReturnsQuestionMark();
    void atResetClearsModes();

    // A2 — Mode 01 live PIDs + value->raw encoders
    void pidEncodersRoundTrip();
    void mode01Pid();
    void mode01SpacesOff();
    void mode01SupportedPids();

    // A3 — Mode 03/07/0A read + Mode 04 clear
    void mode03Stored();
    void mode07And0A();
    void mode04Clear();
    void mode02FreezeFrame();
    void mode01Readiness();
    void multiEcuDtcLines();

    // A4 — Mode 09 VIN / Calibration IDs (multi-frame)
    void mode09Vin();
    void mode09CalIds();

    // A5 — scenario presets
    void scenariosLoadAndSwitch();

    // Error / robustness cases
    void errorCases();
    void toleratesSpacesAndCase();
    void sessionDump(); // prints a full RX->TX session for manual review
};

// Mimics Elm327Connection::parseHexBytes: concatenate every 2-hex-digit token,
// ignoring line numbers, length lines and separators. This is the exact lens the
// app views the emulator's multi-line response through.
static QByteArray appHexParse(const QByteArray &resp)
{
    QByteArray out;
    const QStringList toks = QString::fromLatin1(resp)
                                 .split(QRegularExpression("[^0-9A-Fa-f]+"), Qt::SkipEmptyParts);
    for (const QString &t : toks) {
        if (t.size() == 2) {
            bool ok = false;
            out.append(char(t.toInt(&ok, 16)));
        }
    }
    return out;
}

// The client (Elm327Connection) opens every session with ATZ/ATE0/ATL0/ATH0/ATSP0.
// The emulator must answer each: ATZ -> version string, the rest -> OK, all framed
// with the ELM327 prompt.
void TestEmulatorEngine::atInit()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    QCOMPARE(engine.handleLine("ATZ"), QByteArray("ELM327 v1.5\r\r>"));
    QCOMPARE(engine.handleLine("ATE0"), QByteArray("OK\r\r>"));
    QCOMPARE(engine.handleLine("ATL0"), QByteArray("OK\r\r>"));
    QCOMPARE(engine.handleLine("ATH0"), QByteArray("OK\r\r>"));
    QCOMPARE(engine.handleLine("ATSP0"), QByteArray("OK\r\r>"));
}

// ATE1 echoes the command back before the response; ATE0 turns it off.
void TestEmulatorEngine::atEcho()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    engine.handleLine("ATE1");
    QCOMPARE(engine.handleLine("ATI"), QByteArray("ATI\rELM327 v1.5\r\r>"));
    engine.handleLine("ATE0");
    QCOMPARE(engine.handleLine("ATI"), QByteArray("ELM327 v1.5\r\r>"));
}

// ATI identifies, AT@1 describes the device, ATRV reports a battery voltage.
void TestEmulatorEngine::atIdentifyDescribeVoltage()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    QCOMPARE(engine.handleLine("ATI"), QByteArray("ELM327 v1.5\r\r>"));
    QCOMPARE(engine.handleLine("AT@1"), QByteArray("OBDII to RS232 Interpreter\r\r>"));

    const QByteArray rv = engine.handleLine("ATRV");
    QVERIFY2(QRegularExpression("^[0-9]+\\.[0-9]+V\r\r>$")
                 .match(QString::fromLatin1(rv)).hasMatch(),
             rv.constData());
}

// ATDPN reports the (auto) protocol number; ATDP its description.
void TestEmulatorEngine::atProtocolReport()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    QCOMPARE(engine.handleLine("ATDPN"), QByteArray("A6\r\r>"));
    QVERIFY(engine.handleLine("ATDP").contains("ISO 15765"));
}

// ATL1 switches line endings to CR/LF for subsequent replies.
void TestEmulatorEngine::atLinefeeds()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    engine.handleLine("ATL1");
    QCOMPARE(engine.handleLine("ATI"), QByteArray("ELM327 v1.5\r\n\r\n>"));
}

// The spaces setting is accepted (its effect on OBD bytes is covered under A2).
void TestEmulatorEngine::atSpacesToggleAck()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    QCOMPARE(engine.handleLine("ATS0"), QByteArray("OK\r\r>"));
    QCOMPARE(engine.handleLine("ATS1"), QByteArray("OK\r\r>"));
}

// A genuinely unknown command returns the ELM327 "?" response.
void TestEmulatorEngine::atUnknownReturnsQuestionMark()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    QCOMPARE(engine.handleLine("ATBOGUS"), QByteArray("?\r\r>"));
}

// ATZ resets echo/linefeed (and other) modes back to defaults.
void TestEmulatorEngine::atResetClearsModes()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    engine.handleLine("ATE1");
    engine.handleLine("ATL1");
    engine.handleLine("ATZ");
    QCOMPARE(engine.handleLine("ATI"), QByteArray("ELM327 v1.5\r\r>"));
}

// Each encoder is the exact inverse of the corresponding ObdPidMonitor decode
// formula. Values chosen to quantize exactly so the round-trip is lossless.
void TestEmulatorEngine::pidEncodersRoundTrip()
{
    auto u16 = [](const QByteArray &b) { return (quint8(b[0]) * 256.0 + quint8(b[1])); };
    auto u8 = [](const QByteArray &b) { return double(quint8(b[0])); };

    QByteArray rpm = PidEncoders::encode(0x0C, 1720.0);
    QCOMPARE(u16(rpm) / 4.0, 1720.0);

    QCOMPARE(u8(PidEncoders::encode(0x0D, 100.0)), 100.0);            // speed km/h
    QCOMPARE(u8(PidEncoders::encode(0x05, 90.0)) - 40.0, 90.0);      // coolant degC
    QCOMPARE(u8(PidEncoders::encode(0x04, 20.0)) * 100.0 / 255.0, 20.0);   // load %
    QCOMPARE(u8(PidEncoders::encode(0x11, 40.0)) * 100.0 / 255.0, 40.0);   // throttle %
    QCOMPARE(u8(PidEncoders::encode(0x0F, 25.0)) - 40.0, 25.0);      // IAT degC
    QCOMPARE(u16(PidEncoders::encode(0x42, 13.5)) / 1000.0, 13.5);   // module voltage
    QCOMPARE(u8(PidEncoders::encode(0x0A, 300.0)) * 3.0, 300.0);     // fuel pressure kPa
}

// A Mode 01 PID request returns "41 <pid> <data>" with the current raw value.
void TestEmulatorEngine::mode01Pid()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    model.setPidRaw(0x0C, PidEncoders::encode(0x0C, 1720.0)); // 0x1A 0xE0
    QCOMPARE(engine.handleLine("010C"), QByteArray("41 0C 1A E0\r\r>"));
}

// With spaces off (ATS0) the bytes are packed with no separators.
void TestEmulatorEngine::mode01SpacesOff()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    model.setPidRaw(0x0C, PidEncoders::encode(0x0C, 1720.0));
    engine.handleLine("ATS0");
    QCOMPARE(engine.handleLine("010C"), QByteArray("410C1AE0\r\r>"));
}

// 01 00/20/40 advertise which PIDs the emulator supports, chaining across ranges.
void TestEmulatorEngine::mode01SupportedPids()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    // 28-PID set: bits for 04-07, 0A-10, 11/14/15, 1F, plus the chain bit.
    QCOMPARE(engine.handleLine("0100"), QByteArray("41 00 1E 7F 98 03\r\r>"));
    // 21/2E/2F/33 supported in 21-40; chains on to 41-60.
    QCOMPARE(engine.handleLine("0120"), QByteArray("41 20 80 06 20 01\r\r>"));
    // 42-46, 49, 4C, 5C, 5E supported in 41-60; no further ranges.
    QCOMPARE(engine.handleLine("0140"), QByteArray("41 40 7C 90 00 14\r\r>"));
}

// Mode 03 returns "43 <count> <2-byte codes>" (inverse of ObdDtcClient::decodeDtc:
// P0301 -> 03 01, P0420 -> 04 20). Matches the app's indexOf(0x43)+pairs parser.
void TestEmulatorEngine::mode03Stored()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    model.setStoredDtcs({"P0301", "P0420"});
    QCOMPARE(engine.handleLine("03"), QByteArray("43 02 03 01 04 20\r\r>"));
}

// Mode 07 (pending, service 0x47) and Mode 0A (permanent, service 0x4A).
void TestEmulatorEngine::mode07And0A()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    model.setPendingDtcs({"P0301"});
    model.setPermanentDtcs({"U0100"}); // U0100 -> C1 00
    QCOMPARE(engine.handleLine("07"), QByteArray("47 01 03 01\r\r>"));
    QCOMPARE(engine.handleLine("0A"), QByteArray("4A 01 C1 00\r\r>"));
}

// Mode 04 acknowledges with "44" and clears stored DTCs.
void TestEmulatorEngine::mode04Clear()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    model.setStoredDtcs({"P0301", "P0420"});
    QCOMPARE(engine.handleLine("04"), QByteArray("44\r\r>"));
    QCOMPARE(engine.handleLine("03"), QByteArray("43 00\r\r>"));
}

// Mode 09 PID 02: VIN as a multi-frame response the app reassembles to 17 chars.
void TestEmulatorEngine::mode09Vin()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);
    model.setVin("1HGBH41JXMN109186");

    const QByteArray bytes = appHexParse(engine.handleLine("0902"));
    const int idx = bytes.indexOf(QByteArray::fromHex("4902"));
    QVERIFY2(idx >= 0, "response must contain the 49 02 marker");
    int p = idx + 2;
    if (quint8(bytes.at(p)) <= 0x0F)
        p += 1; // skip NODI
    QCOMPARE(QString::fromLatin1(bytes.mid(p, 17)), QString("1HGBH41JXMN109186"));
}

// Mode 09 PID 04: calibration IDs as 16-byte NUL-padded chunks.
void TestEmulatorEngine::mode09CalIds()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);
    model.setCalIds({"CAL0001", "CAL0002"});

    const QByteArray bytes = appHexParse(engine.handleLine("0904"));
    const int idx = bytes.indexOf(QByteArray::fromHex("4904"));
    QVERIFY2(idx >= 0, "response must contain the 49 04 marker");
    int p = idx + 2;
    if (quint8(bytes.at(p)) <= 0x0F)
        p += 1; // skip NODI

    QStringList ids;
    QByteArray chunk;
    for (; p < bytes.size(); ++p) {
        chunk.append(bytes.at(p));
        if (chunk.size() == 16) {
            const int end = chunk.indexOf('\0');
            if (end >= 0)
                chunk.truncate(end);
            const QString id = QString::fromLatin1(chunk).trimmed();
            if (!id.isEmpty())
                ids << id;
            chunk.clear();
        }
    }
    QCOMPARE(ids, QStringList({"CAL0001", "CAL0002"}));
}

// Malformed input -> "?"; well-formed-but-unsupported -> "NO DATA".
void TestEmulatorEngine::errorCases()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    QCOMPARE(engine.handleLine(""), QByteArray("?\r\r>"));          // empty
    QCOMPARE(engine.handleLine("XYZ"), QByteArray("?\r\r>"));       // non-hex garbage
    QCOMPARE(engine.handleLine("01ZZ"), QByteArray("?\r\r>"));      // non-hex PID
    QCOMPARE(engine.handleLine("0199"), QByteArray("NO DATA\r\r>")); // unsupported PID
    QCOMPARE(engine.handleLine("99"), QByteArray("NO DATA\r\r>"));   // unknown but valid-hex mode
    QCOMPARE(engine.handleLine("22F190"), QByteArray("NO DATA\r\r>"));// UDS request we don't model
}

// ELM327 ignores spaces and is case-insensitive.
void TestEmulatorEngine::toleratesSpacesAndCase()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);
    model.setPidRaw(0x0C, PidEncoders::encode(0x0C, 1720.0));

    QCOMPARE(engine.handleLine("01 0c"), QByteArray("41 0C 1A E0\r\r>"));
    QCOMPARE(engine.handleLine("atz"), QByteArray("ELM327 v1.5\r\r>"));
}

// Not an assertion test: replays the exact init+poll session the app sends, plus a
// few error inputs, printing RX -> TX so behaviour can be reviewed by eye.
void TestEmulatorEngine::sessionDump()
{
    ObdMessageModel model;
    model.setPidRaw(0x0C, PidEncoders::encode(0x0C, 1720.0)); // RPM 1720
    model.setPidRaw(0x0D, PidEncoders::encode(0x0D, 100.0));  // speed 100
    model.setPidRaw(0x05, PidEncoders::encode(0x05, 90.0));   // coolant 90C
    model.setStoredDtcs({"P0301", "P0420"});
    model.setVin("1HGBH41JXMN109186");

    Elm327EmulatorEngine engine(&model);

    const char *session[] = {
        "ATZ", "ATE0", "ATL0", "ATH0", "ATSP0",          // app init
        "0100", "010C", "010D", "0105",                   // live data
        "03", "0902",                                      // DTCs, VIN
        "0199", "GARBAGE", "01ZZ",                         // error inputs
    };
    qInfo("---- emulator session ----");
    for (const char *cmd : session) {
        QByteArray tx = engine.handleLine(cmd);
        tx.replace("\r", "\\r");
        qInfo("  %-8s -> %s", cmd, tx.constData());
    }
}

// Scenarios load from the bundled JSON and, when selected, drive what the engine
// reports (DTCs present, RPM value, etc.).
void TestEmulatorEngine::scenariosLoadAndSwitch()
{
    ObdMessageModel model;
    QVERIFY2(model.loadBundled(), "bundled obd_message.json must load");
    QVERIFY(model.scenarioNames().contains("Misfire (P0301)"));
    QVERIFY(model.scenarioNames().contains("Healthy (default)"));

    Elm327EmulatorEngine engine(&model);

    model.setActiveScenario("Misfire (P0301)");
    QCOMPARE(model.activeScenario(), QString("Misfire (P0301)"));
    QVERIFY(model.storedDtcs().contains("P0301"));
    QCOMPARE(engine.handleLine("03"), QByteArray("43 02 03 00 03 01\r\r>")); // P0300, P0301
    // RPM 3200 -> raw 3200*4 = 12800 = 0x3200
    QCOMPARE(engine.handleLine("010C"), QByteArray("41 0C 32 00\r\r>"));

    model.setActiveScenario("Healthy (default)");
    QCOMPARE(engine.handleLine("03"), QByteArray("43 00\r\r>")); // no DTCs
}

// Mode 02 freeze frame: PID 02 reports the trigger DTC (00 00 when nothing is
// stored); data PIDs mirror the Mode 01 raw encodings with a frame-number
// byte, and answer NO DATA when no frame was captured.
void TestEmulatorEngine::mode02FreezeFrame()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    // No stored DTC: no freeze frame.
    QCOMPARE(appHexParse(engine.handleLine("020200")),
             QByteArray("\x42\x02\x00\x00\x00", 5));
    QVERIFY(engine.handleLine("020C00").contains("NO DATA"));

    // With a misfire stored, the trigger DTC and the captured RPM come back.
    model.setStoredDtcs({"P0301"});
    model.setPidRaw(0x0C, QByteArray("\x32\x00", 2)); // 3200 rpm
    QCOMPARE(appHexParse(engine.handleLine("020200")),
             QByteArray("\x42\x02\x00\x03\x01", 5));
    QCOMPARE(appHexParse(engine.handleLine("020C00")),
             QByteArray("\x42\x0C\x00\x32\x00", 5));

    // A PID the model has no value for is not part of the frame.
    QVERIFY(engine.handleLine("020D00").contains("NO DATA"));
}

// Mode 01 PID 01 (readiness) derives from the DTC state: MIL + count with
// stored codes, misfire monitor incomplete while a P03xx code is stored.
void TestEmulatorEngine::mode01Readiness()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);

    // Healthy: MIL off, zero codes, continuous monitors ready.
    QCOMPARE(engine.handleLine("0101"), QByteArray("41 01 00 07 E5 00\r\r>"));

    // Misfire stored: MIL on, 2 codes, misfire monitor incomplete.
    model.setStoredDtcs({"P0300", "P0301"});
    QCOMPARE(engine.handleLine("0101"), QByteArray("41 01 82 17 E5 00\r\r>"));
}

// silentEcus > 0 makes broadcast DTC reads answer like a real multi-ECU bus:
// one line per ECU, the silent ones reporting "no codes". This reproduces the
// response shape that once caused phantom-code parsing on a real vehicle.
void TestEmulatorEngine::multiEcuDtcLines()
{
    ObdMessageModel model;
    Elm327EmulatorEngine engine(&model);
    model.setSilentEcus(2);

    // No codes anywhere: three ECUs each answer "43 00".
    QCOMPARE(engine.handleLine("03"), QByteArray("43 00\r43 00\r43 00\r\r>"));

    // Engine ECU reports a misfire; the silent ECUs still answer "no codes".
    model.setStoredDtcs({"P0301"});
    QCOMPARE(engine.handleLine("03"), QByteArray("43 01 03 01\r43 00\r43 00\r\r>"));
}

QTEST_MAIN(TestEmulatorEngine)
#include "test_emulator_engine.moc"
