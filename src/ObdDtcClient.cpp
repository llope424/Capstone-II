#include "ObdDtcClient.h"

#include <QHash>

#include "GvretConnection.h"

namespace {
constexpr quint32 kFunctionalRequestId = 0x7DF;
constexpr quint32 kResponseIdMin = 0x7E8;
constexpr quint32 kResponseIdMax = 0x7EF;
constexpr quint8 kPositiveResponseOffset = 0x40;
constexpr quint8 kNegativeResponse = 0x7F;
constexpr int kTransactionTimeoutMs = 2000;

constexpr quint8 kServiceStored = 0x03;
constexpr quint8 kServicePending = 0x07;
constexpr quint8 kServicePermanent = 0x0A;
constexpr quint8 kServiceClear = 0x04;

QChar hexDigit(int v)
{
    return QChar(v < 10 ? ('0' + v) : ('A' + v - 10));
}
}

ObdDtcClient::ObdDtcClient(GvretConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    connect(m_connection, &GvretConnection::frameReceived, this, &ObdDtcClient::onFrameReceived);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ObdDtcClient::onTimeout);
}

void ObdDtcClient::readStoredDtcs() { beginTransaction(kServiceStored); }
void ObdDtcClient::readPendingDtcs() { beginTransaction(kServicePending); }
void ObdDtcClient::readPermanentDtcs() { beginTransaction(kServicePermanent); }
void ObdDtcClient::clearDtcs() { beginTransaction(kServiceClear); }

void ObdDtcClient::beginTransaction(quint8 serviceMode)
{
    if (!m_connection->isOpen()) {
        emit logMessage("Cannot send DTC request - not connected.");
        return;
    }
    if (m_activeMode != 0) {
        emit logMessage("A DTC request is already in progress; please wait.");
        return;
    }

    m_activeMode = serviceMode;
    m_responderId = 0;
    m_rxBuffer.clear();
    m_rxRemaining = 0;
    m_expectedSeq = 1;

    sendServiceRequest(serviceMode);
    m_timeout.start(kTransactionTimeoutMs);
}

void ObdDtcClient::sendServiceRequest(quint8 serviceMode)
{
    // Services 03/07/0A/04 take no sub-parameters, so the ISO-TP single frame
    // is just [PCI len=1][service], padded to 8 bytes.
    QByteArray payload;
    payload.append(char(0x01));
    payload.append(char(serviceMode));
    while (payload.size() < 8)
        payload.append(char(0x00));

    m_connection->sendFrame(kFunctionalRequestId, false, 0, payload);
}

void ObdDtcClient::sendFlowControl(quint32 responderId)
{
    // Physical request ID for an ECU responding on 0x7E8..0x7EF is id - 8.
    const quint32 requestId = responderId - 8;
    QByteArray payload;
    payload.append(char(0x30)); // Flow Control, FS = Continue To Send
    payload.append(char(0x00)); // Block Size = 0 (send all remaining frames)
    payload.append(char(0x00)); // STmin = 0 (no minimum separation)
    while (payload.size() < 8)
        payload.append(char(0x00));

    m_connection->sendFrame(requestId, false, 0, payload);
}

void ObdDtcClient::onFrameReceived(const CanFrame &frame)
{
    if (m_activeMode == 0)
        return;
    if (frame.id < kResponseIdMin || frame.id > kResponseIdMax)
        return;
    // Once an ECU starts answering, ignore others for this transaction.
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
        QByteArray payload(reinterpret_cast<const char *>(&frame.data[1]), len);
        handleAssembledResponse(payload);
        break;
    }
    case 0x1: { // First Frame of a multi-frame message
        if (frame.length < 2)
            return;
        const int totalLen = ((frame.data[0] & 0x0F) << 8) | frame.data[1];
        m_responderId = frame.id;
        m_rxBuffer = QByteArray(reinterpret_cast<const char *>(&frame.data[2]), 6);
        m_rxRemaining = totalLen - 6;
        m_expectedSeq = 1;
        sendFlowControl(frame.id);
        m_timeout.start(kTransactionTimeoutMs);
        break;
    }
    case 0x2: { // Consecutive Frame
        const quint8 seq = frame.data[0] & 0x0F;
        if (seq != m_expectedSeq) {
            emit logMessage(QString("ISO-TP sequence error (expected %1, got %2) - aborting DTC read.")
                                 .arg(m_expectedSeq).arg(seq));
            resetTransaction();
            return;
        }
        const int take = qMin(7, m_rxRemaining);
        m_rxBuffer.append(reinterpret_cast<const char *>(&frame.data[1]), take);
        m_rxRemaining -= take;
        m_expectedSeq = (m_expectedSeq + 1) & 0x0F;
        m_timeout.start(kTransactionTimeoutMs);
        if (m_rxRemaining <= 0)
            handleAssembledResponse(m_rxBuffer);
        break;
    }
    default:
        break;
    }
}

void ObdDtcClient::handleAssembledResponse(const QByteArray &payload)
{
    if (payload.isEmpty())
        return;

    const quint8 requestedMode = m_activeMode;
    const quint8 first = static_cast<quint8>(payload.at(0));

    if (first == kNegativeResponse) {
        const quint8 svc = payload.size() > 1 ? static_cast<quint8>(payload.at(1)) : 0;
        const quint8 nrc = payload.size() > 2 ? static_cast<quint8>(payload.at(2)) : 0;
        emit logMessage(QString("ECU rejected service 0x%1 (NRC 0x%2).")
                             .arg(svc, 2, 16, QChar('0'))
                             .arg(nrc, 2, 16, QChar('0')));
        resetTransaction();
        return;
    }

    if (requestedMode == kServiceClear) {
        if (first == (kServiceClear + kPositiveResponseOffset)) {
            resetTransaction();
            emit dtcsCleared();
        } else {
            emit logMessage("Unexpected response to clear request.");
            resetTransaction();
        }
        return;
    }

    if (first != (requestedMode + kPositiveResponseOffset)) {
        emit logMessage(QString("Unexpected DTC response service byte 0x%1.")
                             .arg(first, 2, 16, QChar('0')));
        resetTransaction();
        return;
    }

    // [service][DTC count][pairs...] per ISO 15765-4.
    QStringList codes;
    if (payload.size() >= 2) {
        const int count = static_cast<quint8>(payload.at(1));
        int idx = 2;
        for (int i = 0; i < count && idx + 1 < payload.size(); ++i) {
            const quint8 a = static_cast<quint8>(payload.at(idx));
            const quint8 b = static_cast<quint8>(payload.at(idx + 1));
            idx += 2;
            if (a == 0 && b == 0)
                continue; // padding / empty slot
            codes << decodeDtc(a, b);
        }
    }

    resetTransaction();
    emit dtcsReceived(requestedMode, codes);
}

void ObdDtcClient::onTimeout()
{
    if (m_activeMode == 0)
        return;

    // Mode 03/07/0A with no DTCs sometimes yields no reply at all on some ECUs;
    // for a clear, a missing ack is a genuine failure worth surfacing.
    if (m_activeMode == kServiceClear) {
        emit logMessage("No response to clear-DTC request (timed out).");
    } else {
        const quint8 mode = m_activeMode;
        resetTransaction();
        emit dtcsReceived(mode, QStringList()); // treat as "no codes / no response"
        return;
    }
    resetTransaction();
}

void ObdDtcClient::resetTransaction()
{
    m_timeout.stop();
    m_activeMode = 0;
    m_responderId = 0;
    m_rxBuffer.clear();
    m_rxRemaining = 0;
    m_expectedSeq = 1;
}

QString ObdDtcClient::decodeDtc(quint8 a, quint8 b)
{
    static const char letters[4] = {'P', 'C', 'B', 'U'};
    QString code;
    code += QChar(letters[(a >> 6) & 0x03]);
    code += hexDigit((a >> 4) & 0x03);
    code += hexDigit(a & 0x0F);
    code += hexDigit((b >> 4) & 0x0F);
    code += hexDigit(b & 0x0F);
    return code;
}

QString ObdDtcClient::describeDtc(const QString &code)
{
    // Curated descriptions for common generic codes. This is not a full DTC
    // database (which is enormous); unknown codes fall back to a subsystem hint
    // derived from the code structure.
    static const QHash<QString, QString> known = {
        {"P0101", "Mass Air Flow Circuit Range/Performance"},
        {"P0102", "Mass Air Flow Circuit Low Input"},
        {"P0113", "Intake Air Temperature Circuit High Input"},
        {"P0117", "Engine Coolant Temperature Circuit Low"},
        {"P0128", "Coolant Thermostat (below regulating temperature)"},
        {"P0131", "O2 Sensor Circuit Low Voltage (Bank 1 Sensor 1)"},
        {"P0135", "O2 Sensor Heater Circuit (Bank 1 Sensor 1)"},
        {"P0171", "System Too Lean (Bank 1)"},
        {"P0172", "System Too Rich (Bank 1)"},
        {"P0174", "System Too Lean (Bank 2)"},
        {"P0300", "Random/Multiple Cylinder Misfire Detected"},
        {"P0301", "Cylinder 1 Misfire Detected"},
        {"P0302", "Cylinder 2 Misfire Detected"},
        {"P0303", "Cylinder 3 Misfire Detected"},
        {"P0304", "Cylinder 4 Misfire Detected"},
        {"P0305", "Cylinder 5 Misfire Detected"},
        {"P0306", "Cylinder 6 Misfire Detected"},
        {"P0325", "Knock Sensor 1 Circuit (Bank 1)"},
        {"P0340", "Camshaft Position Sensor Circuit"},
        {"P0401", "Exhaust Gas Recirculation Flow Insufficient"},
        {"P0420", "Catalyst System Efficiency Below Threshold (Bank 1)"},
        {"P0430", "Catalyst System Efficiency Below Threshold (Bank 2)"},
        {"P0440", "Evaporative Emission System Malfunction"},
        {"P0442", "Evaporative Emission System Leak Detected (small leak)"},
        {"P0446", "Evaporative Emission System Vent Control Circuit"},
        {"P0455", "Evaporative Emission System Leak Detected (large leak)"},
        {"P0500", "Vehicle Speed Sensor Malfunction"},
        {"P0505", "Idle Air Control System Malfunction"},
        {"P0562", "System Voltage Low"},
        {"P0563", "System Voltage High"},
        {"U0100", "Lost Communication With ECM/PCM"},
        {"U0121", "Lost Communication With ABS Control Module"},
    };

    const auto it = known.constFind(code);
    if (it != known.constEnd())
        return it.value();

    if (code.isEmpty())
        return QString();

    // Fallback: broad category from the first letter, plus generic/manufacturer
    // hint from the second character (0/2/3 = generic, 1 = manufacturer).
    QString category;
    switch (code.at(0).toLatin1()) {
    case 'P': category = "Powertrain"; break;
    case 'C': category = "Chassis"; break;
    case 'B': category = "Body"; break;
    case 'U': category = "Network/Communication"; break;
    default: category = "Unknown"; break;
    }

    QString scope = "generic";
    if (code.size() > 1 && code.at(1) == QChar('1'))
        scope = "manufacturer-specific";

    return QString("%1 %2 code (no description on file)").arg(category, scope);
}
