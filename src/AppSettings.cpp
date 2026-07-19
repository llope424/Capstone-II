#include "AppSettings.h"

#include <QHash>
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

QString styleName()
{
    QSettings settings;
    const QString stored = settings.value("appearance/style").toString();
    if (!stored.isEmpty())
        return stored;
    // Migration from the pre-styles boolean theme setting.
    return settings.value("appearance/darkTheme", false).toBool() ? QStringLiteral("Dark")
                                                                  : QStringLiteral("Light");
}

void setStyleName(const QString &name)
{
    QSettings().setValue("appearance/style", name);
}

QColor customStyleColor(const QString &role)
{
    static const QHash<QString, QString> defaults = {
        {"main", "#2B2E33"}, {"secondary", "#24272B"}, {"details", "#4CA6FF"}};
    const QString stored =
        QSettings().value("appearance/custom_" + role, defaults.value(role)).toString();
    const QColor color(stored);
    return color.isValid() ? color : QColor(defaults.value(role));
}

void setCustomStyleColor(const QString &role, const QColor &color)
{
    QSettings().setValue("appearance/custom_" + role, color.name());
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

bool autoReconnect()
{
    return QSettings().value("connection/autoReconnect", true).toBool();
}

void setAutoReconnect(bool on)
{
    QSettings().setValue("connection/autoReconnect", on);
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
