#include "PidEncoders.h"

#include <cmath>

namespace {

struct Def
{
    quint8 pid;
    int nbytes;
};

// PIDs the emulator answers, with their response data-byte counts. Must stay in
// step with the SAE J1979 decode formulas in ObdPidMonitor::standardPids().
const QVector<Def> kDefs = {
    {0x04, 1}, {0x05, 1}, {0x06, 1}, {0x07, 1}, {0x0A, 1}, {0x0B, 1},
    {0x0C, 2}, {0x0D, 1}, {0x0E, 1}, {0x0F, 1}, {0x10, 2}, {0x11, 1},
    {0x14, 2}, {0x15, 2}, {0x1F, 2}, {0x21, 2}, {0x2E, 1}, {0x2F, 1},
    {0x33, 1}, {0x42, 2}, {0x43, 2}, {0x44, 2}, {0x45, 1}, {0x46, 1},
    {0x49, 1}, {0x4C, 1}, {0x5C, 1}, {0x5E, 2},
};

quint8 clamp8(double v)
{
    long r = std::lround(v);
    if (r < 0)
        r = 0;
    if (r > 0xFF)
        r = 0xFF;
    return quint8(r);
}

QByteArray one(double v)
{
    QByteArray b(1, char(0));
    b[0] = char(clamp8(v));
    return b;
}

QByteArray two(double raw)
{
    long r = std::lround(raw);
    if (r < 0)
        r = 0;
    if (r > 0xFFFF)
        r = 0xFFFF;
    QByteArray b(2, char(0));
    b[0] = char((r >> 8) & 0xFF);
    b[1] = char(r & 0xFF);
    return b;
}

} // namespace

namespace PidEncoders {

QByteArray encode(quint8 pid, double v)
{
    switch (pid) {
    case 0x04: return one(v * 255.0 / 100.0);          // engine load %
    case 0x05: return one(v + 40.0);                   // coolant degC
    case 0x06:                                          // short/long fuel trim %
    case 0x07: return one((v + 100.0) * 128.0 / 100.0);
    case 0x0A: return one(v / 3.0);                     // fuel pressure kPa
    case 0x0B: return one(v);                           // intake manifold kPa
    case 0x0C: return two(v * 4.0);                     // engine rpm
    case 0x0D: return one(v);                           // vehicle speed km/h
    case 0x0E: return one((v + 64.0) * 2.0);            // timing advance deg
    case 0x0F: return one(v + 40.0);                   // intake air temp degC
    case 0x10: return two(v * 100.0);                  // MAF g/s
    case 0x11: return one(v * 255.0 / 100.0);          // throttle %
    case 0x14:                                          // O2 voltage (byte B unused -> 0xFF)
    case 0x15: {
        QByteArray b(2, char(0));
        b[0] = char(clamp8(v * 200.0));
        b[1] = char(0xFF);
        return b;
    }
    case 0x1F: return two(v);                          // run time s
    case 0x21: return two(v);                          // distance with MIL km
    case 0x2E:                                          // EVAP purge %
    case 0x2F: return one(v * 255.0 / 100.0);          // fuel level %
    case 0x33: return one(v);                           // barometric kPa
    case 0x42: return two(v * 1000.0);                 // control module voltage V
    case 0x43: return two(v * 255.0 / 100.0);          // absolute load %
    case 0x44: return two(v * 32768.0);                // equivalence ratio
    case 0x45: return one(v * 255.0 / 100.0);          // relative throttle %
    case 0x46: return one(v + 40.0);                   // ambient air degC
    case 0x49: return one(v * 255.0 / 100.0);          // accel pedal %
    case 0x4C: return one(v * 255.0 / 100.0);          // commanded throttle %
    case 0x5C: return one(v + 40.0);                   // oil temp degC
    case 0x5E: return two(v * 20.0);                   // fuel rate L/h
    default: return {};
    }
}

QVector<quint8> supportedPids()
{
    QVector<quint8> pids;
    for (const Def &d : kDefs)
        pids.append(d.pid);
    return pids;
}

int dataBytes(quint8 pid)
{
    for (const Def &d : kDefs)
        if (d.pid == pid)
            return d.nbytes;
    return 0;
}

} // namespace PidEncoders
