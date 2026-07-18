#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>

class ObdMessageModel;

// The native C++ ELM327 emulator engine. Transport-agnostic and synchronous: feed
// it one request line (a command without its trailing CR) via handleLine() and it
// returns the full reply bytes the adapter would send, terminated with the ELM327
// prompt. It tracks the small amount of ELM state the client can set (echo,
// headers, ...) so replies match what Elm327Connection expects to parse.
//
// Reply framing: payload lines joined by "\r", then "\r>" (the prompt). Echo
// defaults OFF (deterministic; the app sends ATE0 anyway); ATE1 turns it on.
class Elm327EmulatorEngine : public QObject
{
    Q_OBJECT

public:
    explicit Elm327EmulatorEngine(ObdMessageModel *model, QObject *parent = nullptr);

    QByteArray handleLine(const QByteArray &line);
    void reset(); // ATZ state

signals:
    void exchanged(const QByteArray &rx, const QByteArray &tx);

private:
    QByteArray handleObd(const QByteArray &cmd);  // non-AT (OBD) requests
    QByteArray mode01(quint8 pid);                // live data / support masks
    QByteArray mode02(quint8 pid);                // freeze frame (trigger DTC + snapshot)
    QByteArray dtcResponse(quint8 service, const QStringList &codes); // 03/07/0A
    QByteArray mode09(quint8 pid);                // VIN (02) / calibration IDs (04)
    QByteArray multiFrame(quint8 pid, quint8 nodi, const QByteArray &data) const;
    QByteArray supportedMask(quint8 base) const;  // 4-byte 01 00/20/40 bitmask
    QByteArray formatBytes(const QByteArray &bytes) const; // hex, honoring ATS
    QByteArray lineTerm() const;                  // CR, or CR/LF when ATL1

    // ELM327 state the client can toggle. Echo defaults OFF (a deliberate,
    // documented simplification for deterministic output; the app sends ATE0
    // regardless). Spaces default ON and linefeeds OFF, matching an ELM327 at
    // power-on, so OBD byte responses read like "41 0C 1A E0".
    ObdMessageModel *m_model;
    bool m_echo = false;
    bool m_headers = false;
    bool m_linefeeds = false;
    bool m_spaces = true;
};
