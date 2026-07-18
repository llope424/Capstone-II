#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "CanFrame.h"
#include "ObdPidMonitor.h" // for PidDefinition / ObdPidMonitor::standardPids()

QT_BEGIN_NAMESPACE
class QSerialPort;
class QTcpSocket;
class QIODevice;
QT_END_NAMESPACE

// Talks to a commercial ELM327 OBD-II adapter over serial (USB / Bluetooth SPP)
// or TCP (WiFi ELM327). The ELM327 speaks a high-level ASCII "AT"-command
// protocol and handles the CAN/ISO-TP layer itself, so this class issues text
// commands and parses hex-ASCII replies, then emits the SAME UI signals as the
// GVRET stack (pidUpdated / dtcsReceived / vinReceived / ...). That lets the app
// use a proven commercial adapter as an alternative to the custom ESP32 scanner.
class Elm327Connection : public QObject
{
    Q_OBJECT

public:
    explicit Elm327Connection(QObject *parent = nullptr);
    ~Elm327Connection() override;

    void openSerial(const QString &portName);
    void openNetwork(const QString &host, quint16 port);
    void close();
    bool isOpen() const;

    // High-level OBD operations (mirrors the GVRET OBD stack's actions).
    void startMonitoring(int intervalMs = 250);
    void stopMonitoring();
    bool isMonitoring() const { return m_monitorTimer.isActive(); }

    void sendTestRequest();
    void queryPidSupport();
    void readStoredDtcs();
    void readPendingDtcs();
    void readPermanentDtcs();
    void clearDtcs();
    void readFreezeFrame(); // Mode 02: trigger DTC + captured PID snapshot
    void readVin();
    void readCalibrationIds();

signals:
    void connected();
    void disconnected(const QString &reason);
    void frameReceived(const CanFrame &frame); // synthesized, for the raw view
    void pidUpdated(quint8 pid, double value);
    void dtcsReceived(quint8 mode, const QStringList &codes);
    void dtcsCleared();
    void freezeFrameDtcReceived(const QString &code, bool present);
    void freezeFramePidReceived(quint8 pid, double value, bool ok);
    void vinReceived(const QString &vin);
    void calibrationIdsReceived(const QStringList &ids);
    void deviceInfoReceived(const QString &adapterId);
    void logMessage(const QString &text);

private slots:
    void onReadyRead();
    void onIoError();
    void onCommandTimeout();
    void pollNext();

private:
    // One queued ELM327 request and how to handle its reply.
    enum class Kind { Init, TestPid, Pid, Dtc, Vin, CalId, Clear, FreezeDtc, FreezePid };
    struct Command
    {
        Kind kind;
        QByteArray text; // ASCII, without trailing CR
        quint8 pid = 0;  // for Pid
        quint8 mode = 0; // for Dtc (0x03/0x07/0x0A)
    };

    void enqueue(const Command &cmd);
    void pumpQueue();
    void writeRaw(const QByteArray &bytes);
    void handleResponse(const Command &cmd, const QString &response);
    void beginSession();
    void finishConnect();

    static QVector<quint8> parseHexBytes(const QString &s);
    void synthesizeFrame(quint32 id, const QVector<quint8> &data);

    QSerialPort *m_serial = nullptr;
    QTcpSocket *m_tcp = nullptr;
    QIODevice *m_device = nullptr;

    QByteArray m_rxBuf;                 // accumulates until the '>' prompt
    QQueue<Command> m_queue;
    bool m_commandInFlight = false;
    Command m_current;
    QTimer m_commandTimeout;

    QTimer m_monitorTimer;             // round-robin PID polling
    QVector<PidDefinition> m_pids;
    int m_pollIndex = 0;

    bool m_connected = false;
    int m_initStep = 0;                // progress through the AT init sequence
    int m_baudAttempt = 0;             // serial auto-baud attempt index
    QElapsedTimer m_clock;             // timestamps synthesized frames, like the scanner clock
};
