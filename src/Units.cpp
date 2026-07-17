#include "Units.h"

#include <QChar>

namespace {
QString degreesC() { return QString(QChar(0x00B0)) + QLatin1Char('C'); }
QString degreesF() { return QString(QChar(0x00B0)) + QLatin1Char('F'); }
}

namespace Units
{

QString displayUnit(const QString &metricUnit, bool imperial)
{
    if (!imperial)
        return metricUnit;
    if (metricUnit == QLatin1String("km/h"))
        return QStringLiteral("mph");
    if (metricUnit == degreesC())
        return degreesF();
    if (metricUnit == QLatin1String("kPa"))
        return QStringLiteral("psi");
    return metricUnit;
}

double display(double metricValue, const QString &metricUnit, bool imperial)
{
    if (!imperial)
        return metricValue;
    if (metricUnit == QLatin1String("km/h"))
        return metricValue * 0.6213712;
    if (metricUnit == degreesC())
        return metricValue * 9.0 / 5.0 + 32.0;
    if (metricUnit == QLatin1String("kPa"))
        return metricValue * 0.1450377;
    return metricValue;
}

}
