#pragma once

#include <QWidget>
#include "VehicleHealthAnalyzer.h"

class QLabel;
class QListWidget;
class QTableWidget;
class QPushButton;

class VehicleHealthWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VehicleHealthWidget(QWidget *parent = nullptr);

    void updatePid(quint8 pid, double value);
    void updateDtcs(quint8 mode, const QStringList &codes);
    void clearDtcs();
    void resetReport();

private slots:
    void refreshReport();
    void exportTextReport();

private:
    QString buildTextReport(const VehicleHealthReport &report) const;
    void displayReport(const VehicleHealthReport &report);

    VehicleHealthAnalyzer m_analyzer;
    QLabel *m_scoreLabel = nullptr;
    QLabel *m_ratingLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QTableWidget *m_systemTable = nullptr;
    QListWidget *m_warningList = nullptr;
    QListWidget *m_recommendationList = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_exportButton = nullptr;
};
