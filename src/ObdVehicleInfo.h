#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "CanFrame.h"

class GvretConnection;

// Reads vehicle identification data via OBD-II Mode 09 (Request Vehicle
// Information) over ISO 15765-4: VIN (PID 02) and Calibration IDs (PID 04).
// These responses span multiple CAN frames, so this implements ISO-TP
// single-frame plus first-frame/consecutive-frame reassembly with flow control,
// mirroring the DTC client.
class ObdVehicleInfo : public QObject
{
    Q_OBJECT

public:
    explicit ObdVehicleInfo(GvretConnection *connection, QObject *parent = nullptr);

    void readVin();            // Mode 09 PID 02
    void readCalibrationIds(); // Mode 09 PID 04

    bool busy() const { return m_activePid != 0; }

signals:
    void vinReceived(const QString &vin);
    void calibrationIdsReceived(const QStringList &ids);
    void logMessage(const QString &text);

private slots:
    void onFrameReceived(const CanFrame &frame);
    void onTimeout();

private:
    void beginRequest(quint8 pid);
    void sendFlowControl(quint32 responderId);
    void handleAssembledResponse(const QByteArray &payload);
    void resetTransaction();

    GvretConnection *m_connection;
    QTimer m_timeout;

    quint8 m_activePid = 0; // 0 = idle
    quint32 m_responderId = 0;
    QByteArray m_rxBuffer;
    int m_rxRemaining = 0;
    quint8 m_expectedSeq = 1;
};
