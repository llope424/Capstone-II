#pragma once

#include <QString>

// Display-layer unit conversion. PID decode formulas always produce SAE metric
// values (km/h, degrees C, kPa); when the user prefers imperial these helpers
// convert values and unit labels at the point of display. Units without an
// imperial counterpart (rpm, %, V) pass through unchanged.
namespace Units
{
// The unit label to display for a metric source unit.
QString displayUnit(const QString &metricUnit, bool imperial);

// A metric value converted for display (identity when imperial is false or
// the unit has no conversion).
double display(double metricValue, const QString &metricUnit, bool imperial);
}
