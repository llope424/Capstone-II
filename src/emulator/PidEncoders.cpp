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
    {0x04, 1}, {0x05, 1}, {0x06, 1}, {0x0A, 1}, {0x0C, 2},
    {0x0D, 1}, {0x0F, 1}, {0x11, 1}, {0x14, 2}, {0x42, 2},
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
    case 0x06: return one((v + 100.0) * 128.0 / 100.0);// short-term fuel trim %
    case 0x0A: return one(v / 3.0);                     // fuel pressure kPa
    case 0x0C: return two(v * 4.0);                     // engine rpm
    case 0x0D: return one(v);                           // vehicle speed km/h
    case 0x0F: return one(v + 40.0);                   // intake air temp degC
    case 0x11: return one(v * 255.0 / 100.0);          // throttle %
    case 0x14: {                                        // O2 voltage (byte B unused -> 0xFF)
        QByteArray b(2, char(0));
        b[0] = char(clamp8(v * 200.0));
        b[1] = char(0xFF);
        return b;
    }
    case 0x42: return two(v * 1000.0);                 // control module voltage V
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
