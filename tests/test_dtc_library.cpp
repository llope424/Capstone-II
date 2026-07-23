#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "DtcLibrary.h"

class TestDtcLibrary : public QObject
{
    Q_OBJECT

private slots:
    void loadsBundledGeneric();
    void describesKnownCode();
    void genericUnknownFallback();
    void manufacturerCodeFallback();
    void caseAndWhitespaceInsensitive();
    void severityNameRoundTrip();
    void severityOrdering();
    void severityBuckets();
    void manufacturerTablePreferred();
    void allDatasetCodesWellFormed();
};

void TestDtcLibrary::loadsBundledGeneric()
{
    QVERIFY2(DtcLibrary::instance().loaded(), "bundled generic.json must load");
}

void TestDtcLibrary::describesKnownCode()
{
    const DtcInfo i = DtcLibrary::instance().describe("P0301");
    QCOMPARE(i.description, QString("Cylinder 1 Misfire Detected"));
    QCOMPARE(i.severity, DtcSeverity::High);
    QVERIFY(!i.isManufacturer);
}

// A generic code (2nd char 0/2) not in the table -> structural fallback, flagged
// generic, categorised by the first letter.
void TestDtcLibrary::genericUnknownFallback()
{
    const DtcInfo i = DtcLibrary::instance().describe("P0999");
    QVERIFY(!i.description.isEmpty());
    QVERIFY(!i.isManufacturer);
    QVERIFY2(i.description.contains("Powertrain"), qPrintable(i.description));
}

// A manufacturer-specific code (2nd char 1/3) not in the table -> fallback flagged
// manufacturer.
void TestDtcLibrary::manufacturerCodeFallback()
{
    const DtcInfo i = DtcLibrary::instance().describe("P1234");
    QVERIFY(i.isManufacturer);
    QVERIFY2(i.description.contains("manufacturer", Qt::CaseInsensitive),
             qPrintable(i.description));
}

void TestDtcLibrary::caseAndWhitespaceInsensitive()
{
    const DtcInfo i = DtcLibrary::instance().describe("  p0301 ");
    QCOMPARE(i.description, QString("Cylinder 1 Misfire Detected"));
}

void TestDtcLibrary::severityNameRoundTrip()
{
    QCOMPARE(DtcLibrary::severityName(DtcSeverity::Critical), QString("Critical"));
    QCOMPARE(DtcLibrary::severityFromName("Critical"), DtcSeverity::Critical);
    QCOMPARE(DtcLibrary::severityFromName("nonsense"), DtcSeverity::Medium);
}

void TestDtcLibrary::severityOrdering()
{
    QVERIFY(DtcSeverity::Info < DtcSeverity::Low);
    QVERIFY(DtcSeverity::High < DtcSeverity::Critical);
}

// Locks the severity assigned to a representative code in each bucket, so the
// severity rules baked into the dataset can't silently drift.
void TestDtcLibrary::severityBuckets()
{
    const DtcLibrary &lib = DtcLibrary::instance();
    QCOMPARE(lib.describe("P0217").severity, DtcSeverity::Critical); // engine over-temp
    QCOMPARE(lib.describe("P0301").severity, DtcSeverity::High);     // misfire
    QCOMPARE(lib.describe("U0100").severity, DtcSeverity::High);     // lost comms w/ ECM
    QCOMPARE(lib.describe("P0100").severity, DtcSeverity::Medium);   // MAF circuit
    QCOMPARE(lib.describe("P0442").severity, DtcSeverity::Low);      // small EVAP leak
    QCOMPARE(lib.describe("P0457").severity, DtcSeverity::Info);     // loose fuel cap
}

// With a make, a manufacturer-specific code resolves to its real description;
// without one it falls back (generic table has no P1xxx). Make is case-insensitive.
void TestDtcLibrary::manufacturerTablePreferred()
{
    const DtcLibrary &lib = DtcLibrary::instance();

    const DtcInfo t = lib.describe("P1349", "Toyota");
    QVERIFY(t.isManufacturer);
    QCOMPARE(t.description, QString("Variable Valve Timing System Malfunction (Bank 1)"));

    const DtcInfo noMake = lib.describe("P1349");
    QVERIFY(noMake.isManufacturer);                                  // P1 => manufacturer
    QVERIFY(noMake.description.contains("no description on file"));  // structural fallback

    QCOMPARE(lib.describe("P1349", "toyota").description, t.description); // case-insensitive
    QCOMPARE(lib.describe("P1259", "Honda").description,
             QString("VTEC System Malfunction (Bank 1)"));

    // A make we don't have a table for -> structural fallback, not a crash.
    QVERIFY(lib.describe("P1349", "DeLorean").description.contains("no description on file"));
}

// Every bundled code (generic + manufacturer) must be a well-formed DTC:
// [P/C/B/U] then a 0-3 digit then three hex digits. A malformed code would encode
// to 00 00 and be silently dropped by the app (the 152-vs-146 stored-codes bug).
void TestDtcLibrary::allDatasetCodesWellFormed()
{
    static const QRegularExpression valid(QStringLiteral("^[PCBU][0-3][0-9A-F]{3}$"));

    QStringList files{QStringLiteral(":/dtc/generic.json")};
    const QDir mfr(QStringLiteral(":/dtc/manufacturer"));
    for (const QString &fn : mfr.entryList({QStringLiteral("*.json")}, QDir::Files))
        files << mfr.filePath(fn);

    int checked = 0;
    for (const QString &path : files) {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(path));
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.key().startsWith(QLatin1Char('_')))
                continue;
            QVERIFY2(valid.match(it.key()).hasMatch(),
                     qPrintable(QString("%1 in %2").arg(it.key(), path)));
            ++checked;
        }
    }
    QVERIFY(checked > 150); // sanity: we actually walked the dataset
}

QTEST_MAIN(TestDtcLibrary)
#include "test_dtc_library.moc"
