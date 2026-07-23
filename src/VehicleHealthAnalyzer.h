#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

struct VehicleHealthReport
{
    int score = 100;
    QString rating = "Excellent";
    QString summary;
    QStringList warnings;
    QStringList recommendations;
    QHash<QString, QString> systemStatus;
    QHash<quint8, double> pidValues;
    QStringList storedDtcs;
    QStringList pendingDtcs;
    QStringList permanentDtcs;
    QDateTime generatedAt = QDateTime::currentDateTime();
};

class VehicleHealthAnalyzer
{
public:
    void updatePid(quint8 pid, double value);
    void updateDtcs(quint8 mode, const QStringList &codes);
    void clearDtcs();
    void reset();

    VehicleHealthReport analyze() const;

private:
    QHash<quint8, double> m_pidValues;
    QStringList m_storedDtcs;
    QStringList m_pendingDtcs;
    QStringList m_permanentDtcs;
};
