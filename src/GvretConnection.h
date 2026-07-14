#pragma once

#include <QObject>
#include <QSerialPort>
#include <QTcpSocket>
#include <QTimer>

#include "CanFrame.h"

// Speaks the GVRET binary protocol (as used by SavvyCAN and GVRET-compatible
// firmware such as ESP32RET) over either a QSerialPort or a QTcpSocket. Byte
// layout and the TCP transport (host:23) were taken directly from SavvyCAN's
// connections/gvretserial.cpp (procRXChar / piSendFrame / connectDevice), not
// reconstructed from memory, so it should match any firmware that speaks
// standard GVRET.
class GvretConnection : public QObject
{
    Q_OBJECT

public:
    explicit GvretConnection(QObject *parent = nullptr);
    ~GvretConnection() override;

    // If espMode is true, no flow control and DTR/RTS held low (ESP32 boards use
    // DTR/RTS for bootloader entry/reset, so raising them resets the board).
    // If false, hardware flow control is used (SAM/Due-style GVRET boards).
    void openSerial(const QString &portName, bool espMode = true);

    // Connects to a GVRET device speaking the protocol over WiFi/network, e.g.
    // an ESP32 board exposing GVRET on a TCP socket. Port 23 matches the port
    // SavvyCAN itself uses for TCP GVRET devices.
    void openNetwork(const QString &host, quint16 port = 23);

    void close();
    bool isOpen() const;

    // Queues a CAN frame for transmission to the scanner on the given bus (0 or 1).
    bool sendFrame(quint32 id, bool extended, quint8 bus, const QByteArray &payload);

signals:
    void connected();
    void disconnected(const QString &reason);
    void frameReceived(const CanFrame &frame);
    void deviceInfoReceived(int buildNumber, int singleWireMode);
    void busParamsReceived(int bus0Baud, bool bus0Enabled, int bus1Baud, bool bus1Enabled);
    void logMessage(const QString &text);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onTcpConnected();
    void onTcpError(QAbstractSocket::SocketError error);
    void onTcpDisconnected();
    void onValidationTick();
    void onConnectTimeout();

private:
    enum class RxState
    {
        Idle,
        GetCommand,
        BuildCanFrame,
        TimeSync,
        GetCanbusParams,
        GetDeviceInfo,
        GetNumBuses,
        GetExtBuses
    };

    void beginHandshakeSession();
    void processByte(quint8 c);
    void sendHandshake();
    void sendCommValidation();
    void sendBusConfig(quint32 baud, bool active, bool listenOnly);
    void sendToDevice(const QByteArray &bytes);

    QSerialPort *m_serial = nullptr;
    QTcpSocket *m_tcpSocket = nullptr;
    QIODevice *m_device = nullptr; // aliases whichever of the above is active

    QTimer m_validationTimer;
    QTimer m_connectTimeoutTimer;
    int m_validationCounter = 0;
    bool m_awaitingFirstValidation = false;
    bool m_connected = false;

    RxState m_rxState = RxState::Idle;
    int m_rxStep = 0;

    quint32 m_buildTimestamp = 0;
    quint32 m_buildId = 0;
    bool m_buildExtended = false;
    quint8 m_buildBus = 0;
    QByteArray m_buildData;

    // Scratch fields used while parsing multi-byte GET_CANBUS_PARAMS / GET_DEVICE_INFO replies.
    int m_can0Enabled = 0, m_can0ListenOnly = 0;
    quint32 m_can0Baud = 0;
    int m_can1Enabled = 0, m_can1ListenOnly = 0;
    quint32 m_can1Baud = 0;
    int m_deviceBuildNum = 0;
    int m_deviceSingleWireMode = 0;
};
