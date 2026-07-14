#pragma once

#include <QtGlobal>
#include <cstdint>

// A single decoded CAN frame as received from (or queued for) the scanner.
struct CanFrame
{
    quint32 timestampUs = 0; // microseconds, as reported by the scanner's clock
    quint32 id = 0;          // 11-bit or 29-bit CAN identifier (extended flag stripped out)
    bool extended = false;
    quint8 bus = 0;
    quint8 length = 0;       // 0-8
    quint8 data[8] = {0};
};
