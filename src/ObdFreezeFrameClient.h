#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

#include "CanFrame.h"
#include "ObdPidMonitor.h" // PidDefinition / standardPids()

class GvretConnection;

// One-shot SAE J1979 Mode 02 (freeze frame) reader over a GVRET/CAN link:
// requests the trigger DTC (PID 02, frame 00) and then each standard PID in
// sequence, emitting decoded values as ECUs answer. PIDs that get no reply
// within the per-step timeout are reported as unavailable, mirroring how a
// vehicle omits parameters it did not capture.
class ObdFreezeFrameClient : public QObject
{
    Q_OBJECT

public:
    explicit ObdFreezeFrameClient(GvretConnection *connection, QObject *parent = nullptr);

    void read();

signals:
    void freezeFrameDtcReceived(const QString &code, bool present);
    void freezeFramePidReceived(quint8 pid, double value, bool ok);
    void logMessage(const QString &text);

private slots:
    void onFrameReceived(const CanFrame &frame);
    void onTimeout();

private:
    quint8 currentPid() const; // 0x02 for the trigger step, else the PID
    void sendCurrent();
    void advance();

    GvretConnection *m_connection;
    QVector<PidDefinition> m_pids;
    QTimer m_timeout;
    int m_step = -1; // -1 idle; 0 = trigger DTC; 1..N = m_pids[step-1]
};
