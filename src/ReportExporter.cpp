#include "ReportExporter.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPdfWriter>
#include <QTextDocument>
#include <QTextStream>

namespace {

QString htmlEscape(const QString &s)
{
    QString out = s;
    out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return out;
}

QString buildHtml(const ReportData &data)
{
    const QString when = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString html;
    html += "<html><body>";
    html += "<h1>OBD-II Diagnostic Report</h1>";
    html += "<p><b>Generated:</b> " + htmlEscape(when) + "</p>";

    html += "<h2>Vehicle</h2><table cellpadding='4'>";
    html += "<tr><td><b>VIN</b></td><td>" + htmlEscape(data.vin.isEmpty() ? "(not read)" : data.vin) + "</td></tr>";
    html += "<tr><td><b>Protocol</b></td><td>" + htmlEscape(data.protocol.isEmpty() ? "(unknown)" : data.protocol) + "</td></tr>";
    html += "<tr><td><b>Firmware</b></td><td>" + htmlEscape(data.firmware.isEmpty() ? "(unknown)" : data.firmware) + "</td></tr>";
    html += "<tr><td><b>Calibration IDs</b></td><td>" +
            htmlEscape(data.calibrationIds.isEmpty() ? "(not read)" : data.calibrationIds.join(", ")) + "</td></tr>";
    html += "</table>";

    html += "<h2>Diagnostic Trouble Codes</h2>";
    if (data.dtcs.isEmpty()) {
        html += "<p>No trouble codes recorded.</p>";
    } else {
        html += "<table border='1' cellpadding='4' cellspacing='0'>";
        html += "<tr><th>Code</th><th>Status</th><th>Description</th></tr>";
        for (const auto &d : data.dtcs)
            html += "<tr><td>" + htmlEscape(d.code) + "</td><td>" + htmlEscape(d.status) +
                    "</td><td>" + htmlEscape(d.description) + "</td></tr>";
        html += "</table>";
    }

    html += "<h2>Live Sensor Snapshot</h2>";
    if (data.pidSnapshot.isEmpty()) {
        html += "<p>No sensor data captured.</p>";
    } else {
        html += "<table border='1' cellpadding='4' cellspacing='0'>";
        html += "<tr><th>Parameter</th><th>Value</th><th>Unit</th></tr>";
        for (const auto &p : data.pidSnapshot)
            html += "<tr><td>" + htmlEscape(p.name) + "</td><td>" + htmlEscape(p.value) +
                    "</td><td>" + htmlEscape(p.unit) + "</td></tr>";
        html += "</table>";
    }

    html += "</body></html>";
    return html;
}

} // namespace

namespace ReportExporter {

bool exportJson(const ReportData &data, const QString &filePath, QString *error)
{
    QJsonObject root;
    root["generated"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonObject vehicle;
    vehicle["vin"] = data.vin;
    vehicle["protocol"] = data.protocol;
    vehicle["firmware"] = data.firmware;
    vehicle["calibrationIds"] = QJsonArray::fromStringList(data.calibrationIds);
    root["vehicle"] = vehicle;

    QJsonArray dtcs;
    for (const auto &d : data.dtcs) {
        QJsonObject o;
        o["code"] = d.code;
        o["status"] = d.status;
        o["description"] = d.description;
        dtcs.append(o);
    }
    root["dtcs"] = dtcs;

    QJsonArray pids;
    for (const auto &p : data.pidSnapshot) {
        QJsonObject o;
        o["name"] = p.name;
        o["value"] = p.value;
        o["unit"] = p.unit;
        pids.append(o);
    }
    root["pidSnapshot"] = pids;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool exportCsv(const ReportData &data, const QString &filePath, QString *error)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    ts << "Section,Field,Value\n";
    ts << "Report,Generated," << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    ts << "Vehicle,VIN," << data.vin << "\n";
    ts << "Vehicle,Protocol," << data.protocol << "\n";
    ts << "Vehicle,Firmware," << data.firmware << "\n";
    ts << "Vehicle,CalibrationIDs," << data.calibrationIds.join(' ') << "\n";
    for (const auto &d : data.dtcs)
        ts << "DTC," << d.code << ",\"" << d.status << ": " << d.description << "\"\n";
    for (const auto &p : data.pidSnapshot)
        ts << "PID,\"" << p.name << "\",\"" << p.value << ' ' << p.unit << "\"\n";
    f.close();
    return true;
}

bool exportPdf(const ReportData &data, const QString &filePath, QString *error)
{
    QPdfWriter writer(filePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300);

    QTextDocument doc;
    doc.setHtml(buildHtml(data));
    // Match the document width to the writer's page so text wraps sensibly.
    doc.setPageSize(writer.pageLayout().paintRectPixels(writer.resolution()).size());
    doc.print(&writer);

    // QPdfWriter has no error signal; verify the file was produced.
    if (!QFile::exists(filePath)) {
        if (error) *error = "PDF file was not created.";
        return false;
    }
    return true;
}

} // namespace ReportExporter
