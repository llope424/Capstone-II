#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QListWidget;
class QSpinBox;
QT_END_NAMESPACE

// Dashboard layout editor (SDD FR-6): choose which PIDs appear as gauges,
// their order, and the grid column count. The caller persists the result and
// rebuilds the gauge grid.
class DashboardConfigDialog : public QDialog
{
    Q_OBJECT

public:
    // available: gauge-capable PIDs as (pid, display name), in catalog order.
    // selected: PIDs currently shown, in display order.
    DashboardConfigDialog(const QVector<QPair<int, QString>> &available,
                          const QList<int> &selected, int columns,
                          QWidget *parent = nullptr);

    QList<int> selectedPids() const;
    int columns() const;

private:
    void moveCurrentRow(int delta);

    QListWidget *m_list;
    QSpinBox *m_columnsSpin;
};
