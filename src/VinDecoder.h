#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

// Result of decoding a single VIN. `valid` reflects only whether the VIN is
// structurally well-formed (17 chars, legal alphabet) - a structurally valid
// VIN can still fail to decode (e.g. offline with an unrecognized WMI).
struct VinDecodeResult
{
    QString vin;
    bool valid = false;
    bool fromOnline = false; // true if make/model/trim came from the NHTSA vPIC API

    QString make;
    QString model;
    QString modelYear;
    QString trim;
    QString series;
    QString bodyClass;
    QString engine;
    QString manufacturer;
    QString plantCountry;

    QString error; // human-readable note (invalid VIN, or "offline decode only")
};

// Decodes a 17-character VIN into make/model/year/trim.
//
// Strategy (hybrid): query NHTSA's free vPIC "DecodeVinValues" API, which
// covers essentially every VIN sold in North America and returns make,
// model, trim, series, body class, engine, manufacturer, and plant country.
// If the request fails or times out (no internet, NHTSA unreachable), fall
// back to a purely local decode: model year from VIN position 10 and
// manufacturer from a bundled WMI (World Manufacturer Identifier) table.
// The offline fallback cannot determine model or trim - those require
// NHTSA's database - so those fields are left blank with `error` explaining
// why.
class VinDecoder : public QObject
{
    Q_OBJECT

public:
    explicit VinDecoder(QObject *parent = nullptr);

    void decode(const QString &vin);
    bool busy() const { return m_reply != nullptr; }

    // Pure, offline decode helpers - exposed (rather than private) so they can
    // be unit-tested without a network round-trip. Used internally as the
    // fallback path when the NHTSA lookup fails or times out.
    static bool isStructurallyValid(const QString &vin);
    static QString modelYearFromVin(const QString &vin);
    static QString manufacturerFromWmi(const QString &vin);
    static QString countryFromVin(const QString &vin);

signals:
    void decoded(const VinDecodeResult &result);

private slots:
    void onReplyFinished();
    void onTimeout();

private:
    void finishWithLocalFallback(const QString &reason);

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QTimer m_timeout;
    QString m_pendingVin;
};
