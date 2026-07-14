#include "Elm327Connection.h"

#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTcpSocket>

#include "ObdDtcClient.h" // for decodeDtc()

namespace {
constexpr int kCommandTimeoutMs = 4000; // ATZ can be slow; generous for all
const QList<qint32> kSerialBauds = {38400, 115200}; // common ELM327 defaults, tried in order
}

Elm327Connection::Elm327Connection(QObject *parent) : QObject(parent)
{
    m_pids = ObdPidMonitor::standardPids();

    m_commandTimeout.setSingleShot(true);
    connect(&m_commandTimeout, &QTimer::timeout, this, &Elm327Connection::onCommandTimeout);
    connect(&m_monitorTimer, &QTimer::timeout, this, &Elm327Connection::pollNext);
}

Elm327Connection::~Elm327Connection()
{
    close();
}

// m_initStep doubles as the serial-baud attempt index in the high bits; keep it
// simple with a dedicated member instead. (Re)opens the serial port at a baud.
void Elm327Connection::openSerial(const QString &portName)
{
    close();
    m_baudAttempt = 0;

    m_serial = new QSerialPort(QSerialPortInfo(portName), this);
    m_serial->setBaudRate(kSerialBauds.at(0));
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString err = m_serial->errorString();
        emit logMessage("Failed to open " + portName + ": " + err);
        m_serial->deleteLater();
        m_serial = nullptr;
        emit disconnected(err);
        return;
    }
    connect(m_serial, &QSerialPort::readyRead, this, &Elm327Connection::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e != QSerialPort::NoError && e != QSerialPort::TimeoutError)
            onIoError();
    });
    m_device = m_serial;
    emit logMessage(QString("Opened %1 at %2 baud; initializing ELM327...")
                        .arg(portName).arg(kSerialBauds.at(0)));
    beginSession();
}

void Elm327Connection::openNetwork(const QString &host, quint16 port)
{
    close();
    m_tcp = new QTcpSocket(this);
    connect(m_tcp, &QTcpSocket::readyRead, this, &Elm327Connection::onReadyRead);
    connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { onIoError(); });
    connect(m_tcp, &QTcpSocket::connected, this, [this]() {
        m_device = m_tcp;
        emit logMessage("Network link up; initializing ELM327...");
        beginSession();
    });
    emit logMessage(QString("Connecting to ELM327 at %1:%2 ...").arg(host).arg(port));
    m_tcp->connectToHost(host, port);
}

void Elm327Connection::close()
{
    m_monitorTimer.stop();
    m_commandTimeout.stop();
    m_queue.clear();
    m_commandInFlight = false;
    m_connected = false;
    m_rxBuf.clear();
    m_initStep = 0;
    m_device = nullptr;
    if (m_serial) {
        m_serial->disconnect();
        if (m_serial->isOpen())
            m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_tcp) {
        m_tcp->disconnect();
        if (m_tcp->isOpen())
            m_tcp->close();
        m_tcp->deleteLater();
        m_tcp = nullptr;
    }
}

bool Elm327Connection::isOpen() const
{
    return m_device && m_device->isOpen() && m_connected;
}

void Elm327Connection::writeRaw(const QByteArray &bytes)
{
    if (m_device && m_device->isOpen())
        m_device->write(bytes);
}

void Elm327Connection::beginSession()
{
    m_connected = false;
    m_initStep = 0;
    m_queue.clear();
    m_rxBuf.clear();
    m_commandInFlight = false;

    // Standard ELM327 init: reset, echo off, linefeeds off, headers off, auto
    // protocol. Each is queued as an Init command; finishConnect() runs after
    // the last one completes.
    enqueue({Kind::Init, "ATZ"});
    enqueue({Kind::Init, "ATE0"});
    enqueue({Kind::Init, "ATL0"});
    enqueue({Kind::Init, "ATH0"});
    enqueue({Kind::Init, "ATSP0"});
    pumpQueue();
}

void Elm327Connection::finishConnect()
{
    m_connected = true;
    emit connected();
}

void Elm327Connection::enqueue(const Command &cmd)
{
    m_queue.enqueue(cmd);
}

void Elm327Connection::pumpQueue()
{
    if (m_commandInFlight || m_queue.isEmpty() || !m_device || !m_device->isOpen())
        return;
    m_current = m_queue.dequeue();
    m_commandInFlight = true;
    m_rxBuf.clear();
    writeRaw(m_current.text + '\r');
    m_commandTimeout.start(kCommandTimeoutMs);
}

void Elm327Connection::onReadyRead()
{
    if (!m_device)
        return;
    m_rxBuf.append(m_device->readAll());

    // ELM327 terminates every reply with the '>' prompt.
    const int promptIdx = m_rxBuf.indexOf('>');
    if (promptIdx < 0)
        return;

    QString response = QString::fromLatin1(m_rxBuf.left(promptIdx));
    m_rxBuf.remove(0, promptIdx + 1);

    if (!m_commandInFlight)
        return; // stray data

    m_commandTimeout.stop();
    m_commandInFlight = false;
    const Command cmd = m_current;
    handleResponse(cmd, response);
    pumpQueue();
}

void Elm327Connection::onCommandTimeout()
{
    if (!m_commandInFlight)
        return;
    m_commandInFlight = false;

    // Auto-baud: if the very first reset (ATZ) never answered on serial, retry at
    // the next common baud rate before giving up.
    if (m_current.kind == Kind::Init && m_current.text == "ATZ" && m_serial) {
        m_baudAttempt++;
        if (m_baudAttempt < kSerialBauds.size()) {
            const qint32 baud = kSerialBauds.at(m_baudAttempt);
            emit logMessage(QString("No response; retrying at %1 baud...").arg(baud));
            m_serial->setBaudRate(baud);
            beginSession();
            return;
        }
        emit logMessage("ELM327 did not respond. Check the adapter, port, and baud rate.");
        close();
        emit disconnected("No response from ELM327 adapter");
        return;
    }

    emit logMessage("ELM327 command timed out: " + QString::fromLatin1(m_current.text));
    pumpQueue();
}

QVector<quint8> Elm327Connection::parseHexBytes(const QString &s)
{
    QVector<quint8> out;
    const QStringList tokens = s.split(QRegularExpression("[^0-9A-Fa-f]+"), Qt::SkipEmptyParts);
    for (const QString &t : tokens) {
        if (t.size() == 2) {
            bool ok = false;
            const int v = t.toInt(&ok, 16);
            if (ok)
                out.append(static_cast<quint8>(v));
        }
    }
    return out;
}

void Elm327Connection::synthesizeFrame(quint32 id, const QVector<quint8> &data)
{
    CanFrame f;
    f.id = id;
    f.extended = false;
    f.bus = 0;
    f.length = static_cast<quint8>(qMin(8, data.size()));
    for (int i = 0; i < f.length; ++i)
        f.data[i] = data.at(i);
    emit frameReceived(f);
}

void Elm327Connection::handleResponse(const Command &cmd, const QString &response)
{
    const QString clean = response.trimmed();

    if (cmd.kind == Kind::Init) {
        if (cmd.text == "ATZ" && !clean.isEmpty())
            emit deviceInfoReceived(clean.section('\n', -1).trimmed());
        m_initStep++;
        if (m_initStep >= 5) // all five init commands done
            finishConnect();
        return;
    }

    const QString upper = clean.toUpper();
    if (upper.contains("UNABLE TO CONNECT")) {
        emit logMessage("ELM327: unable to connect to the vehicle (is the ignition on?).");
        return;
    }
    if (upper.contains("NO DATA") || upper.contains("STOPPED") || upper.contains("?")) {
        // No response for this request; for DTC reads that means "no codes".
        if (cmd.kind == Kind::Dtc)
            emit dtcsReceived(cmd.mode, QStringList());
        return;
    }

    const QVector<quint8> bytes = parseHexBytes(clean);
    if (bytes.isEmpty())
        return;

    switch (cmd.kind) {
    case Kind::TestPid:
    case Kind::Pid: {
        // Expect: 41 <pid> <data...>
        int idx = bytes.indexOf(0x41);
        if (idx < 0 || idx + 1 >= bytes.size())
            return;
        const quint8 pid = bytes.at(idx + 1);
        synthesizeFrame(0x7E8, bytes.mid(idx - 0)); // show in raw view
        for (const PidDefinition &def : m_pids) {
            if (def.pid != pid)
                continue;
            const int dataStart = idx + 2;
            if (dataStart + def.dataBytes > bytes.size())
                return;
            quint8 buf[8] = {0};
            for (int i = 0; i < def.dataBytes && i < 8; ++i)
                buf[i] = bytes.at(dataStart + i);
            emit pidUpdated(pid, def.decode(buf));
            return;
        }
        break;
    }
    case Kind::Dtc: {
        int idx = bytes.indexOf(0x43);
        // 0x47 / 0x4A for pending / permanent
        if (idx < 0) idx = bytes.indexOf(0x47);
        if (idx < 0) idx = bytes.indexOf(0x4A);
        QStringList codes;
        if (idx >= 0) {
            int p = idx + 1;
            // Optional count byte, then 2-byte DTCs.
            if (p < bytes.size()) {
                int count = bytes.at(p);
                // Heuristic: if the "count" is implausible vs remaining bytes,
                // treat everything as DTC pairs (some adapters omit the count).
                const int remainingPairs = (bytes.size() - (p + 1)) / 2;
                if (count > 0 && count <= remainingPairs) {
                    p += 1;
                    for (int i = 0; i < count && p + 1 < bytes.size(); ++i, p += 2) {
                        const quint8 a = bytes.at(p), b = bytes.at(p + 1);
                        if (a == 0 && b == 0) continue;
                        codes << ObdDtcClient::decodeDtc(a, b);
                    }
                } else {
                    for (; p + 1 < bytes.size(); p += 2) {
                        const quint8 a = bytes.at(p), b = bytes.at(p + 1);
                        if (a == 0 && b == 0) continue;
                        codes << ObdDtcClient::decodeDtc(a, b);
                    }
                }
            }
        }
        emit dtcsReceived(cmd.mode, codes);
        break;
    }
    case Kind::Clear: {
        emit dtcsCleared();
        break;
    }
    case Kind::Vin: {
        int idx = -1;
        for (int i = 0; i + 2 < bytes.size(); ++i) {
            if (bytes.at(i) == 0x49 && bytes.at(i + 1) == 0x02) { idx = i; break; }
        }
        if (idx < 0)
            return;
        int p = idx + 2;
        if (p < bytes.size() && bytes.at(p) <= 0x0F) // skip NODI (number-of-items) byte
            p += 1;
        QByteArray vin;
        for (; p < bytes.size() && vin.size() < 17; ++p)
            vin.append(char(bytes.at(p)));
        emit vinReceived(QString::fromLatin1(vin).trimmed());
        break;
    }
    case Kind::CalId: {
        int idx = -1;
        for (int i = 0; i + 2 < bytes.size(); ++i) {
            if (bytes.at(i) == 0x49 && bytes.at(i + 1) == 0x04) { idx = i; break; }
        }
        if (idx < 0)
            return;
        int p = idx + 2;
        if (p < bytes.size() && bytes.at(p) <= 0x0F)
            p += 1; // NODI
        QStringList ids;
        QByteArray chunk;
        for (; p < bytes.size(); ++p) {
            chunk.append(char(bytes.at(p)));
            if (chunk.size() == 16) {
                int end = chunk.indexOf('\0');
                if (end >= 0) chunk.truncate(end);
                const QString id = QString::fromLatin1(chunk).trimmed();
                if (!id.isEmpty()) ids << id;
                chunk.clear();
            }
        }
        emit calibrationIdsReceived(ids);
        break;
    }
    default:
        break;
    }
}

// --- High-level operations ---

void Elm327Connection::startMonitoring(int intervalMs)
{
    m_pollIndex = 0;
    m_monitorTimer.start(intervalMs);
}

void Elm327Connection::stopMonitoring()
{
    m_monitorTimer.stop();
}

void Elm327Connection::pollNext()
{
    if (m_pids.isEmpty() || !isOpen())
        return;
    // Backpressure: don't pile up requests faster than the adapter answers.
    if (!m_queue.isEmpty())
        return;

    const PidDefinition &def = m_pids.at(m_pollIndex);
    m_pollIndex = (m_pollIndex + 1) % m_pids.size();

    Command c;
    c.kind = Kind::Pid;
    c.pid = def.pid;
    c.text = QByteArray("01") + QByteArray::number(def.pid, 16).rightJustified(2, '0').toUpper();
    enqueue(c);
    synthesizeFrame(0x7DF, {0x02, 0x01, def.pid});
    pumpQueue();
}

void Elm327Connection::sendTestRequest()
{
    enqueue({Kind::TestPid, "0100", 0x00, 0});
    pumpQueue();
}

void Elm327Connection::readStoredDtcs()
{
    enqueue({Kind::Dtc, "03", 0, 0x03});
    pumpQueue();
}

void Elm327Connection::readPendingDtcs()
{
    enqueue({Kind::Dtc, "07", 0, 0x07});
    pumpQueue();
}

void Elm327Connection::readPermanentDtcs()
{
    enqueue({Kind::Dtc, "0A", 0, 0x0A});
    pumpQueue();
}

void Elm327Connection::clearDtcs()
{
    enqueue({Kind::Clear, "04", 0, 0x04});
    pumpQueue();
}

void Elm327Connection::readVin()
{
    enqueue({Kind::Vin, "0902"});
    pumpQueue();
}

void Elm327Connection::readCalibrationIds()
{
    enqueue({Kind::CalId, "0904"});
    pumpQueue();
}

void Elm327Connection::onIoError()
{
    if (!m_device)
        return;
    const QString msg = m_serial ? m_serial->errorString()
                                  : (m_tcp ? m_tcp->errorString() : QStringLiteral("I/O error"));
    emit logMessage("ELM327 link error: " + msg);
    close();
    emit disconnected(msg);
}
