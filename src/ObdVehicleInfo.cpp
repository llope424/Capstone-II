#include "ObdVehicleInfo.h"

#include "GvretConnection.h"

namespace {
constexpr quint32 kFunctionalRequestId = 0x7DF;
constexpr quint32 kResponseIdMin = 0x7E8;
constexpr quint32 kResponseIdMax = 0x7EF;
constexpr quint8 kModeVehicleInfo = 0x09;
constexpr quint8 kPositiveResponse = 0x49; // 0x09 + 0x40
constexpr quint8 kNegativeResponse = 0x7F;
constexpr quint8 kPidVin = 0x02;
constexpr quint8 kPidCalId = 0x04;
constexpr int kTimeoutMs = 2000;
}

ObdVehicleInfo::ObdVehicleInfo(GvretConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    connect(m_connection, &GvretConnection::frameReceived, this, &ObdVehicleInfo::onFrameReceived);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ObdVehicleInfo::onTimeout);
}

void ObdVehicleInfo::readVin() { beginRequest(kPidVin); }
void ObdVehicleInfo::readCalibrationIds() { beginRequest(kPidCalId); }

void ObdVehicleInfo::beginRequest(quint8 pid)
{
    if (!m_connection->isOpen()) {
        emit logMessage("Cannot request vehicle info - not connected.");
        return;
    }
    if (m_activePid != 0) {
        emit logMessage("A vehicle-info request is already in progress; please wait.");
        return;
    }

    m_activePid = pid;
    m_responderId = 0;
    m_rxBuffer.clear();
    m_rxRemaining = 0;
    m_expectedSeq = 1;

    // ISO-TP single frame: [PCI len=2][mode 09][pid], padded to 8.
    QByteArray payload;
    payload.append(char(0x02));
    payload.append(char(kModeVehicleInfo));
    payload.append(char(pid));
    while (payload.size() < 8)
        payload.append(char(0x00));

    m_connection->sendFrame(kFunctionalRequestId, false, 0, payload);
    m_timeout.start(kTimeoutMs);
}

void ObdVehicleInfo::sendFlowControl(quint32 responderId)
{
    const quint32 requestId = responderId - 8;
    QByteArray payload;
    payload.append(char(0x30)); // Continue To Send
    payload.append(char(0x00)); // Block Size = all
    payload.append(char(0x00)); // STmin = 0
    while (payload.size() < 8)
        payload.append(char(0x00));
    m_connection->sendFrame(requestId, false, 0, payload);
}

void ObdVehicleInfo::onFrameReceived(const CanFrame &frame)
{
    if (m_activePid == 0)
        return;
    if (frame.id < kResponseIdMin || frame.id > kResponseIdMax)
        return;
    if (m_responderId != 0 && frame.id != m_responderId)
        return;
    if (frame.length < 1)
        return;

    const quint8 pciType = frame.data[0] >> 4;

    switch (pciType) {
    case 0x0: { // Single Frame
        const int len = frame.data[0] & 0x0F;
        if (len < 1 || len > 7 || len > frame.length - 1)
            return;
        m_responderId = frame.id;
        handleAssembledResponse(QByteArray(reinterpret_cast<const char *>(&frame.data[1]), len));
        break;
    }
    case 0x1: { // First Frame
        if (frame.length < 2)
            return;
        const int totalLen = ((frame.data[0] & 0x0F) << 8) | frame.data[1];
        m_responderId = frame.id;
        m_rxBuffer = QByteArray(reinterpret_cast<const char *>(&frame.data[2]), 6);
        m_rxRemaining = totalLen - 6;
        m_expectedSeq = 1;
        sendFlowControl(frame.id);
        m_timeout.start(kTimeoutMs);
        break;
    }
    case 0x2: { // Consecutive Frame
        const quint8 seq = frame.data[0] & 0x0F;
        if (seq != m_expectedSeq) {
            emit logMessage("ISO-TP sequence error - aborting vehicle-info read.");
            resetTransaction();
            return;
        }
        const int take = qMin(7, m_rxRemaining);
        m_rxBuffer.append(reinterpret_cast<const char *>(&frame.data[1]), take);
        m_rxRemaining -= take;
        m_expectedSeq = (m_expectedSeq + 1) & 0x0F;
        m_timeout.start(kTimeoutMs);
        if (m_rxRemaining <= 0)
            handleAssembledResponse(m_rxBuffer);
        break;
    }
    default:
        break;
    }
}

void ObdVehicleInfo::handleAssembledResponse(const QByteArray &payload)
{
    if (payload.isEmpty())
        return;

    const quint8 pid = m_activePid;
    const quint8 first = static_cast<quint8>(payload.at(0));

    if (first == kNegativeResponse) {
        emit logMessage("ECU rejected the Mode 09 request.");
        resetTransaction();
        return;
    }
    if (first != kPositiveResponse || payload.size() < 3) {
        emit logMessage("Unexpected vehicle-info response.");
        resetTransaction();
        return;
    }

    // payload = [0x49][pid][NODI][data...]
    const QByteArray data = payload.mid(3);
    resetTransaction();

    if (pid == kPidVin) {
        QString vin = QString::fromLatin1(data).trimmed();
        // Strip any leading padding nulls some ECUs prepend.
        vin.remove(QChar('\0'));
        emit vinReceived(vin);
    } else if (pid == kPidCalId) {
        QStringList ids;
        for (int off = 0; off + 16 <= data.size(); off += 16) {
            QByteArray chunk = data.mid(off, 16);
            // Cal IDs are null-padded ASCII.
            int end = chunk.indexOf('\0');
            if (end >= 0)
                chunk.truncate(end);
            const QString id = QString::fromLatin1(chunk).trimmed();
            if (!id.isEmpty())
                ids << id;
        }
        emit calibrationIdsReceived(ids);
    }
}

void ObdVehicleInfo::onTimeout()
{
    if (m_activePid == 0)
        return;
    const quint8 pid = m_activePid;
    resetTransaction();
    if (pid == kPidVin)
        emit logMessage("No VIN response (timed out). The vehicle may not support Mode 09 PID 02.");
    else
        emit logMessage("No calibration-ID response (timed out).");
}

void ObdVehicleInfo::resetTransaction()
{
    m_timeout.stop();
    m_activePid = 0;
    m_responderId = 0;
    m_rxBuffer.clear();
    m_rxRemaining = 0;
    m_expectedSeq = 1;
}
