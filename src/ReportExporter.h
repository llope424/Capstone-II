#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// A snapshot of everything that goes into a diagnostic report.
struct ReportData
{
    QString vin;
    QString protocol;
    QStringList calibrationIds;
    QString firmware;

    struct Dtc
    {
        QString code;
        QString status;
        QString description;
    };
    QVector<Dtc> dtcs;

    struct PidReading
    {
        QString name;
        QString value;
        QString unit;
    };
    QVector<PidReading> pidSnapshot;
};

// Writes a ReportData snapshot to disk in JSON, CSV, or PDF. PDF is produced via
// QTextDocument + QPdfWriter, so no extra Qt module is required.
namespace ReportExporter {
bool exportJson(const ReportData &data, const QString &filePath, QString *error = nullptr);
bool exportCsv(const ReportData &data, const QString &filePath, QString *error = nullptr);
bool exportPdf(const ReportData &data, const QString &filePath, QString *error = nullptr);
} // namespace ReportExporter
