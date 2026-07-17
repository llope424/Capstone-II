#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// A recorded diagnostic session attached to a vehicle profile.
struct DiagnosticRecord
{
    QString timestamp;    // ISO-8601 when the session was saved
    QStringList dtcs;     // trouble codes present at that time
    QString notes;        // free-form summary (e.g., PID snapshot text)
};

// A saved vehicle and its diagnostic history.
struct VehicleProfile
{
    QString name;         // user label, e.g. "My Camry"
    QString vin;
    QString make;
    QString model;
    QString year;
    QString trim;
    QString notes;
    QVector<DiagnosticRecord> history;
};

// Persists vehicle profiles + history to a JSON file in the app data directory,
// covering the SDD user-management requirements: create vehicle profiles, store
// diagnostic history, associate reports with vehicles, retrieve past sessions.
class VehicleStore : public QObject
{
    Q_OBJECT

public:
    explicit VehicleStore(QObject *parent = nullptr);

    const QVector<VehicleProfile> &vehicles() const { return m_vehicles; }
    int count() const { return m_vehicles.size(); }
    const VehicleProfile &at(int index) const { return m_vehicles.at(index); }

    int addVehicle(const VehicleProfile &v); // returns new index
    void updateVehicle(int index, const VehicleProfile &v);
    void removeVehicle(int index);
    void addRecord(int index, const DiagnosticRecord &record);

    bool load();
    bool save() const;

    QString storagePath() const { return m_path; }

private:
    QVector<VehicleProfile> m_vehicles;
    QString m_path;
};
