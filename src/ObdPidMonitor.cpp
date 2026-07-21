#include "ObdPidMonitor.h"

#include "GvretConnection.h"

namespace {
// OBD-II 11-bit addressing: requests go to the functional broadcast ID; ECUs
// answer somewhere in the physical response range.
constexpr quint32 kFunctionalRequestId = 0x7DF;
constexpr quint32 kResponseIdMin = 0x7E8;
constexpr quint32 kResponseIdMax = 0x7EF;
constexpr quint8 kModeCurrentData = 0x01;
constexpr quint8 kResponseModeMask = 0x40; // positive response = request mode + 0x40

QString degreesC()
{
    return QString(QChar(0x00B0)) + QLatin1Char('C');
}
}

QVector<PidDefinition> ObdPidMonitor::standardPids()
{
    const QString pct = QStringLiteral("%");
    const QString dC = degreesC();
    const QString deg = QString(QChar(0x00B0));
    const QString kPa = QStringLiteral("kPa");
    const QString volts = QStringLiteral("V");

    // 28 standard SAE J1979 Mode 01 parameters (SDD: at least 25 simultaneous
    // PIDs). Vehicles report which of these they implement via the support
    // bitmasks; unsupported ones are marked n/a in the UI.
    return {
        {0x04, "Engine Load",              pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x05, "Coolant Temperature",      dC,   1, [](const quint8 *d) { return d[0] - 40.0; }},
        {0x06, "Short-Term Fuel Trim B1",  pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 128.0 - 100.0; }},
        {0x07, "Long-Term Fuel Trim B1",   pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 128.0 - 100.0; }},
        {0x0A, "Fuel Pressure",            kPa,  1, [](const quint8 *d) { return d[0] * 3.0; }},
        {0x0B, "Intake Manifold Pressure", kPa,  1, [](const quint8 *d) { return double(d[0]); }},
        {0x0C, "Engine RPM",               QStringLiteral("rpm"),  2, [](const quint8 *d) { return (d[0] * 256.0 + d[1]) / 4.0; }},
        {0x0D, "Vehicle Speed",            QStringLiteral("km/h"), 1, [](const quint8 *d) { return double(d[0]); }},
        {0x0E, "Timing Advance",           deg,  1, [](const quint8 *d) { return d[0] / 2.0 - 64.0; }},
        {0x0F, "Intake Air Temperature",   dC,   1, [](const quint8 *d) { return d[0] - 40.0; }},
        {0x10, "MAF Air Flow Rate",        QStringLiteral("g/s"),  2, [](const quint8 *d) { return (d[0] * 256.0 + d[1]) / 100.0; }},
        {0x11, "Throttle Position",        pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x14, "O2 Sensor 1 Voltage",      volts, 2, [](const quint8 *d) { return d[0] / 200.0; }},
        {0x15, "O2 Sensor 2 Voltage",      volts, 2, [](const quint8 *d) { return d[0] / 200.0; }},
        {0x1F, "Run Time Since Start",     QStringLiteral("s"),    2, [](const quint8 *d) { return d[0] * 256.0 + d[1]; }},
        {0x21, "Distance With MIL On",     QStringLiteral("km"),   2, [](const quint8 *d) { return d[0] * 256.0 + d[1]; }},
        {0x2E, "Commanded EVAP Purge",     pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x2F, "Fuel Tank Level",          pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x33, "Barometric Pressure",      kPa,  1, [](const quint8 *d) { return double(d[0]); }},
        {0x42, "Control Module Voltage",   volts, 2, [](const quint8 *d) { return (d[0] * 256.0 + d[1]) / 1000.0; }},
        {0x43, "Absolute Load Value",      pct,  2, [](const quint8 *d) { return (d[0] * 256.0 + d[1]) * 100.0 / 255.0; }},
        {0x44, "Commanded Equiv. Ratio",   QStringLiteral("lambda"), 2, [](const quint8 *d) { return (d[0] * 256.0 + d[1]) / 32768.0; }},
        {0x45, "Relative Throttle Pos.",   pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x46, "Ambient Air Temperature",  dC,   1, [](const quint8 *d) { return d[0] - 40.0; }},
        {0x49, "Accelerator Pedal Pos.",   pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x4C, "Commanded Throttle",       pct,  1, [](const quint8 *d) { return d[0] * 100.0 / 255.0; }},
        {0x5C, "Engine Oil Temperature",   dC,   1, [](const quint8 *d) { return d[0] - 40.0; }},
        {0x5E, "Engine Fuel Rate",         QStringLiteral("L/h"),  2, [](const quint8 *d) { return (d[0] * 256.0 + d[1]) / 20.0; }},
    };
}

ObdPidMonitor::ObdPidMonitor(GvretConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    connect(m_connection, &GvretConnection::frameReceived, this, &ObdPidMonitor::onFrameReceived);
    connect(&m_pollTimer, &QTimer::timeout, this, &ObdPidMonitor::pollNext);

    m_pids = standardPids();
}

void ObdPidMonitor::start(int intervalMs)
{
    if (m_pids.isEmpty())
        return;
    m_pollIndex = 0;
    m_pollTimer.start(intervalMs);
}

void ObdPidMonitor::stop()
{
    m_pollTimer.stop();
}

bool ObdPidMonitor::isRunning() const
{
    return m_pollTimer.isActive();
}

void ObdPidMonitor::pollNext()
{
    if (m_pids.isEmpty() || !m_connection->isOpen())
        return;

    const PidDefinition &def = m_pids.at(m_pollIndex);
    m_pollIndex = (m_pollIndex + 1) % m_pids.size();

    // ISO-TP single frame: [PCI length=2][mode][pid], padded to 8 bytes.
    QByteArray payload;
    payload.append(char(0x02));
    payload.append(char(kModeCurrentData));
    payload.append(char(def.pid));
    while (payload.size() < 8)
        payload.append(char(0x00));

    m_connection->sendFrame(kFunctionalRequestId, false, 0, payload);
}

void ObdPidMonitor::onFrameReceived(const CanFrame &frame)
{
    if (frame.id < kResponseIdMin || frame.id > kResponseIdMax)
        return;
    if (frame.length < 3)
        return;

    // Single-frame ISO-TP: high nibble of byte 0 is the frame type (0 = single),
    // low nibble is the payload length. byte1 = response mode, byte2 = echoed PID.
    const quint8 pci = frame.data[0];
    if ((pci & 0xF0) != 0x00)
        return; // not a single frame; multi-frame responses aren't needed for these PIDs
    const int sfLength = pci & 0x0F;
    if (sfLength < 2 || sfLength > 7)
        return;

    if (frame.data[1] != (kModeCurrentData | kResponseModeMask))
        return;

    const quint8 pid = frame.data[2];

    for (const PidDefinition &def : m_pids) {
        if (def.pid != pid)
            continue;

        // Data bytes start at index 3; make sure the frame actually carried
        // enough of them for this PID's decoder before reading.
        const int available = sfLength - 2; // subtract mode + pid bytes
        if (available < def.dataBytes)
            return;
        if (3 + def.dataBytes > 8)
            return;

        emit pidUpdated(pid, def.decode(&frame.data[3]));
        return;
    }
}
