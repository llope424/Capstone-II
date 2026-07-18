#include "ObdFreezeFrameClient.h"

#include "GvretConnection.h"
#include "ObdDtcClient.h" // for decodeDtc()

namespace {
constexpr quint32 kFunctionalRequestId = 0x7DF;
constexpr quint32 kResponseIdMin = 0x7E8;
constexpr quint32 kResponseIdMax = 0x7EF;
constexpr quint8 kModeFreezeFrame = 0x02;
constexpr quint8 kPositiveResponse = 0x42;
constexpr quint8 kPidTriggerDtc = 0x02;
constexpr int kStepTimeoutMs = 700;
}

ObdFreezeFrameClient::ObdFreezeFrameClient(GvretConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    m_pids = ObdPidMonitor::standardPids();
    connect(m_connection, &GvretConnection::frameReceived, this,
            &ObdFreezeFrameClient::onFrameReceived);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ObdFreezeFrameClient::onTimeout);
}

void ObdFreezeFrameClient::read()
{
    if (!m_connection->isOpen()) {
        emit logMessage("Cannot read freeze frame - not connected.");
        return;
    }
    if (m_step >= 0) {
        emit logMessage("A freeze-frame read is already in progress; please wait.");
        return;
    }
    m_step = 0;
    sendCurrent();
}

quint8 ObdFreezeFrameClient::currentPid() const
{
    return m_step == 0 ? kPidTriggerDtc : m_pids.at(m_step - 1).pid;
}

void ObdFreezeFrameClient::sendCurrent()
{
    // ISO-TP single frame: [PCI len=3][mode 02][PID][frame 00], zero-padded.
    QByteArray payload;
    payload.append(char(0x03));
    payload.append(char(kModeFreezeFrame));
    payload.append(char(currentPid()));
    payload.append(char(0x00));
    while (payload.size() < 8)
        payload.append(char(0x00));
    m_connection->sendFrame(kFunctionalRequestId, false, 0, payload);
    m_timeout.start(kStepTimeoutMs);
}

void ObdFreezeFrameClient::onFrameReceived(const CanFrame &frame)
{
    if (m_step < 0)
        return;
    if (frame.id < kResponseIdMin || frame.id > kResponseIdMax)
        return;
    // Single frame [PCI][42][pid][frame][data...] for the step we asked about.
    if (frame.length < 4 || (frame.data[0] >> 4) != 0x0)
        return;
    if (frame.data[1] != kPositiveResponse || frame.data[2] != currentPid())
        return;

    if (m_step == 0) {
        if (frame.length < 6)
            return;
        const quint8 a = frame.data[4], b = frame.data[5];
        if (a == 0 && b == 0)
            emit freezeFrameDtcReceived(QString(), false);
        else
            emit freezeFrameDtcReceived(ObdDtcClient::decodeDtc(a, b), true);
    } else {
        const PidDefinition &def = m_pids.at(m_step - 1);
        if (frame.length < 4 + def.dataBytes)
            return;
        quint8 buf[8] = {0};
        for (int i = 0; i < def.dataBytes && i < 4; ++i)
            buf[i] = frame.data[4 + i];
        emit freezeFramePidReceived(def.pid, def.decode(buf), true);
    }
    advance();
}

void ObdFreezeFrameClient::onTimeout()
{
    if (m_step < 0)
        return;
    if (m_step == 0)
        emit freezeFrameDtcReceived(QString(), false);
    else
        emit freezeFramePidReceived(m_pids.at(m_step - 1).pid, 0.0, false);
    advance();
}

void ObdFreezeFrameClient::advance()
{
    m_timeout.stop();
    ++m_step;
    if (m_step > m_pids.size()) {
        m_step = -1; // done
        return;
    }
    sendCurrent();
}
