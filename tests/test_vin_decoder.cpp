#include <QtTest>

#include "VinDecoder.h"

// Covers only the offline decode helpers (VIN structure check, model-year
// code, WMI manufacturer lookup). The NHTSA online path isn't exercised here
// since it requires a live network round-trip.
class TestVinDecoder : public QObject
{
    Q_OBJECT

private slots:
    void validatesStructure();
    void decodesPre2010Cycle();
    void decodes2010PlusCycle();
    void unknownWmiYieldsEmptyMake();
    void countryFromWmi();
};

void TestVinDecoder::validatesStructure()
{
    QVERIFY(VinDecoder::isStructurallyValid("1HGBH41JXMN109186"));
    QVERIFY(!VinDecoder::isStructurallyValid("1HGBH41JXMN10918")); // 16 chars
    QVERIFY(!VinDecoder::isStructurallyValid("1HGBH41JXMN1091866")); // 18 chars
    QVERIFY(!VinDecoder::isStructurallyValid("1HGBH41IXMN109186")); // contains 'I'
    QVERIFY(!VinDecoder::isStructurallyValid("1HGBH41OXMN109186")); // contains 'O'
    QVERIFY(!VinDecoder::isStructurallyValid("1HGBH41QXMN109186")); // contains 'Q'
}

void TestVinDecoder::decodesPre2010Cycle()
{
    // Well-known sample VIN: 1991 Honda Accord. Position 7 ('1') is numeric,
    // which resolves the 30-year year-code ambiguity to the 1980-2009 cycle.
    const QString vin = "1HGBH41JXMN109186";
    QCOMPARE(VinDecoder::modelYearFromVin(vin), QString("1991"));
    QCOMPARE(VinDecoder::manufacturerFromWmi(vin), QString("Honda"));
}

void TestVinDecoder::decodes2010PlusCycle()
{
    // 2015 Tesla Model S. Position 7 ('E') is a letter, resolving to the
    // 2010-2039 cycle.
    const QString vin = "5YJSA1E14FF086302";
    QCOMPARE(VinDecoder::modelYearFromVin(vin), QString("2015"));
    QCOMPARE(VinDecoder::manufacturerFromWmi(vin), QString("Tesla"));
}

void TestVinDecoder::unknownWmiYieldsEmptyMake()
{
    QVERIFY(VinDecoder::manufacturerFromWmi("ZZZ99999999999999").isEmpty());
}

void TestVinDecoder::countryFromWmi()
{
    QCOMPARE(VinDecoder::countryFromVin("1HGBH41JXMN109186"), QString("United States"));
    QCOMPARE(VinDecoder::countryFromVin("JHMBH41JXMN109186"), QString("Japan"));
    QCOMPARE(VinDecoder::countryFromVin("WBABH41JXMN109186"), QString("Germany"));
}

QTEST_APPLESS_MAIN(TestVinDecoder)
#include "test_vin_decoder.moc"
