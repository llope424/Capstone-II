#pragma once

#include <QHash>
#include <QString>

// Relative urgency of a trouble code, derived from its subsystem. Ordered from
// least to most severe so callers can compare / sort.
enum class DtcSeverity { Info, Low, Medium, High, Critical };

struct DtcInfo
{
    QString description;
    DtcSeverity severity = DtcSeverity::Medium;
    bool isManufacturer = false; // true for manufacturer-specific / fallback codes
};

// Plain-language descriptions + severity for OBD-II Diagnostic Trouble Codes.
// Generic (SAE J2012) codes load from a bundled JSON; manufacturer-specific codes
// load from per-make JSON and take precedence when a make is supplied. Codes not
// in any table fall back to a structural category derived from the code itself, so
// describe() always returns something sensible. Singleton (data is read-only).
class DtcLibrary
{
public:
    static DtcLibrary &instance();

    // Look up a code such as "P0301". If make is given (e.g. "Toyota"), a
    // manufacturer table for that make is consulted first.
    DtcInfo describe(const QString &code, const QString &make = QString()) const;

    bool loaded() const { return m_loaded; }

    static QString severityName(DtcSeverity s);            // "High", ...
    static DtcSeverity severityFromName(const QString &s); // inverse; Medium if unknown

private:
    DtcLibrary();

    void loadGeneric();
    void loadManufacturers();
    static DtcInfo structuralFallback(const QString &code);

    QHash<QString, DtcInfo> m_generic;                       // code -> info
    QHash<QString, QHash<QString, DtcInfo>> m_byMake;        // make(lower) -> code -> info
    bool m_loaded = false;
};
