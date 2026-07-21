#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// Editable data model for the in-app ELM327 emulator. Adopts the Python
// ELM327-emulator "ObdMessage" format: named scenarios, each a table of
// request(regex)->response entries. Grown test-by-test (see tasks A2/A5); for now
// it holds the per-PID raw response bytes the engine emits for Mode 01.
class ObdMessageModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ObdMessageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Current raw response bytes for a Mode 01 PID (what "41 <pid> ..." carries).
    void setPidRaw(quint8 pid, const QByteArray &raw);
    QByteArray pidRaw(quint8 pid) const;

    // Diagnostic trouble codes by category (Mode 03 / 07 / 0A), as "P0301" strings.
    void setStoredDtcs(const QStringList &codes);
    void setPendingDtcs(const QStringList &codes);
    void setPermanentDtcs(const QStringList &codes);
    QStringList storedDtcs() const { return m_stored; }
    QStringList pendingDtcs() const { return m_pending; }
    QStringList permanentDtcs() const { return m_permanent; }

    // Mode 04: clear stored + pending DTCs (permanent DTCs persist, as on a car).
    void clearDtcs();

    // Number of additional "silent" ECUs that answer broadcast DTC reads with
    // a no-codes line, as every ECU does on a real vehicle.
    void setSilentEcus(int count) { m_silentEcus = count; }
    int silentEcus() const { return m_silentEcus; }

    // Mode 09 vehicle information.
    void setVin(const QString &vin);
    QString vin() const { return m_vin; }
    void setCalIds(const QStringList &ids);
    QStringList calIds() const { return m_calIds; }

    // Scenario presets (healthy / misfire / ...), adopting the Python emulator's
    // idea of named vehicle states. A scenario carries engineering-unit PID values
    // (encoded to raw bytes on apply), DTC lists, and VIN.
    struct Scenario
    {
        QString name;
        QString vin;
        QStringList calIds;
        QHash<quint8, double> pids; // PID -> engineering value
        QStringList storedDtcs;
        QStringList pendingDtcs;
        QStringList permanentDtcs;
        int silentEcus = 0;
    };

    bool loadBundled();                              // from :/emulator/obd_message.json
    bool loadScenariosJson(const QByteArray &json);  // parse + replace scenarios
    QStringList scenarioNames() const;
    QString activeScenario() const { return m_activeScenario; }
    void setActiveScenario(const QString &name);     // applies it to the model

private:
    int indexOfScenario(const QString &name) const;

    QHash<quint8, QByteArray> m_pidRaw;
    int m_silentEcus = 0;
    QStringList m_stored;
    QStringList m_pending;
    QStringList m_permanent;
    QString m_vin = QStringLiteral("1HGBH41JXMN109186");
    QStringList m_calIds = {QStringLiteral("OBDSUITEECU01")};
    QVector<Scenario> m_scenarios;
    QString m_activeScenario;
};
