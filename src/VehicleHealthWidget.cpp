#include "VehicleHealthWidget.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>

VehicleHealthWidget::VehicleHealthWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    auto *top = new QHBoxLayout();
    auto *title = new QLabel("Vehicle Health Report");
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    top->addWidget(title);
    top->addStretch();

    m_refreshButton = new QPushButton("Generate Report");
    m_exportButton = new QPushButton("Export Text...");
    connect(m_refreshButton, &QPushButton::clicked, this, &VehicleHealthWidget::refreshReport);
    connect(m_exportButton, &QPushButton::clicked, this, &VehicleHealthWidget::exportTextReport);
    top->addWidget(m_refreshButton);
    top->addWidget(m_exportButton);
    root->addLayout(top);

    auto *scoreRow = new QHBoxLayout();
    m_scoreLabel = new QLabel("100 / 100");
    QFont scoreFont = m_scoreLabel->font();
    scoreFont.setPointSize(scoreFont.pointSize() + 10);
    scoreFont.setBold(true);
    m_scoreLabel->setFont(scoreFont);
    m_ratingLabel = new QLabel("Excellent");
    QFont ratingFont = m_ratingLabel->font();
    ratingFont.setPointSize(ratingFont.pointSize() + 4);
    ratingFont.setBold(true);
    m_ratingLabel->setFont(ratingFont);
    scoreRow->addWidget(m_scoreLabel);
    scoreRow->addWidget(m_ratingLabel);
    scoreRow->addStretch();
    root->addLayout(scoreRow);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setWordWrap(true);
    root->addWidget(m_summaryLabel);

    root->addWidget(new QLabel("System Status"));
    m_systemTable = new QTableWidget(0, 2);
    m_systemTable->setHorizontalHeaderLabels({"System", "Status"});
    m_systemTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_systemTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_systemTable->verticalHeader()->setVisible(false);
    m_systemTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_systemTable);

    auto *lists = new QHBoxLayout();
    auto *warningColumn = new QVBoxLayout();
    warningColumn->addWidget(new QLabel("Warnings"));
    m_warningList = new QListWidget();
    warningColumn->addWidget(m_warningList);
    lists->addLayout(warningColumn);

    auto *recommendationColumn = new QVBoxLayout();
    recommendationColumn->addWidget(new QLabel("Recommendations"));
    m_recommendationList = new QListWidget();
    recommendationColumn->addWidget(m_recommendationList);
    lists->addLayout(recommendationColumn);
    root->addLayout(lists, 1);

    refreshReport();
}

void VehicleHealthWidget::updatePid(quint8 pid, double value)
{
    m_analyzer.updatePid(pid, value);
}

void VehicleHealthWidget::updateDtcs(quint8 mode, const QStringList &codes)
{
    m_analyzer.updateDtcs(mode, codes);
    refreshReport();
}

void VehicleHealthWidget::clearDtcs()
{
    m_analyzer.clearDtcs();
    refreshReport();
}

void VehicleHealthWidget::resetReport()
{
    m_analyzer.reset();
    refreshReport();
}

void VehicleHealthWidget::refreshReport()
{
    displayReport(m_analyzer.analyze());
}

void VehicleHealthWidget::displayReport(const VehicleHealthReport &report)
{
    m_scoreLabel->setText(QString("%1 / 100").arg(report.score));
    m_ratingLabel->setText(report.rating);
    m_summaryLabel->setText(report.summary);

    m_systemTable->setRowCount(0);
    QStringList systems = report.systemStatus.keys();
    systems.sort(Qt::CaseInsensitive);
    for (const QString &system : systems) {
        const int row = m_systemTable->rowCount();
        m_systemTable->insertRow(row);
        m_systemTable->setItem(row, 0, new QTableWidgetItem(system));
        m_systemTable->setItem(row, 1, new QTableWidgetItem(report.systemStatus.value(system)));
    }

    m_warningList->clear();
    if (report.warnings.isEmpty()) m_warningList->addItem("No warnings detected.");
    else m_warningList->addItems(report.warnings);

    m_recommendationList->clear();
    m_recommendationList->addItems(report.recommendations);
}

QString VehicleHealthWidget::buildTextReport(const VehicleHealthReport &report) const
{
    QString text;
    QTextStream out(&text);
    out << "OBD SUITE - VEHICLE HEALTH REPORT\n";
    out << "Generated: " << report.generatedAt.toString(Qt::ISODate) << "\n\n";
    out << "OVERALL SCORE: " << report.score << "/100 - " << report.rating << "\n";
    out << report.summary << "\n\n";

    out << "SYSTEM STATUS\n";
    QStringList systems = report.systemStatus.keys();
    systems.sort(Qt::CaseInsensitive);
    for (const QString &system : systems)
        out << "- " << system << ": " << report.systemStatus.value(system) << "\n";

    out << "\nDIAGNOSTIC TROUBLE CODES\n";
    out << "Stored: " << (report.storedDtcs.isEmpty() ? "None" : report.storedDtcs.join(", ")) << "\n";
    out << "Pending: " << (report.pendingDtcs.isEmpty() ? "None" : report.pendingDtcs.join(", ")) << "\n";
    out << "Permanent: " << (report.permanentDtcs.isEmpty() ? "None" : report.permanentDtcs.join(", ")) << "\n";

    out << "\nWARNINGS\n";
    if (report.warnings.isEmpty()) out << "- None\n";
    else for (const QString &warning : report.warnings) out << "- " << warning << "\n";

    out << "\nRECOMMENDATIONS\n";
    for (const QString &recommendation : report.recommendations)
        out << "- " << recommendation << "\n";

    out << "\nNote: This report interprets available generic OBD-II data and is not a replacement for a professional inspection.\n";
    return text;
}

void VehicleHealthWidget::exportTextReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Vehicle Health Report", "vehicle-health-report.txt", "Text Files (*.txt)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Failed", "Unable to create the report file.");
        return;
    }

    QTextStream out(&file);
    out << buildTextReport(m_analyzer.analyze());
    file.close();
    QMessageBox::information(this, "Report Exported", "Vehicle health report saved successfully.");
}
