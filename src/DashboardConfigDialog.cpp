#include "DashboardConfigDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

DashboardConfigDialog::DashboardConfigDialog(const QVector<QPair<int, QString>> &available,
                                             const QList<int> &selected, int columns,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Configure Dashboard");

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Checked parameters appear as gauges, top to bottom\n"
                                 "matching left to right on the dashboard."));

    m_list = new QListWidget(this);

    // Selected PIDs first, in their saved order, then the rest unchecked.
    auto addItem = [this](int pid, const QString &name, bool checked) {
        auto *item = new QListWidgetItem(name, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, pid);
    };
    for (int pid : selected) {
        for (const auto &entry : available)
            if (entry.first == pid) {
                addItem(pid, entry.second, true);
                break;
            }
    }
    for (const auto &entry : available)
        if (!selected.contains(entry.first))
            addItem(entry.first, entry.second, false);

    auto *listRow = new QHBoxLayout();
    listRow->addWidget(m_list, 1);

    auto *orderButtons = new QVBoxLayout();
    auto *upButton = new QPushButton("Move &Up", this);
    auto *downButton = new QPushButton("Move &Down", this);
    connect(upButton, &QPushButton::clicked, this, [this]() { moveCurrentRow(-1); });
    connect(downButton, &QPushButton::clicked, this, [this]() { moveCurrentRow(+1); });
    orderButtons->addWidget(upButton);
    orderButtons->addWidget(downButton);
    orderButtons->addStretch();
    listRow->addLayout(orderButtons);
    layout->addLayout(listRow, 1);

    auto *columnsRow = new QHBoxLayout();
    columnsRow->addWidget(new QLabel("Gauges per row:"));
    m_columnsSpin = new QSpinBox(this);
    m_columnsSpin->setRange(1, 6);
    m_columnsSpin->setValue(qBound(1, columns, 6));
    columnsRow->addWidget(m_columnsSpin);
    columnsRow->addStretch();
    layout->addLayout(columnsRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(380, 420);
}

void DashboardConfigDialog::moveCurrentRow(int delta)
{
    const int row = m_list->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_list->count())
        return;
    QListWidgetItem *item = m_list->takeItem(row);
    m_list->insertItem(target, item);
    m_list->setCurrentRow(target);
}

QList<int> DashboardConfigDialog::selectedPids() const
{
    QList<int> pids;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem *item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            pids << item->data(Qt::UserRole).toInt();
    }
    return pids;
}

int DashboardConfigDialog::columns() const
{
    return m_columnsSpin->value();
}
