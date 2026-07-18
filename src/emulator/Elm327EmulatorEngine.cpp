#include "Elm327EmulatorEngine.h"

#include "ObdMessageModel.h"
#include "PidEncoders.h"

#include <QRegularExpression>

namespace {

// Inverse of ObdDtcClient::decodeDtc: "P0301" -> {0x03,0x01}. First char selects
// P/C/B/U (top two bits of byte A); the four hex digits fill the remaining nibbles.
QByteArray encodeDtc(const QString &code)
{
    QByteArray out(2, char(0));
    if (code.size() < 5)
        return out;
    static const QString letters = QStringLiteral("PCBU");
    int l = letters.indexOf(code.at(0).toUpper());
    if (l < 0)
        l = 0;
    const auto hex = [](QChar c) { return QString(c).toInt(nullptr, 16); };
    const int d1 = hex(code.at(1)) & 0x3;
    const int d2 = hex(code.at(2)) & 0xF;
    const int d3 = hex(code.at(3)) & 0xF;
    const int d4 = hex(code.at(4)) & 0xF;
    out[0] = char((l << 6) | (d1 << 4) | d2);
    out[1] = char((d3 << 4) | d4);
    return out;
}

} // namespace

Elm327EmulatorEngine::Elm327EmulatorEngine(ObdMessageModel *model, QObject *parent)
    : QObject(parent), m_model(model)
{
}

void Elm327EmulatorEngine::reset()
{
    m_echo = false;
    m_headers = false;
    m_linefeeds = false;
    m_spaces = true;
}

QByteArray Elm327EmulatorEngine::handleLine(const QByteArray &line)
{
    // ELM327 is case-insensitive and ignores spaces within a command.
    const QByteArray cmd = line.toUpper().replace(' ', "");

    QByteArray payload;
    if (cmd == "ATZ" || cmd == "ATWS") {
        reset();                     // full/warm reset clears the toggle state
        payload = "ELM327 v1.5";
    } else if (cmd == "ATD") {
        reset();                     // restore all defaults
        payload = "OK";
    } else if (cmd == "ATI") {
        payload = "ELM327 v1.5";     // identify (no reset)
    } else if (cmd == "AT@1") {
        payload = "OBDII to RS232 Interpreter";
    } else if (cmd == "ATRV") {
        payload = "12.3V";           // battery voltage
    } else if (cmd == "ATDPN") {
        payload = "A6";              // auto, protocol 6 = ISO 15765-4 CAN 11/500
    } else if (cmd == "ATDP") {
        payload = "AUTO, ISO 15765-4 (CAN 11/500)";
    } else if (cmd == "ATE0") {
        m_echo = false;
        payload = "OK";
    } else if (cmd == "ATE1") {
        m_echo = true;
        payload = "OK";
    } else if (cmd == "ATL0") {
        m_linefeeds = false;
        payload = "OK";
    } else if (cmd == "ATL1") {
        m_linefeeds = true;
        payload = "OK";
    } else if (cmd == "ATH0") {
        m_headers = false;
        payload = "OK";
    } else if (cmd == "ATH1") {
        m_headers = true;
        payload = "OK";
    } else if (cmd == "ATS0") {
        m_spaces = false;
        payload = "OK";
    } else if (cmd == "ATS1") {
        m_spaces = true;
        payload = "OK";
    } else if (cmd.startsWith("ATSP") || cmd.startsWith("ATAT") || cmd.startsWith("ATCAF")
               || cmd.startsWith("ATST") || cmd.startsWith("ATSH") || cmd.startsWith("ATCRA")
               || cmd.startsWith("ATFC") || cmd.startsWith("ATAL") || cmd.startsWith("ATNL")
               || cmd.startsWith("ATPP") || cmd.startsWith("ATTP") || cmd.startsWith("ATPC")
               || cmd.startsWith("ATBI") || cmd.startsWith("ATM")) {
        // Recognized AT commands we accept without a special reply.
        payload = "OK";
    } else {
        payload = handleObd(cmd);    // OBD request (Mode 01 now; 03/04/09 later)
    }

    // Reply framing: optional echo of the command, the payload, then the prompt.
    const QByteArray term = lineTerm();
    QByteArray reply;
    if (m_echo)
        reply += line + term;
    reply += payload + term + term + ">";

    emit exchanged(line, reply);
    return reply;
}

QByteArray Elm327EmulatorEngine::handleObd(const QByteArray &cmd)
{
    // Mode 01 = current/live data. (Modes 03/04/09 are added in later tasks.)
    if (cmd.startsWith("01") && cmd.size() >= 4) {
        bool ok = false;
        const quint8 pid = quint8(cmd.mid(2, 2).toInt(&ok, 16));
        if (ok)
            return mode01(pid);
    } else if (cmd.startsWith("02") && cmd.size() >= 4) {
        bool ok = false;
        const quint8 pid = quint8(cmd.mid(2, 2).toInt(&ok, 16));
        if (ok)
            return mode02(pid);
    } else if (cmd.startsWith("03")) {
        return dtcResponse(0x43, m_model ? m_model->storedDtcs() : QStringList());
    } else if (cmd.startsWith("07")) {
        return dtcResponse(0x47, m_model ? m_model->pendingDtcs() : QStringList());
    } else if (cmd.startsWith("0A")) {
        return dtcResponse(0x4A, m_model ? m_model->permanentDtcs() : QStringList());
    } else if (cmd.startsWith("04")) {
        if (m_model)
            m_model->clearDtcs();
        return "44";                 // clear acknowledged
    } else if (cmd.startsWith("09") && cmd.size() >= 4) {
        bool ok = false;
        const quint8 pid = quint8(cmd.mid(2, 2).toInt(&ok, 16));
        if (ok)
            return mode09(pid);
    }

    // A well-formed (all-hex) request we don't implement is answered "NO DATA",
    // exactly as a vehicle with no such data would; anything else is a syntax
    // error ("?"), matching real ELM327 behaviour.
    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-Fa-f]+$"));
    if (!cmd.isEmpty() && hexOnly.match(QString::fromLatin1(cmd)).hasMatch())
        return "NO DATA";
    return "?";
}

QByteArray Elm327EmulatorEngine::mode02(quint8 pid)
{
    // SAE J1979 Mode 02 (freeze frame, frame 00). PID 02 carries the DTC that
    // triggered the capture (00 00 = nothing stored); data PIDs reuse the
    // Mode 01 raw encodings with the frame-number byte inserted. The frame is
    // "captured" when the scenario has a stored DTC, and the snapshot simply
    // reflects the model's current PID values.
    const QStringList stored = m_model ? m_model->storedDtcs() : QStringList();

    QByteArray head;
    head += char(0x42);
    head += char(pid);
    head += char(0x00); // frame number

    if (pid == 0x02) {
        const QByteArray dtc =
            stored.isEmpty() ? QByteArray(2, char(0x00)) : encodeDtc(stored.first());
        return formatBytes(head + dtc);
    }
    if (stored.isEmpty())
        return "NO DATA"; // no freeze frame captured
    const QByteArray raw = m_model ? m_model->pidRaw(pid) : QByteArray();
    if (raw.isEmpty())
        return "NO DATA"; // PID not part of the captured frame
    return formatBytes(head + raw);
}

QByteArray Elm327EmulatorEngine::mode09(quint8 pid)
{
    if (pid == 0x02) {               // VIN: one 17-char item
        const QByteArray vin = (m_model ? m_model->vin() : QString()).toLatin1();
        return multiFrame(0x02, 1, vin);
    }
    if (pid == 0x04) {               // calibration IDs: 16-byte NUL-padded items
        const QStringList ids = m_model ? m_model->calIds() : QStringList();
        QByteArray data;
        for (const QString &id : ids)
            data += id.toLatin1().left(16).leftJustified(16, '\0');
        return multiFrame(0x04, quint8(ids.size()), data);
    }
    return "NO DATA";
}

QByteArray Elm327EmulatorEngine::multiFrame(quint8 pid, quint8 nodi, const QByteArray &data) const
{
    QByteArray payload;
    payload += char(0x49);
    payload += char(pid);
    payload += char(nodi);
    payload += data;

    // ELM327-style multi-line output: a total-length line, then "N: <=7 bytes>".
    // The app reassembles by concatenating hex tokens, so the grouping is cosmetic.
    QByteArray out = QByteArray::number(payload.size(), 16).rightJustified(3, '0').toUpper();
    for (int i = 0; i < payload.size(); i += 7) {
        out += lineTerm();
        out += QByteArray::number(i / 7, 16).toUpper();
        out += ':';
        if (m_spaces)
            out += ' ';
        out += formatBytes(payload.mid(i, 7));
    }
    return out;
}

QByteArray Elm327EmulatorEngine::lineTerm() const
{
    return m_linefeeds ? QByteArray("\r\n") : QByteArray("\r");
}

QByteArray Elm327EmulatorEngine::dtcResponse(quint8 service, const QStringList &codes)
{
    // "<service> <count> <2-byte code>...", e.g. "43 02 03 01 04 20".
    QByteArray bytes;
    bytes += char(service);
    bytes += char(codes.size() & 0xFF);
    for (const QString &c : codes)
        bytes += encodeDtc(c);
    return formatBytes(bytes);
}

QByteArray Elm327EmulatorEngine::mode01(quint8 pid)
{
    // 01 00/20/40/... are support-range queries answered with a 4-byte bitmask.
    if ((pid & 0x1F) == 0)
        return formatBytes(QByteArray(1, char(0x41)) + char(pid) + supportedMask(pid));

    QByteArray raw = m_model ? m_model->pidRaw(pid) : QByteArray();
    if (raw.isEmpty()) {
        const int n = PidEncoders::dataBytes(pid);
        if (n <= 0)
            return "NO DATA";           // PID not supported / no value
        raw = QByteArray(n, char(0));   // supported but unset -> zeros
    }
    return formatBytes(QByteArray(1, char(0x41)) + char(pid) + raw);
}

QByteArray Elm327EmulatorEngine::supportedMask(quint8 base) const
{
    // Bit (31-idx) of a 32-bit value marks PID base+idx+1 as supported; the lowest
    // bit marks base+0x20 (i.e. "the next range is supported"), chaining queries.
    const int hi = int(base) + 0x20;
    quint32 bits = 0;
    bool more = false;
    for (quint8 p : PidEncoders::supportedPids()) {
        if (p > base && int(p) <= hi)
            bits |= (1u << (31 - (int(p) - int(base) - 1)));
        else if (int(p) > hi)
            more = true;
    }
    if (more)
        bits |= 1u;

    QByteArray b(4, char(0));
    b[0] = char((bits >> 24) & 0xFF);
    b[1] = char((bits >> 16) & 0xFF);
    b[2] = char((bits >> 8) & 0xFF);
    b[3] = char(bits & 0xFF);
    return b;
}

QByteArray Elm327EmulatorEngine::formatBytes(const QByteArray &bytes) const
{
    QByteArray out;
    for (int i = 0; i < bytes.size(); ++i) {
        if (i && m_spaces)
            out += ' ';
        out += QByteArray(1, bytes[i]).toHex().toUpper();
    }
    return out;
}
