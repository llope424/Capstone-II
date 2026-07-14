#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <functional>

#include "CanFrame.h"

class GvretConnection;

// One monitored OBD-II parameter: its Mode 01 PID, a display name, a unit, how
// many data bytes its response carries, and a decode function turning those raw
// bytes into an engineering value. Formulas follow the standard SAE J1979 PID
// definitions.
struct PidDefinition
{
    quint8 pid;
    QString name;
    QString unit;
    int dataBytes;
    std::function<double(const quint8 *)> decode;
};

// Polls a fixed set of OBD-II PIDs round-robin over a GvretConnection and emits
// decoded values as responses arrive. Requests go to the functional broadcast
// ID 0x7DF; responses are expected as ISO-TP single frames from 0x7E8-0x7EF.
class ObdPidMonitor : public QObject
{
    Q_OBJECT

public:
    explicit ObdPidMonitor(GvretConnection *connection, QObject *parent = nullptr);

    const QVector<PidDefinition> &definitions() const { return m_pids; }

    // The standard set of monitored PIDs (name, unit, decode formula). Shared so
    // other backends (e.g. the ELM327 adapter) reuse the same decode logic.
    static QVector<PidDefinition> standardPids();

    void start(int intervalMs = 60);
    void stop();
    bool isRunning() const;

signals:
    void pidUpdated(quint8 pid, double value);

private slots:
    void pollNext();
    void onFrameReceived(const CanFrame &frame);

private:
    GvretConnection *m_connection;
    QVector<PidDefinition> m_pids;
    QTimer m_pollTimer;
    int m_pollIndex = 0;
};
