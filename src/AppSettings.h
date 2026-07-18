#pragma once

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>

// Persistent user preferences, stored via QSettings under the ObdSuite
// organization/application names set in main(). Every accessor returns the
// stored value or a sensible default, so callers never need to special-case
// a fresh install.
namespace AppSettings
{
// Appearance: the active style preset name (see AppStyle::presetNames()) and
// the three user-chosen colors backing the "Custom" preset.
QString styleName();
void setStyleName(const QString &name);
QColor customStyleColor(const QString &role); // "main" | "secondary" | "details"
void setCustomStyleColor(const QString &role, const QColor &color);

// Units: false = metric (SAE default), true = imperial (mph / degF / psi).
bool imperialUnits();
void setImperialUnits(bool imperial);

// Live-data polling: milliseconds between successive PID requests.
int pollIntervalMs();
void setPollIntervalMs(int ms);

// Dashboard layout (SDD FR-6 "configurable dashboard layouts"): the ordered
// list of PIDs shown as gauges and the grid column count.
QList<int> dashboardPids();
void setDashboardPids(const QList<int> &pids);
int dashboardColumns();
void setDashboardColumns(int columns);

// Main window geometry across runs.
QByteArray windowGeometry();
void setWindowGeometry(const QByteArray &geometry);
}
