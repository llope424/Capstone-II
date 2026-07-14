#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "CanFrame.h"

class GvretConnection;

// Reads and clears OBD-II Diagnostic Trouble Codes over ISO 15765-4 (CAN).
// Handles the three DTC read services and the clear service:
//   0x03  stored (confirmed) DTCs
//   0x07  pending DTCs
//   0x0A  permanent DTCs
//   0x04  clear DTCs / reset MIL
// DTC responses frequently exceed a single CAN frame, so this implements the
// ISO-TP transport (single frame, plus first-frame/consecutive-frame
// reassembly with a flow-control frame sent back to the responding ECU).
class ObdDtcClient : public QObject
{
    Q_OBJECT

public:
    explicit ObdDtcClient(GvretConnection *connection, QObject *parent = nullptr);

    void readStoredDtcs();
    void readPendingDtcs();
    void readPermanentDtcs();
    void clearDtcs();

    bool busy() const { return m_activeMode != 0; }

    // Turns two raw DTC bytes into a code string such as "P0301".
    static QString decodeDtc(quint8 a, quint8 b);
    // Human-readable description for a decoded code string.
    static QString describeDtc(const QString &code);

signals:
    // mode is the request service (0x03/0x07/0x0A) these codes came from.
    void dtcsReceived(quint8 mode, const QStringList &codes);
    void dtcsCleared();
    void logMessage(const QString &text);

private slots:
    void onFrameReceived(const CanFrame &frame);
    void onTimeout();

private:
    void beginTransaction(quint8 serviceMode);
    void sendServiceRequest(quint8 serviceMode);
    void sendFlowControl(quint32 responderId);
    void handleAssembledResponse(const QByteArray &payload);
    void resetTransaction();

    GvretConnection *m_connection;
    QTimer m_timeout;

    quint8 m_activeMode = 0;        // 0 = idle; otherwise the pending request service
    quint32 m_responderId = 0;      // locked to the first ECU that answers
    QByteArray m_rxBuffer;          // reassembled ISO-TP payload
    int m_rxRemaining = 0;          // bytes still expected in a multi-frame transfer
    quint8 m_expectedSeq = 0;       // next consecutive-frame sequence number
};
