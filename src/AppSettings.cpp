#include "AppSettings.h"

#include <QSettings>
#include <QVariant>

namespace {
// The default dashboard mirrors the original fixed layout, so upgrading users
// see no change until they customize.
const QList<int> kDefaultDashboardPids = {0x0C, 0x0D, 0x05, 0x04, 0x11, 0x0F, 0x42, 0x0A};
constexpr int kDefaultColumns = 4;
constexpr int kDefaultPollMs = 250;
}

namespace AppSettings
{

bool darkTheme()
{
    return QSettings().value("appearance/darkTheme", false).toBool();
}

void setDarkTheme(bool dark)
{
    QSettings().setValue("appearance/darkTheme", dark);
}

bool imperialUnits()
{
    return QSettings().value("units/imperial", false).toBool();
}

void setImperialUnits(bool imperial)
{
    QSettings().setValue("units/imperial", imperial);
}

int pollIntervalMs()
{
    return QSettings().value("liveData/pollIntervalMs", kDefaultPollMs).toInt();
}

void setPollIntervalMs(int ms)
{
    QSettings().setValue("liveData/pollIntervalMs", ms);
}

QList<int> dashboardPids()
{
    const QVariant v = QSettings().value("dashboard/pids");
    if (!v.isValid())
        return kDefaultDashboardPids;
    QList<int> pids;
    const QVariantList list = v.toList();
    for (const QVariant &item : list)
        pids << item.toInt();
    return pids;
}

void setDashboardPids(const QList<int> &pids)
{
    QVariantList list;
    for (int pid : pids)
        list << pid;
    QSettings().setValue("dashboard/pids", list);
}

int dashboardColumns()
{
    return QSettings().value("dashboard/columns", kDefaultColumns).toInt();
}

void setDashboardColumns(int columns)
{
    QSettings().setValue("dashboard/columns", columns);
}

QByteArray windowGeometry()
{
    return QSettings().value("window/geometry").toByteArray();
}

void setWindowGeometry(const QByteArray &geometry)
{
    QSettings().setValue("window/geometry", geometry);
}

}
