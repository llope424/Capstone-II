#include "GvretConnection.h"

#include <QSerialPortInfo>

namespace {
constexpr quint8 kFrameStart = 0xF1;
constexpr quint8 kCmdBuildCanFrame = 0;
constexpr quint8 kCmdTimeSync = 1;
constexpr quint8 kCmdSetupCanbus = 5;
constexpr quint8 kCmdGetCanbusParams = 6;
constexpr quint8 kCmdGetDeviceInfo = 7;
constexpr quint8 kCmdCommValidation = 9;
constexpr quint8 kCmdGetNumBuses = 12;
constexpr quint8 kCmdGetExtBuses = 13;
}

GvretConnection::GvretConnection(QObject *parent) : QObject(parent)
{
    connect(&m_validationTimer, &QTimer::timeout, this, &GvretConnection::onValidationTick);
    m_connectTimeoutTimer.setSingleShot(true);
    connect(&m_connectTimeoutTimer, &QTimer::timeout, this, &GvretConnection::onConnectTimeout);
}

GvretConnection::~GvretConnection()
{
    close();
}

void GvretConnection::openSerial(const QString &portName, bool espMode)
{
    close();

    m_serial = new QSerialPort(QSerialPortInfo(portName), this);
    m_serial->setBaudRate(1000000);
    m_serial->setDataBits(QSerialPort::Data8);

    if (espMode) {
        m_serial->setFlowControl(QSerialPort::NoFlowControl);
    } else {
        m_serial->setFlowControl(QSerialPort::HardwareControl);
    }

    // Open BEFORE connecting the error signal. QSerialPort emits errorOccurred
    // synchronously from inside open() when it fails; if that were already wired
    // to onSerialError (which calls close() and frees m_serial) we would then
    // dereference a freed m_serial below and crash. Handle open failure inline.
    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString err = m_serial->errorString();
        emit logMessage("Failed to open " + portName + ": " + err +
                         " (is another program - Arduino IDE, SavvyCAN, another window - using this port?)");
        m_serial->deleteLater();
        m_serial = nullptr;
        emit disconnected(err);
        return;
    }

    if (espMode) {
        // ESP32 boards use DTR/RTS for bootloader entry / reset, so leave them low.
        m_serial->setDataTerminalReady(false);
        m_serial->setRequestToSend(false);
    }

    connect(m_serial, &QSerialPort::readyRead, this, &GvretConnection::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &GvretConnection::onSerialError);

    m_device = m_serial;
    beginHandshakeSession();
}

void GvretConnection::openNetwork(const QString &host, quint16 port)
{
    close();

    m_tcpSocket = new QTcpSocket(this);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &GvretConnection::onReadyRead);
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &GvretConnection::onTcpError);
    connect(m_tcpSocket, &QTcpSocket::connected, this, &GvretConnection::onTcpConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &GvretConnection::onTcpDisconnected);

    emit logMessage(QString("Connecting to %1:%2 ...").arg(host).arg(port));
    m_tcpSocket->connectToHost(host, port);
    // Handshake begins once onTcpConnected() fires; the connect timeout is
    // still armed here so a host that never accepts the connection also
    // eventually reports failure instead of hanging silently.
    m_connectTimeoutTimer.start(8000);
}

void GvretConnection::onTcpConnected()
{
    m_device = m_tcpSocket;
    beginHandshakeSession();
}

void GvretConnection::onTcpError(QAbstractSocket::SocketError error)
{
    if (!m_tcpSocket)
        return; // already torn down by another handler

    QString message = m_tcpSocket->errorString();
    if (error == QAbstractSocket::RemoteHostClosedError) {
        message += " - the ESP32 may still be holding a previous connection. "
                   "Power-cycle the board and make sure only one program (not SavvyCAN "
                   "or Arduino IDE) is connected.";
    }
    emit logMessage("Network error: " + message);
    close();
    emit disconnected(message);
}

void GvretConnection::onTcpDisconnected()
{
    if (!m_tcpSocket)
        return; // already handled via onTcpError/close
    emit logMessage("Network connection closed by the device.");
    close();
    emit disconnected("Connection closed by the device");
}

void GvretConnection::beginHandshakeSession()
{
    m_rxState = RxState::Idle;
    m_rxStep = 0;
    m_validationCounter = 10;
    m_awaitingFirstValidation = true;
    m_connected = false;

    sendHandshake();

    m_validationTimer.setInterval(250);
    m_validationTimer.start();

    // If the scanner never answers GET_CANBUS_PARAMS, don't hang on
    // "Connecting..." forever - fail loudly after a few seconds instead.
    m_connectTimeoutTimer.start(5000);
}

void GvretConnection::close()
{
    m_validationTimer.stop();
    m_connectTimeoutTimer.stop();
    m_device = nullptr;
    if (m_serial) {
        // Detach signals first so close() can't fire errorOccurred back into our
        // handlers mid-teardown (which would re-enter close() and double-free).
        m_serial->disconnect();
        if (m_serial->isOpen())
            m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_tcpSocket) {
        m_tcpSocket->disconnect();
        if (m_tcpSocket->isOpen())
            m_tcpSocket->close();
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }
}

bool GvretConnection::isOpen() const
{
    return m_device && m_device->isOpen();
}

void GvretConnection::sendToDevice(const QByteArray &bytes)
{
    if (!m_device || !m_device->isOpen()) {
        emit logMessage("Attempted to write while not connected");
        return;
    }
    m_device->write(bytes);
}

void GvretConnection::sendHandshake()
{
    QByteArray out;
    out.append(char(0xE7));
    out.append(char(0xE7)); // enter binary comm mode

    out.append(char(kFrameStart));
    out.append(char(kCmdGetNumBuses));

    out.append(char(kFrameStart));
    out.append(char(kCmdGetCanbusParams));

    out.append(char(kFrameStart));
    out.append(char(kCmdGetDeviceInfo));

    out.append(char(kFrameStart));
    out.append(char(kCmdTimeSync));

    out.append(char(kFrameStart));
    out.append(char(kCmdCommValidation));

    sendToDevice(out);
}

void GvretConnection::sendCommValidation()
{
    QByteArray out;
    out.append(char(kFrameStart));
    out.append(char(kCmdCommValidation));
    sendToDevice(out);
}

void GvretConnection::sendBusConfig(quint32 baud, bool active, bool listenOnly)
{
    // GVRET SETUP_CANBUS (command 5): two 32-bit words (bus0, bus1), each the
    // speed OR'd with control flags. Bit 31 = "this word is valid/configure",
    // bit 30 = bus active, bit 29 = listen-only.
    quint32 bus0 = baud | 0x80000000u;
    if (active)
        bus0 |= 0x40000000u;
    if (listenOnly)
        bus0 |= 0x20000000u;

    const quint32 bus1 = 0; // leave bus 1 untouched (we only use bus 0)

    QByteArray buf;
    buf.append(char(kFrameStart));
    buf.append(char(kCmdSetupCanbus));
    buf.append(char(bus0 & 0xFF));
    buf.append(char((bus0 >> 8) & 0xFF));
    buf.append(char((bus0 >> 16) & 0xFF));
    buf.append(char((bus0 >> 24) & 0xFF));
    buf.append(char(bus1 & 0xFF));
    buf.append(char((bus1 >> 8) & 0xFF));
    buf.append(char((bus1 >> 16) & 0xFF));
    buf.append(char((bus1 >> 24) & 0xFF));
    buf.append(char(0)); // trailing pad byte
    sendToDevice(buf);
}

bool GvretConnection::sendFrame(quint32 id, bool extended, quint8 bus, const QByteArray &payload)
{
    if (!isOpen() || payload.size() > 8)
        return false;

    quint32 wireId = id;
    if (extended)
        wireId |= (1u << 31);

    QByteArray buffer;
    buffer.append(char(kFrameStart));
    buffer.append(char(kCmdBuildCanFrame));
    buffer.append(char(wireId & 0xFF));
    buffer.append(char((wireId >> 8) & 0xFF));
    buffer.append(char((wireId >> 16) & 0xFF));
    buffer.append(char((wireId >> 24) & 0xFF));
    buffer.append(char(bus & 3));
    buffer.append(char(payload.size()));
    buffer.append(payload);
    buffer.append(char(0)); // trailing pad byte expected by GVRET framing

    sendToDevice(buffer);
    return true;
}

void GvretConnection::onReadyRead()
{
    if (!m_device)
        return;
    const QByteArray data = m_device->readAll();

    if (!m_connected) {
        // While we're still trying to complete the handshake, show exactly what
        // came back so a protocol mismatch (wrong framing, ASCII text, etc.) is
        // visible instead of silently discarded by the parser below.
        QStringList hex;
        for (unsigned char b : data)
            hex << QStringLiteral("%1").arg(b, 2, 16, QChar('0'));
        emit logMessage(QString("RX (%1 bytes): %2").arg(data.size()).arg(hex.join(' ')));
    }

    for (int i = 0; i < data.size(); ++i)
        processByte(static_cast<quint8>(data.at(i)));
}

void GvretConnection::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    const QString message = m_serial ? m_serial->errorString() : QStringLiteral("Unknown serial error");
    emit logMessage("Serial error: " + message);

    switch (error) {
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::PermissionError:
    case QSerialPort::OpenError:
    case QSerialPort::ResourceError:
    case QSerialPort::UnknownError:
    case QSerialPort::NotOpenError:
        close();
        emit disconnected(message);
        break;
    default:
        break;
    }
}

void GvretConnection::onValidationTick()
{
    if (!isOpen())
        return;

    // The liveness heartbeat only makes sense once we've actually connected;
    // before that, onConnectTimeout() is what detects a dead handshake.
    if (m_connected) {
        if (!m_awaitingFirstValidation) {
            m_validationCounter--;
            if (m_validationCounter <= 0) {
                emit logMessage("Comm validation failed - scanner stopped responding");
                const QString reason = "No response from scanner";
                close();
                emit disconnected(reason);
                return;
            }
        }
        sendCommValidation();
    }
}

void GvretConnection::onConnectTimeout()
{
    if (m_connected || !isOpen())
        return;

    emit logMessage("Timed out waiting for scanner to respond to GET_CANBUS_PARAMS - "
                     "check that the firmware speaks GVRET on this port.");
    close();
    emit disconnected("Connection timed out - no valid GVRET response from scanner");
}

void GvretConnection::processByte(quint8 c)
{
    switch (m_rxState) {
    case RxState::Idle:
        if (c == kFrameStart)
            m_rxState = RxState::GetCommand;
        return;

    case RxState::GetCommand:
        m_rxStep = 0;
        switch (c) {
        case kCmdBuildCanFrame:
            m_rxState = RxState::BuildCanFrame;
            break;
        case kCmdTimeSync:
            m_rxState = RxState::TimeSync;
            break;
        case kCmdGetCanbusParams:
            m_rxState = RxState::GetCanbusParams;
            break;
        case kCmdGetDeviceInfo:
            m_rxState = RxState::GetDeviceInfo;
            break;
        case kCmdCommValidation:
            m_validationCounter = 10;
            m_awaitingFirstValidation = false;
            m_rxState = RxState::Idle;
            break;
        case kCmdGetNumBuses:
            m_rxState = RxState::GetNumBuses;
            break;
        case kCmdGetExtBuses:
            m_rxState = RxState::GetExtBuses;
            break;
        case kCmdSetupCanbus:
            // The device never replies to this command; nothing to consume.
            m_rxState = RxState::Idle;
            break;
        default:
            // Unrecognized command byte - don't get permanently stuck waiting
            // for bytes that will never come.
            emit logMessage(QString("Unknown GVRET command byte 0x%1").arg(c, 2, 16, QChar('0')));
            m_rxState = RxState::Idle;
            break;
        }
        return;

    case RxState::BuildCanFrame:
        switch (m_rxStep) {
        case 0: m_buildTimestamp = c; break;
        case 1: m_buildTimestamp |= quint32(c) << 8; break;
        case 2: m_buildTimestamp |= quint32(c) << 16; break;
        case 3: m_buildTimestamp |= quint32(c) << 24; break;
        case 4: m_buildId = c; break;
        case 5: m_buildId |= quint32(c) << 8; break;
        case 6: m_buildId |= quint32(c) << 16; break;
        case 7:
            m_buildId |= quint32(c) << 24;
            m_buildExtended = (m_buildId & (1u << 31)) != 0;
            if (m_buildExtended)
                m_buildId &= 0x7FFFFFFFu;
            break;
        case 8: {
            const int dlc = c & 0xF;
            m_buildBus = (c & 0xF0) >> 4;
            if (dlc > 8) {
                // A classic CAN frame carries at most 8 data bytes; a bigger
                // value means we latched onto a stray byte mid-stream. Resync
                // instead of overflowing CanFrame::data.
                m_rxState = RxState::Idle;
                return;
            }
            m_buildData.resize(dlc);
            if (m_buildData.isEmpty()) {
                // Zero-length frame - nothing more to read, finish immediately.
                CanFrame frame;
                frame.timestampUs = m_buildTimestamp;
                frame.id = m_buildId;
                frame.extended = m_buildExtended;
                frame.bus = m_buildBus;
                frame.length = 0;
                emit frameReceived(frame);
                m_rxState = RxState::Idle;
                return;
            }
            break;
        }
        default: {
            const int dataIndex = m_rxStep - 9;
            if (dataIndex >= 0 && dataIndex < m_buildData.size()) {
                m_buildData[dataIndex] = char(c);
                if (dataIndex == m_buildData.size() - 1) {
                    CanFrame frame;
                    frame.timestampUs = m_buildTimestamp;
                    frame.id = m_buildId;
                    frame.extended = m_buildExtended;
                    frame.bus = m_buildBus;
                    frame.length = static_cast<quint8>(m_buildData.size());
                    for (int i = 0; i < m_buildData.size(); ++i)
                        frame.data[i] = static_cast<quint8>(m_buildData.at(i));
                    emit frameReceived(frame);
                    m_rxState = RxState::Idle;
                    return;
                }
            } else {
                // Should never happen; resync defensively.
                m_rxState = RxState::Idle;
                return;
            }
            break;
        }
        }
        m_rxStep++;
        return;

    case RxState::TimeSync:
        // 4-byte device timestamp basis; we don't currently need it for the
        // raw traffic view (frame timestamps are shown relative to arrival).
        m_rxStep++;
        if (m_rxStep >= 4)
            m_rxState = RxState::Idle;
        return;

    case RxState::GetCanbusParams:
        switch (m_rxStep) {
        case 0:
            m_can0Enabled = c & 0xF;
            m_can0ListenOnly = c >> 4;
            break;
        case 1: m_can0Baud = c; break;
        case 2: m_can0Baud |= quint32(c) << 8; break;
        case 3: m_can0Baud |= quint32(c) << 16; break;
        case 4: m_can0Baud |= quint32(c) << 24; break;
        case 5:
            m_can1Enabled = c & 0xF;
            m_can1ListenOnly = c >> 4;
            break;
        case 6: m_can1Baud = c; break;
        case 7: m_can1Baud |= quint32(c) << 8; break;
        case 8: m_can1Baud |= quint32(c) << 16; break;
        case 9:
            m_can1Baud |= quint32(c) << 24;
            emit busParamsReceived(static_cast<int>(m_can0Baud), m_can0Enabled != 0,
                                    static_cast<int>(m_can1Baud), m_can1Enabled != 0);
            emit logMessage(QString("Bus 0 listen-only: %1, enabled: %2")
                                 .arg(m_can0ListenOnly ? "yes" : "no")
                                 .arg(m_can0Enabled ? "yes" : "no"));

            // Explicitly (re)configure bus 0 active and NOT listen-only so we can
            // actually transmit requests. A device left in listen-only mode can
            // sniff traffic but never puts a frame on the bus, which looks exactly
            // like "reading works but nothing ever answers our requests."
            {
                const quint32 baud = m_can0Baud ? m_can0Baud : 500000u;
                sendBusConfig(baud, /*active=*/true, /*listenOnly=*/false);
                emit logMessage(QString("Enabled bus 0 for transmit at %1 baud.").arg(baud));
            }

            m_rxState = RxState::Idle;
            if (!m_connected) {
                m_connected = true;
                m_connectTimeoutTimer.stop();
                emit connected();
            }
            break;
        }
        m_rxStep++;
        return;

    case RxState::GetDeviceInfo:
        switch (m_rxStep) {
        case 0: m_deviceBuildNum = c; break;
        case 1: m_deviceBuildNum |= c << 8; break;
        case 2: break; // eeprom version, unused
        case 3: break; // file type, unused
        case 4: break; // auto-log flag, unused
        case 5:
            m_deviceSingleWireMode = c;
            emit deviceInfoReceived(m_deviceBuildNum, m_deviceSingleWireMode);
            m_rxState = RxState::Idle;
            break;
        }
        m_rxStep++;
        return;

    case RxState::GetNumBuses: {
        // Single byte: number of buses implemented by the firmware.
        emit logMessage(QString("Scanner reports %1 CAN bus(es)").arg(c));
        QByteArray out;
        out.append(char(kFrameStart));
        out.append(char(kCmdGetExtBuses));
        sendToDevice(out);
        m_rxState = RxState::Idle;
        return;
    }

    case RxState::GetExtBuses:
        // 15-byte extended bus (SWCAN/LIN) info. We don't use it in this MVP,
        // just consume the bytes so the stream doesn't desync.
        m_rxStep++;
        if (m_rxStep >= 15)
            m_rxState = RxState::Idle;
        return;

    default:
        m_rxState = RxState::Idle;
        return;
    }
}
