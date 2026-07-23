#include "DtcLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

DtcLibrary::DtcLibrary()
{
    loadGeneric();
    loadManufacturers();
}

DtcLibrary &DtcLibrary::instance()
{
    static DtcLibrary lib;
    return lib;
}

DtcInfo DtcLibrary::describe(const QString &rawCode, const QString &make) const
{
    const QString code = rawCode.trimmed().toUpper();

    // Manufacturer table (if a make is supplied) wins over the generic table.
    if (!make.isEmpty()) {
        const auto mk = m_byMake.constFind(make.trimmed().toLower());
        if (mk != m_byMake.constEnd()) {
            const auto it = mk->constFind(code);
            if (it != mk->constEnd())
                return it.value();
        }
    }

    const auto it = m_generic.constFind(code);
    if (it != m_generic.constEnd())
        return it.value();

    return structuralFallback(code);
}

QString DtcLibrary::severityName(DtcSeverity s)
{
    switch (s) {
    case DtcSeverity::Info: return QStringLiteral("Info");
    case DtcSeverity::Low: return QStringLiteral("Low");
    case DtcSeverity::Medium: return QStringLiteral("Medium");
    case DtcSeverity::High: return QStringLiteral("High");
    case DtcSeverity::Critical: return QStringLiteral("Critical");
    }
    return QStringLiteral("Medium");
}

DtcSeverity DtcLibrary::severityFromName(const QString &s)
{
    const QString n = s.trimmed().toLower();
    if (n == QLatin1String("info")) return DtcSeverity::Info;
    if (n == QLatin1String("low")) return DtcSeverity::Low;
    if (n == QLatin1String("high")) return DtcSeverity::High;
    if (n == QLatin1String("critical")) return DtcSeverity::Critical;
    return DtcSeverity::Medium; // default / unknown
}

DtcInfo DtcLibrary::structuralFallback(const QString &code)
{
    DtcInfo info;
    info.severity = DtcSeverity::Medium;
    if (code.isEmpty()) {
        info.description = QStringLiteral("Unknown trouble code");
        return info;
    }

    QString category;
    switch (code.at(0).toLatin1()) {
    case 'P': category = QStringLiteral("Powertrain"); break;
    case 'C': category = QStringLiteral("Chassis"); break;
    case 'B': category = QStringLiteral("Body"); break;
    case 'U': category = QStringLiteral("Network/Communication"); break;
    default: category = QStringLiteral("Unknown"); break;
    }

    // SAE numbering: second character 1/3 = manufacturer-specific, 0/2 = generic.
    const bool mfr = code.size() > 1 && (code.at(1) == QChar('1') || code.at(1) == QChar('3'));
    info.isManufacturer = mfr;
    info.description = QStringLiteral("%1 %2 code (no description on file)")
                           .arg(category, mfr ? QStringLiteral("manufacturer-specific")
                                              : QStringLiteral("generic"));
    return info;
}

void DtcLibrary::loadGeneric()
{
    QFile f(QStringLiteral(":/dtc/generic.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.key().startsWith(QLatin1Char('_')))
            continue; // metadata like _comment
        const QJsonObject e = it.value().toObject();
        DtcInfo info;
        info.description = e.value(QStringLiteral("description")).toString();
        info.severity = severityFromName(e.value(QStringLiteral("severity")).toString());
        info.isManufacturer = false;
        m_generic.insert(it.key().toUpper(), info);
    }
    if (!m_generic.isEmpty())
        m_loaded = true;
}

void DtcLibrary::loadManufacturers()
{
    // Each resources/dtc/manufacturer/<make>.json becomes a table keyed by the
    // lower-cased make (the file's base name). Coverage is intentionally partial.
    QDir dir(QStringLiteral(":/dtc/manufacturer"));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &fn : files) {
        QFile f(dir.filePath(fn));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        const QString make = QFileInfo(fn).completeBaseName().toLower();

        QHash<QString, DtcInfo> table;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.key().startsWith(QLatin1Char('_')))
                continue;
            const QJsonObject e = it.value().toObject();
            DtcInfo info;
            info.description = e.value(QStringLiteral("description")).toString();
            info.severity = severityFromName(e.value(QStringLiteral("severity")).toString());
            info.isManufacturer = true;
            table.insert(it.key().toUpper(), info);
        }
        if (!table.isEmpty())
            m_byMake.insert(make, table);
    }
}
