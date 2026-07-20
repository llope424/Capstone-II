#include "VinDecoder.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr int kTimeoutMs = 6000;

// NHTSA vPIC free public API - no key required.
// https://vpic.nhtsa.dot.gov/api/
QString nhtsaDecodeUrl(const QString &vin)
{
    return QString("https://vpic.nhtsa.dot.gov/api/vehicles/DecodeVinValues/%1?format=json").arg(vin);
}

// VIN position-10 model-year code, per SAE J853 / ISO 3779. The 30-character
// sequence repeats every 30 years starting 1980, so each code maps to two
// candidate years 30 apart (e.g. 'A' -> 1980 or 2010).
const QString kYearCodeOrder = "ABCDEFGHJKLMNPRSTVWXY123456789";

// Disambiguates the 30-year cycle using VIN position 7 (index 6): by SAE/NHTSA
// convention, vehicles built for the 1980-2009 cycle have a numeric character
// there, while 2010+ vehicles have a letter. This is the standard heuristic
// used by VIN decoders; it matters here because many OBD-II vehicles on the
// road (mandate started 1996) predate 2010, and just picking "closest to
// today" would wrongly resolve all of them into the 2010+ cycle.
QString resolveModelYear(const QString &vin, QChar yearCode)
{
    const int idx = kYearCodeOrder.indexOf(yearCode.toUpper());
    if (idx < 0)
        return QString();

    const int cycle1980 = 1980 + idx;
    const int cycle2010 = 2010 + idx;

    if (vin.size() >= 7) {
        const QChar pos7 = vin.at(6).toUpper();
        if (pos7.isDigit())
            return QString::number(cycle1980);
        if (pos7.isLetter())
            return QString::number(cycle2010);
    }

    // No usable position-7 character: fall back to whichever candidate isn't
    // in the future.
    const int ceiling = QDate::currentDate().year() + 1;
    return QString::number(cycle2010 <= ceiling ? cycle2010 : cycle1980);
}

// Compact WMI (World Manufacturer Identifier, VIN positions 1-3) table
// covering common North American, Japanese, Korean, and European makes.
// Not exhaustive - offline decode is a fallback, not a replacement for the
// NHTSA database.
const QHash<QString, QString> &wmiTable()
{
    static const QHash<QString, QString> table = {
        // Detroit Three
        {"1FA", "Ford"}, {"1FB", "Ford"}, {"1FC", "Ford"}, {"1FD", "Ford"},
        {"1FM", "Ford"}, {"1FT", "Ford"}, {"2FA", "Ford"}, {"2FM", "Ford"},
        {"2FT", "Ford"}, {"3FA", "Ford"}, {"WF0", "Ford"},
        {"1G1", "Chevrolet"}, {"1G4", "Buick"}, {"1G6", "Cadillac"},
        {"1GC", "Chevrolet"}, {"1GT", "GMC"}, {"2G1", "Chevrolet"},
        {"2G4", "Buick"}, {"3GN", "Chevrolet"}, {"3GY", "GMC"}, {"KL1", "Chevrolet"},
        {"1C3", "Chrysler"}, {"1C4", "Chrysler"}, {"1C6", "Ram"},
        {"1D3", "Dodge"}, {"1D4", "Dodge"}, {"1D7", "Dodge"}, {"1D8", "Dodge"},
        {"1J4", "Jeep"}, {"1J8", "Jeep"}, {"2C3", "Chrysler"}, {"2C4", "Chrysler"},
        {"3C3", "Chrysler"}, {"3C4", "Chrysler"}, {"3C6", "Ram"},
        // Japanese
        {"1HG", "Honda"}, {"1HT", "Honda"}, {"2HG", "Honda"}, {"2HK", "Honda"},
        {"2HM", "Honda"}, {"3HG", "Honda"}, {"5J6", "Honda"}, {"5J8", "Acura"},
        {"JHM", "Honda"}, {"JH4", "Acura"},
        {"1N4", "Nissan"}, {"1N6", "Nissan"}, {"3N1", "Nissan"}, {"3N6", "Nissan"},
        {"5N1", "Nissan"}, {"5N3", "Infiniti"}, {"JN1", "Nissan"}, {"JN6", "Nissan"}, {"JN8", "Nissan"},
        {"4T1", "Toyota"}, {"4T3", "Toyota"}, {"5TD", "Toyota"}, {"5TE", "Toyota"},
        {"5TF", "Toyota"}, {"2T1", "Toyota"}, {"2T2", "Toyota"},
        {"JT2", "Toyota"}, {"JT3", "Toyota"}, {"JT4", "Toyota"}, {"JT6", "Lexus"}, {"JT8", "Lexus"},
        {"JTD", "Toyota"}, {"JTE", "Toyota"}, {"JTH", "Lexus"},
        {"1YV", "Mazda"}, {"4F2", "Mazda"}, {"4F4", "Mazda"}, {"JM1", "Mazda"}, {"JM3", "Mazda"},
        {"4S3", "Subaru"}, {"4S4", "Subaru"}, {"JF1", "Subaru"}, {"JF2", "Subaru"},
        {"JA3", "Mitsubishi"}, {"JA4", "Mitsubishi"}, {"JS2", "Suzuki"}, {"JS3", "Suzuki"},
        // Korean
        {"KM8", "Hyundai"}, {"KMH", "Hyundai"}, {"5NP", "Hyundai"}, {"5NM", "Hyundai"},
        {"KNA", "Kia"}, {"KND", "Kia"}, {"KNJ", "Kia"}, {"KNM", "Kia"}, {"KNC", "Kia"},
        // German
        {"WBA", "BMW"}, {"WBS", "BMW"}, {"WBX", "BMW"}, {"WBY", "BMW"}, {"5UX", "BMW"}, {"5UM", "BMW"},
        {"WDB", "Mercedes-Benz"}, {"WDC", "Mercedes-Benz"}, {"WDD", "Mercedes-Benz"}, {"WDF", "Mercedes-Benz"}, {"4JG", "Mercedes-Benz"},
        {"WVW", "Volkswagen"}, {"WV1", "Volkswagen"}, {"WV2", "Volkswagen"}, {"1VW", "Volkswagen"}, {"3VW", "Volkswagen"},
        {"WAU", "Audi"}, {"WA1", "Audi"}, {"TRU", "Audi"},
        {"WP0", "Porsche"}, {"WP1", "Porsche"},
        {"WMW", "Mini"},
        // Other
        {"YV1", "Volvo"}, {"YV4", "Volvo"},
        {"5YJ", "Tesla"}, {"7SA", "Tesla"},
        {"SAJ", "Jaguar"}, {"SAL", "Land Rover"},
        {"VF1", "Renault"}, {"VF3", "Peugeot"}, {"VF7", "Citroen"},
        {"ZFA", "Fiat"}, {"ZAR", "Alfa Romeo"}, {"ZFF", "Ferrari"}, {"ZHW", "Lamborghini"}, {"ZAM", "Maserati"},
    };
    return table;
}

QString countryFromWmiFirstChar(QChar c)
{
    switch (c.toUpper().toLatin1()) {
    case '1': case '4': case '5': return "United States";
    case '2': return "Canada";
    case '3': return "Mexico";
    case 'J': return "Japan";
    case 'K': return "South Korea";
    case 'S': return "United Kingdom";
    case 'V': return "France/Spain";
    case 'W': return "Germany";
    case 'Y': return "Sweden/Finland";
    case 'Z': return "Italy";
    default: return QString();
    }
}
} // namespace

VinDecoder::VinDecoder(QObject *parent) : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &VinDecoder::onTimeout);
}

bool VinDecoder::isStructurallyValid(const QString &vin)
{
    if (vin.size() != 17)
        return false;
    for (const QChar &c : vin) {
        const QChar u = c.toUpper();
        if (u == 'I' || u == 'O' || u == 'Q')
            return false;
        if (!u.isDigit() && !(u.isLetter() && u.isUpper()))
            return false;
    }
    return true;
}

QString VinDecoder::modelYearFromVin(const QString &vin)
{
    if (vin.size() < 10)
        return QString();
    return resolveModelYear(vin, vin.at(9));
}

QString VinDecoder::manufacturerFromWmi(const QString &vin)
{
    if (vin.size() < 3)
        return QString();
    return wmiTable().value(vin.left(3).toUpper());
}

QString VinDecoder::countryFromVin(const QString &vin)
{
    if (vin.isEmpty())
        return QString();
    return countryFromWmiFirstChar(vin.at(0));
}

void VinDecoder::decode(const QString &vin)
{
    const QString normalized = vin.trimmed().toUpper();

    if (m_reply) {
        // A new request supersedes any in-flight one. Disconnect first so an
        // abort() that completes synchronously can't re-enter onReplyFinished()
        // with stale state.
        disconnect(m_reply, &QNetworkReply::finished, this, &VinDecoder::onReplyFinished);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_timeout.stop();

    if (!isStructurallyValid(normalized)) {
        VinDecodeResult result;
        result.vin = normalized;
        result.valid = false;
        result.error = QString("'%1' is not a valid 17-character VIN.").arg(normalized);
        emit decoded(result);
        return;
    }

    m_pendingVin = normalized;
    const QUrl url(nhtsaDecodeUrl(normalized));
    QNetworkRequest request(url);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &VinDecoder::onReplyFinished);
    m_timeout.start(kTimeoutMs);
}

void VinDecoder::onTimeout()
{
    if (!m_reply)
        return;
    m_reply->abort(); // triggers onReplyFinished() with an error, which falls back
}

void VinDecoder::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    m_timeout.stop();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        finishWithLocalFallback(reply->errorString());
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonArray results = doc.object().value("Results").toArray();
    if (results.isEmpty()) {
        finishWithLocalFallback("NHTSA returned no results.");
        return;
    }

    const QJsonObject o = results.first().toObject();
    const QString make = o.value("Make").toString().trimmed();
    if (make.isEmpty()) {
        finishWithLocalFallback("NHTSA could not decode this VIN.");
        return;
    }

    VinDecodeResult result;
    result.vin = m_pendingVin;
    result.valid = true;
    result.fromOnline = true;
    result.make = make;
    result.model = o.value("Model").toString().trimmed();
    result.modelYear = o.value("ModelYear").toString().trimmed();
    result.trim = o.value("Trim").toString().trimmed();
    result.series = o.value("Series").toString().trimmed();
    result.bodyClass = o.value("BodyClass").toString().trimmed();
    result.manufacturer = o.value("Manufacturer").toString().trimmed();
    result.plantCountry = o.value("PlantCountry").toString().trimmed();

    const QString cylinders = o.value("EngineCylinders").toString().trimmed();
    // NHTSA reports unrounded displacement ("2.998832712"); show one decimal.
    QString displacement = o.value("DisplacementL").toString().trimmed();
    bool numeric = false;
    const double liters = displacement.toDouble(&numeric);
    if (numeric)
        displacement = QString::number(liters, 'f', 1);
    if (!displacement.isEmpty() && !cylinders.isEmpty())
        result.engine = QString("%1L, %2-cyl").arg(displacement, cylinders);
    else if (!displacement.isEmpty())
        result.engine = displacement + "L";

    emit decoded(result);
}

void VinDecoder::finishWithLocalFallback(const QString &reason)
{
    VinDecodeResult result;
    result.vin = m_pendingVin;
    result.valid = true;
    result.fromOnline = false;
    result.modelYear = modelYearFromVin(m_pendingVin);
    result.make = manufacturerFromWmi(m_pendingVin);
    result.plantCountry = countryFromVin(m_pendingVin);
    result.error = QString("Offline decode only (%1). Model and trim require an internet "
                            "connection to NHTSA.")
                       .arg(reason);
    emit decoded(result);
}
