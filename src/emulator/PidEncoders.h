#pragma once

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

// Inverse of the SAE J1979 decode formulas used by ObdPidMonitor: turns an
// engineering value (rpm, km/h, degC, %, V, kPa) into the raw OBD Mode 01
// response bytes for a PID. This is what lets the emulator's editable data be
// entered in engineering units yet emitted as correct on-wire bytes. Kept free of
// any app dependency so it can be unit-tested in isolation.
namespace PidEncoders {

// Raw data bytes for the given Mode 01 PID; empty if the PID is not known here.
QByteArray encode(quint8 pid, double value);

// The Mode 01 PIDs the emulator can answer (drives the 01 00/20/40 support masks).
QVector<quint8> supportedPids();

// Number of data bytes in the PID's response, or 0 if the PID is unknown.
int dataBytes(quint8 pid);

} // namespace PidEncoders
