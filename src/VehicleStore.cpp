#include "VehicleStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

VehicleStore::VehicleStore(QObject *parent) : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_path = dir + "/vehicles.json";
    load();
}

int VehicleStore::addVehicle(const VehicleProfile &v)
{
    m_vehicles.append(v);
    save();
    return m_vehicles.size() - 1;
}

void VehicleStore::updateVehicle(int index, const VehicleProfile &v)
{
    if (index < 0 || index >= m_vehicles.size())
        return;
    // Preserve existing history unless the caller supplied its own.
    VehicleProfile updated = v;
    if (updated.history.isEmpty())
        updated.history = m_vehicles[index].history;
    m_vehicles[index] = updated;
    save();
}

void VehicleStore::removeVehicle(int index)
{
    if (index < 0 || index >= m_vehicles.size())
        return;
    m_vehicles.remove(index);
    save();
}

void VehicleStore::addRecord(int index, const DiagnosticRecord &record)
{
    if (index < 0 || index >= m_vehicles.size())
        return;
    m_vehicles[index].history.append(record);
    save();
}

bool VehicleStore::load()
{
    m_vehicles.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false; // no store yet - not an error

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray())
        return false;

    for (const QJsonValue &vv : doc.array()) {
        const QJsonObject o = vv.toObject();
        VehicleProfile v;
        v.name = o["name"].toString();
        v.vin = o["vin"].toString();
        v.make = o["make"].toString();
        v.model = o["model"].toString();
        v.year = o["year"].toString();
        v.trim = o["trim"].toString();
        v.notes = o["notes"].toString();
        for (const QJsonValue &rv : o["history"].toArray()) {
            const QJsonObject ro = rv.toObject();
            DiagnosticRecord r;
            r.timestamp = ro["timestamp"].toString();
            r.notes = ro["notes"].toString();
            for (const QJsonValue &c : ro["dtcs"].toArray())
                r.dtcs << c.toString();
            v.history.append(r);
        }
        m_vehicles.append(v);
    }
    return true;
}

bool VehicleStore::save() const
{
    QJsonArray arr;
    for (const VehicleProfile &v : m_vehicles) {
        QJsonObject o;
        o["name"] = v.name;
        o["vin"] = v.vin;
        o["make"] = v.make;
        o["model"] = v.model;
        o["year"] = v.year;
        o["trim"] = v.trim;
        o["notes"] = v.notes;
        QJsonArray hist;
        for (const DiagnosticRecord &r : v.history) {
            QJsonObject ro;
            ro["timestamp"] = r.timestamp;
            ro["notes"] = r.notes;
            ro["dtcs"] = QJsonArray::fromStringList(r.dtcs);
            hist.append(ro);
        }
        o["history"] = hist;
        arr.append(o);
    }

    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}
