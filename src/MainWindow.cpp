#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "Elm327Connection.h"
#include "GaugeWidget.h"
#include "LiveChartWidget.h"
#include "NewConnectionDialog.h"
#include "ObdDtcClient.h"
#include "ObdPidMonitor.h"
#include "ObdVehicleInfo.h"
#include "ReportExporter.h"
#include "SessionLogger.h"
#include "VehicleStore.h"

namespace {
constexpr int kMaxDisplayedRows = 5000;

QString formatHexId(quint32 id, bool extended)
{
    return QStringLiteral("0x%1").arg(id, extended ? 8 : 3, 16, QChar('0')).toUpper();
}

QString formatHexBytes(const CanFrame &frame)
{
    QStringList parts;
    for (int i = 0; i < frame.length; ++i)
        parts << QStringLiteral("%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
    return parts.join(' ');
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("OBD Suite - Raw Traffic Viewer");
    resize(1000, 650);

    m_connection = new GvretConnection(this);
    connect(m_connection, &GvretConnection::connected, this, &MainWindow::onConnected);
    connect(m_connection, &GvretConnection::disconnected, this, &MainWindow::onDisconnected);
    connect(m_connection, &GvretConnection::frameReceived, this, &MainWindow::onFrameReceived);
    connect(m_connection, &GvretConnection::deviceInfoReceived, this, &MainWindow::onDeviceInfoReceived);
    connect(m_connection, &GvretConnection::busParamsReceived, this, &MainWindow::onBusParamsReceived);
    connect(m_connection, &GvretConnection::logMessage, this, &MainWindow::onLogMessage);

    m_pidMonitor = new ObdPidMonitor(m_connection, this);
    connect(m_pidMonitor, &ObdPidMonitor::pidUpdated, this, &MainWindow::onPidUpdated);

    m_dtcClient = new ObdDtcClient(m_connection, this);
    connect(m_dtcClient, &ObdDtcClient::dtcsReceived, this, &MainWindow::onDtcsReceived);
    connect(m_dtcClient, &ObdDtcClient::dtcsCleared, this, &MainWindow::onDtcsCleared);
    connect(m_dtcClient, &ObdDtcClient::logMessage, this, &MainWindow::onLogMessage);

    m_vehicleInfo = new ObdVehicleInfo(m_connection, this);
    connect(m_vehicleInfo, &ObdVehicleInfo::vinReceived, this, &MainWindow::onVinReceived);
    connect(m_vehicleInfo, &ObdVehicleInfo::calibrationIdsReceived, this, &MainWindow::onCalibrationIdsReceived);
    connect(m_vehicleInfo, &ObdVehicleInfo::logMessage, this, &MainWindow::onLogMessage);

    m_logger = new SessionLogger(this);
    connect(m_logger, &SessionLogger::frameReplayed, this, &MainWindow::onFrameReplayed);
    connect(m_logger, &SessionLogger::replayFinished, this, &MainWindow::onReplayFinished);
    connect(m_logger, &SessionLogger::logMessage, this, &MainWindow::onLogMessage);

    m_vehicleStore = new VehicleStore(this);

    // ELM327 commercial-adapter backend. It emits the same UI-facing signals as
    // the GVRET stack, so both feed the identical display slots.
    m_elm = new Elm327Connection(this);
    connect(m_elm, &Elm327Connection::connected, this, &MainWindow::onConnected);
    connect(m_elm, &Elm327Connection::disconnected, this, &MainWindow::onDisconnected);
    connect(m_elm, &Elm327Connection::frameReceived, this, &MainWindow::onFrameReceived);
    connect(m_elm, &Elm327Connection::pidUpdated, this, &MainWindow::onPidUpdated);
    connect(m_elm, &Elm327Connection::dtcsReceived, this, &MainWindow::onDtcsReceived);
    connect(m_elm, &Elm327Connection::dtcsCleared, this, &MainWindow::onDtcsCleared);
    connect(m_elm, &Elm327Connection::vinReceived, this, &MainWindow::onVinReceived);
    connect(m_elm, &Elm327Connection::calibrationIdsReceived, this, &MainWindow::onCalibrationIdsReceived);
    connect(m_elm, &Elm327Connection::logMessage, this, &MainWindow::onLogMessage);
    connect(m_elm, &Elm327Connection::deviceInfoReceived, this, [this](const QString &id) {
        m_firmwareText = id;
        m_deviceInfoLabel->setText("Adapter: " + id);
        m_firmwareValue->setText(id);
    });

    m_flushTimer.setInterval(100); // 10 Hz batch flush of buffered frames to the table
    connect(&m_flushTimer, &QTimer::timeout, this, &MainWindow::flushPendingFrames);
    m_flushTimer.start();

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);

    // --- Connection bar ---
    auto *connBar = new QHBoxLayout();

    m_connectButton = new QPushButton("New Connection...");
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);
    connBar->addWidget(m_connectButton);

    m_statusLabel = new QLabel("Disconnected");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #a33;");
    connBar->addWidget(m_statusLabel);

    connBar->addStretch();
    rootLayout->addLayout(connBar);

    m_deviceInfoLabel = new QLabel("No device info yet.");
    rootLayout->addWidget(m_deviceInfoLabel);

    auto *tabs = new QTabWidget(central);

    // --- Dashboard tab (gauges + live chart) ---
    buildDashboardTab(tabs);

    // --- Live Data tab ---
    auto *livePage = new QWidget();
    auto *liveLayout = new QVBoxLayout(livePage);

    auto *liveBar = new QHBoxLayout();
    m_monitorButton = new QPushButton("Start Monitoring");
    m_monitorButton->setEnabled(false);
    m_monitorButton->setToolTip("Continuously polls the standard OBD-II PIDs below and shows live decoded values.");
    connect(m_monitorButton, &QPushButton::clicked, this, &MainWindow::onMonitorButtonClicked);
    liveBar->addWidget(m_monitorButton);
    liveBar->addStretch();
    liveLayout->addLayout(liveBar);

    m_pidTable = new QTableWidget(0, 3, livePage);
    m_pidTable->setHorizontalHeaderLabels({"Parameter", "Value", "Unit"});
    m_pidTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pidTable->verticalHeader()->setVisible(false);
    m_pidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pidTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    liveLayout->addWidget(m_pidTable, 1);

    tabs->addTab(livePage, "Live Data");

    // --- Trouble Codes tab ---
    auto *dtcPage = new QWidget();
    auto *dtcLayout = new QVBoxLayout(dtcPage);

    auto *dtcBar = new QHBoxLayout();
    m_readStoredButton = new QPushButton("Read Stored");
    m_readPendingButton = new QPushButton("Read Pending");
    m_readPermanentButton = new QPushButton("Read Permanent");
    m_clearDtcButton = new QPushButton("Clear DTCs...");
    m_readStoredButton->setToolTip("Service 03 - confirmed trouble codes (MIL/check-engine codes).");
    m_readPendingButton->setToolTip("Service 07 - pending codes not yet confirmed.");
    m_readPermanentButton->setToolTip("Service 0A - permanent codes that cannot be cleared manually.");
    m_clearDtcButton->setToolTip("Service 04 - clears stored codes and turns off the MIL. Asks for confirmation.");
    connect(m_readStoredButton, &QPushButton::clicked, this, &MainWindow::onReadStoredClicked);
    connect(m_readPendingButton, &QPushButton::clicked, this, &MainWindow::onReadPendingClicked);
    connect(m_readPermanentButton, &QPushButton::clicked, this, &MainWindow::onReadPermanentClicked);
    connect(m_clearDtcButton, &QPushButton::clicked, this, &MainWindow::onClearDtcsClicked);
    dtcBar->addWidget(m_readStoredButton);
    dtcBar->addWidget(m_readPendingButton);
    dtcBar->addWidget(m_readPermanentButton);
    dtcBar->addStretch();
    dtcBar->addWidget(m_clearDtcButton);
    dtcLayout->addLayout(dtcBar);

    m_dtcTable = new QTableWidget(0, 3, dtcPage);
    m_dtcTable->setHorizontalHeaderLabels({"Code", "Status", "Description"});
    m_dtcTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_dtcTable->verticalHeader()->setVisible(false);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dtcLayout->addWidget(m_dtcTable, 1);

    setDtcButtonsEnabled(false);
    tabs->addTab(dtcPage, "Trouble Codes");

    // --- Raw Traffic tab ---
    auto *rawPage = new QWidget();
    auto *rawLayout = new QVBoxLayout(rawPage);

    auto *testBar = new QHBoxLayout();
    m_testRequestButton = new QPushButton("Send Test Request (Mode 01 PID 00)");
    m_testRequestButton->setEnabled(false);
    m_testRequestButton->setToolTip("Sends a standard OBD-II \"supported PIDs\" request to broadcast ID 0x7DF. "
                                     "A responding ECU should reply on 0x7E8-0x7EF.");
    connect(m_testRequestButton, &QPushButton::clicked, this, &MainWindow::onSendTestRequestClicked);
    testBar->addWidget(m_testRequestButton);
    testBar->addStretch();
    rawLayout->addLayout(testBar);

    m_frameTable = new QTableWidget(0, 6, rawPage);
    m_frameTable->setHorizontalHeaderLabels({"Time (s)", "Bus", "ID", "Ext", "DLC", "Data"});
    m_frameTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_frameTable->verticalHeader()->setVisible(false);
    m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    rawLayout->addWidget(m_frameTable, 1);

    auto *bottomBar = new QHBoxLayout();
    m_autoScrollCheck = new QCheckBox("Auto-scroll");
    m_autoScrollCheck->setChecked(true);
    bottomBar->addWidget(m_autoScrollCheck);

    m_clearButton = new QPushButton("Clear");
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    bottomBar->addWidget(m_clearButton);

    m_recordButton = new QPushButton("Record...");
    m_recordButton->setToolTip("Record all incoming frames to a CSV log file.");
    connect(m_recordButton, &QPushButton::clicked, this, &MainWindow::onRecordButtonClicked);
    bottomBar->addWidget(m_recordButton);

    m_replayButton = new QPushButton("Replay...");
    m_replayButton->setToolTip("Replay a previously recorded session into this view.");
    connect(m_replayButton, &QPushButton::clicked, this, &MainWindow::onReplayButtonClicked);
    bottomBar->addWidget(m_replayButton);

    bottomBar->addStretch();
    m_frameCountLabel = new QLabel("Frames: 0");
    bottomBar->addWidget(m_frameCountLabel);
    rawLayout->addLayout(bottomBar);

    tabs->addTab(rawPage, "Raw Traffic");

    // --- Vehicle Info tab ---
    buildVehicleInfoTab(tabs);

    // --- Vehicles tab (profiles + history) ---
    buildVehiclesTab(tabs);

    // --- Connection log (persistent, unlike the transient status bar) ---
    m_logView = new QPlainTextEdit(central);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setPlaceholderText("Connection log will appear here...");

    auto *splitter = new QSplitter(Qt::Vertical, central);
    splitter->addWidget(tabs);
    splitter->addWidget(m_logView);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(central);
    statusBar();

    buildMenus();
    buildLiveDataTable();
}

void MainWindow::buildVehicleInfoTab(QTabWidget *tabs)
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *bar = new QHBoxLayout();
    m_readVinButton = new QPushButton("Read VIN");
    m_readCalIdButton = new QPushButton("Read Calibration IDs");
    m_checkFirmwareButton = new QPushButton("Check Firmware / Updates");
    m_readVinButton->setToolTip("Mode 09 PID 02 - Vehicle Identification Number.");
    m_readCalIdButton->setToolTip("Mode 09 PID 04 - ECU calibration identifiers.");
    connect(m_readVinButton, &QPushButton::clicked, this, &MainWindow::onReadVinClicked);
    connect(m_readCalIdButton, &QPushButton::clicked, this, &MainWindow::onReadCalIdsClicked);
    connect(m_checkFirmwareButton, &QPushButton::clicked, this, &MainWindow::onCheckFirmwareClicked);
    bar->addWidget(m_readVinButton);
    bar->addWidget(m_readCalIdButton);
    bar->addStretch();
    bar->addWidget(m_checkFirmwareButton);
    layout->addLayout(bar);

    auto *form = new QFormLayout();
    m_vinValue = new QLabel("--");
    m_protocolValue = new QLabel("--");
    m_calIdValue = new QLabel("--");
    m_firmwareValue = new QLabel("--");
    m_vinValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_calIdValue->setWordWrap(true);
    form->addRow("VIN:", m_vinValue);
    form->addRow("Protocol:", m_protocolValue);
    form->addRow("Calibration IDs:", m_calIdValue);
    form->addRow("Scanner firmware:", m_firmwareValue);
    layout->addLayout(form);
    layout->addStretch();

    m_readVinButton->setEnabled(false);
    m_readCalIdButton->setEnabled(false);

    tabs->addTab(page, "Vehicle Info");
}

namespace {
// Modal editor for a vehicle profile's fields (not its history). Returns true if
// the user accepted; fills `profile` with the edited values.
bool editVehicleDialog(QWidget *parent, VehicleProfile &profile)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(profile.name.isEmpty() ? "Add Vehicle" : "Edit Vehicle");
    dialog.setMinimumWidth(360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();

    auto *nameEdit = new QLineEdit(profile.name);
    auto *vinEdit = new QLineEdit(profile.vin);
    auto *makeEdit = new QLineEdit(profile.make);
    auto *modelEdit = new QLineEdit(profile.model);
    auto *yearEdit = new QLineEdit(profile.year);
    auto *notesEdit = new QLineEdit(profile.notes);
    form->addRow("Name:", nameEdit);
    form->addRow("VIN:", vinEdit);
    form->addRow("Make:", makeEdit);
    form->addRow("Model:", modelEdit);
    form->addRow("Year:", yearEdit);
    form->addRow("Notes:", notesEdit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    profile.name = nameEdit->text().trimmed();
    profile.vin = vinEdit->text().trimmed();
    profile.make = makeEdit->text().trimmed();
    profile.model = modelEdit->text().trimmed();
    profile.year = yearEdit->text().trimmed();
    profile.notes = notesEdit->text().trimmed();
    if (profile.name.isEmpty())
        profile.name = profile.vin.isEmpty() ? "Unnamed vehicle" : profile.vin;
    return true;
}
} // namespace

void MainWindow::buildVehiclesTab(QTabWidget *tabs)
{
    auto *page = new QWidget();
    auto *outer = new QHBoxLayout(page);

    // Left: vehicle list + add/edit/delete.
    auto *leftCol = new QVBoxLayout();
    m_vehicleList = new QListWidget();
    connect(m_vehicleList, &QListWidget::currentRowChanged, this, &MainWindow::onVehicleSelectionChanged);
    leftCol->addWidget(m_vehicleList, 1);

    auto *listButtons = new QHBoxLayout();
    auto *addButton = new QPushButton("Add");
    m_editVehicleButton = new QPushButton("Edit");
    m_deleteVehicleButton = new QPushButton("Delete");
    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddVehicle);
    connect(m_editVehicleButton, &QPushButton::clicked, this, &MainWindow::onEditVehicle);
    connect(m_deleteVehicleButton, &QPushButton::clicked, this, &MainWindow::onDeleteVehicle);
    listButtons->addWidget(addButton);
    listButtons->addWidget(m_editVehicleButton);
    listButtons->addWidget(m_deleteVehicleButton);
    leftCol->addLayout(listButtons);
    outer->addLayout(leftCol, 1);

    // Right: details + history + save-session.
    auto *rightCol = new QVBoxLayout();
    m_vehicleDetails = new QLabel("Select or add a vehicle.");
    m_vehicleDetails->setWordWrap(true);
    m_vehicleDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightCol->addWidget(m_vehicleDetails);

    rightCol->addWidget(new QLabel("Diagnostic history:"));
    m_historyList = new QListWidget();
    rightCol->addWidget(m_historyList, 1);

    m_saveSessionButton = new QPushButton("Save Current Session to This Vehicle");
    m_saveSessionButton->setToolTip("Snapshots the current VIN and trouble codes into the selected vehicle's history.");
    connect(m_saveSessionButton, &QPushButton::clicked, this, &MainWindow::onSaveSessionToVehicle);
    rightCol->addWidget(m_saveSessionButton);
    outer->addLayout(rightCol, 2);

    tabs->addTab(page, "Vehicles");

    refreshVehicleList();
    onVehicleSelectionChanged();
}

void MainWindow::refreshVehicleList()
{
    const int prev = m_vehicleList->currentRow();
    m_vehicleList->clear();
    for (const VehicleProfile &v : m_vehicleStore->vehicles()) {
        QString label = v.name;
        if (!v.year.isEmpty() || !v.make.isEmpty() || !v.model.isEmpty())
            label += QString(" (%1 %2 %3)").arg(v.year, v.make, v.model).simplified();
        m_vehicleList->addItem(label.trimmed());
    }
    if (prev >= 0 && prev < m_vehicleList->count())
        m_vehicleList->setCurrentRow(prev);
    else if (m_vehicleList->count() > 0)
        m_vehicleList->setCurrentRow(0);
}

void MainWindow::onVehicleSelectionChanged()
{
    const int idx = m_vehicleList->currentRow();
    const bool valid = idx >= 0 && idx < m_vehicleStore->count();
    m_editVehicleButton->setEnabled(valid);
    m_deleteVehicleButton->setEnabled(valid);
    m_saveSessionButton->setEnabled(valid);

    m_historyList->clear();
    if (!valid) {
        m_vehicleDetails->setText("Select or add a vehicle.");
        return;
    }

    const VehicleProfile &v = m_vehicleStore->at(idx);
    m_vehicleDetails->setText(QString("<b>%1</b><br>VIN: %2<br>Make/Model/Year: %3 %4 %5<br>Notes: %6")
                                   .arg(v.name.toHtmlEscaped(),
                                        v.vin.isEmpty() ? "-" : v.vin.toHtmlEscaped(),
                                        v.make.toHtmlEscaped(), v.model.toHtmlEscaped(), v.year.toHtmlEscaped(),
                                        v.notes.isEmpty() ? "-" : v.notes.toHtmlEscaped()));
    for (const DiagnosticRecord &r : v.history) {
        const QString codes = r.dtcs.isEmpty() ? "no codes" : r.dtcs.join(", ");
        m_historyList->addItem(QString("%1  -  %2").arg(r.timestamp, codes));
    }
}

void MainWindow::onAddVehicle()
{
    VehicleProfile v;
    // Pre-fill VIN if we've read one this session.
    v.vin = m_vinText;
    if (!editVehicleDialog(this, v))
        return;
    const int idx = m_vehicleStore->addVehicle(v);
    refreshVehicleList();
    m_vehicleList->setCurrentRow(idx);
}

void MainWindow::onEditVehicle()
{
    const int idx = m_vehicleList->currentRow();
    if (idx < 0 || idx >= m_vehicleStore->count())
        return;
    VehicleProfile v = m_vehicleStore->at(idx);
    if (!editVehicleDialog(this, v))
        return;
    m_vehicleStore->updateVehicle(idx, v);
    refreshVehicleList();
    onVehicleSelectionChanged();
}

void MainWindow::onDeleteVehicle()
{
    const int idx = m_vehicleList->currentRow();
    if (idx < 0 || idx >= m_vehicleStore->count())
        return;
    const QString name = m_vehicleStore->at(idx).name;
    if (QMessageBox::question(this, "Delete Vehicle",
                              QString("Delete \"%1\" and its saved history?").arg(name))
        != QMessageBox::Yes)
        return;
    m_vehicleStore->removeVehicle(idx);
    refreshVehicleList();
    onVehicleSelectionChanged();
}

void MainWindow::onSaveSessionToVehicle()
{
    const int idx = m_vehicleList->currentRow();
    if (idx < 0 || idx >= m_vehicleStore->count())
        return;

    DiagnosticRecord rec;
    rec.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    for (int r = 0; r < m_dtcTable->rowCount(); ++r)
        if (m_dtcTable->item(r, 0))
            rec.dtcs << m_dtcTable->item(r, 0)->text();

    QStringList snapshot;
    for (int r = 0; r < m_pidTable->rowCount(); ++r) {
        auto *nameItem = m_pidTable->item(r, 0);
        auto *valItem = m_pidTable->item(r, 1);
        auto *unitItem = m_pidTable->item(r, 2);
        if (nameItem && valItem && valItem->text() != "--")
            snapshot << QString("%1=%2%3").arg(nameItem->text(), valItem->text(),
                                               unitItem ? unitItem->text() : QString());
    }
    rec.notes = snapshot.join("; ");

    m_vehicleStore->addRecord(idx, rec);
    onVehicleSelectionChanged();
    onLogMessage(QString("Saved session (%1 codes) to \"%2\".")
                     .arg(rec.dtcs.size())
                     .arg(m_vehicleStore->at(idx).name));
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu("&File");
    auto *exportAction = fileMenu->addAction("&Export Report...");
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportReport);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction("E&xit");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu("&View");
    auto *darkAction = viewMenu->addAction("&Dark Theme");
    darkAction->setCheckable(true);
    connect(darkAction, &QAction::toggled, this, &MainWindow::onToggleTheme);
}

void MainWindow::buildLiveDataTable()
{
    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();
    m_pidTable->setRowCount(defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        const PidDefinition &def = defs.at(i);
        m_pidTable->setItem(i, 0, new QTableWidgetItem(def.name));
        m_pidTable->setItem(i, 1, new QTableWidgetItem("--"));
        m_pidTable->setItem(i, 2, new QTableWidgetItem(def.unit));
        m_pidRow.insert(def.pid, i);
    }
}

void MainWindow::buildDashboardTab(QTabWidget *tabs)
{
    // Display ranges (and optional red-zone thresholds) for the gauges. Only PIDs
    // that read well as a dial get one; the rest still appear on the Live Data
    // tab and are selectable in the chart.
    struct GaugeConfig
    {
        quint8 pid;
        double min;
        double max;
        bool hasWarn;
        double warn;
    };
    static const QVector<GaugeConfig> configs = {
        {0x0C, 0, 8000, true, 6500},   // Engine RPM
        {0x0D, 0, 240, false, 0},      // Vehicle Speed
        {0x05, -40, 150, true, 105},   // Coolant Temperature
        {0x04, 0, 100, false, 0},      // Engine Load
        {0x11, 0, 100, false, 0},      // Throttle Position
        {0x0F, -40, 150, false, 0},    // Intake Air Temperature
        {0x42, 0, 16, false, 0},       // Control Module Voltage
        {0x0A, 0, 765, false, 0},      // Fuel Pressure
    };

    auto *dashPage = new QWidget();
    auto *dashLayout = new QVBoxLayout(dashPage);

    // Gauge grid inside a scroll area so it stays usable on small windows.
    auto *gaugeContainer = new QWidget();
    auto *grid = new QGridLayout(gaugeContainer);
    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();

    int col = 0, rowIdx = 0;
    const int columns = 4;
    for (const GaugeConfig &cfg : configs) {
        // Look up name/unit from the monitor's PID definitions.
        QString name, unit;
        for (const PidDefinition &def : defs) {
            if (def.pid == cfg.pid) {
                name = def.name;
                unit = def.unit;
                break;
            }
        }
        if (name.isEmpty())
            continue;

        auto *gauge = new GaugeWidget(name, unit, cfg.min, cfg.max, gaugeContainer);
        if (cfg.hasWarn)
            gauge->setWarnThreshold(cfg.warn);
        grid->addWidget(gauge, rowIdx, col);
        m_gauges.insert(cfg.pid, gauge);

        if (++col >= columns) {
            col = 0;
            ++rowIdx;
        }
    }

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(gaugeContainer);
    dashLayout->addWidget(scroll, 2);

    // Live chart with a PID selector.
    auto *chartBar = new QHBoxLayout();
    chartBar->addWidget(new QLabel("Chart:"));
    m_chartPidCombo = new QComboBox();
    for (const PidDefinition &def : defs)
        m_chartPidCombo->addItem(def.name, def.pid);
    chartBar->addWidget(m_chartPidCombo);
    chartBar->addStretch();
    dashLayout->addLayout(chartBar);

    m_chart = new LiveChartWidget(dashPage);
    dashLayout->addWidget(m_chart, 1);

    auto applyChartSelection = [this]() {
        const int idx = m_chartPidCombo->currentIndex();
        if (idx < 0)
            return;
        m_chart->setSeries(m_chartPidCombo->itemText(idx), QString());
    };
    connect(m_chartPidCombo, &QComboBox::currentIndexChanged, this, applyChartSelection);
    applyChartSelection();

    tabs->addTab(dashPage, "Dashboard");
}

void MainWindow::onConnectButtonClicked()
{
    if (m_connection->isOpen() || m_elm->isOpen()) {
        if (m_activeElm) {
            m_elm->stopMonitoring();
            m_elm->close();
        } else {
            m_pidMonitor->stop();
            m_connection->close();
        }
        m_monitorButton->setText("Start Monitoring");
        setConnectedUiState(false);
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet("font-weight: bold; color: #a33;");
        m_testRequestButton->setEnabled(false);
        m_monitorButton->setEnabled(false);
        return;
    }

    ConnectionParams params;
    if (!NewConnectionDialog::getConnectionParams(this, params))
        return; // user cancelled

    m_statusLabel->setText("Connecting...");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #b80;");
    m_deviceInfoLabel->setText("No device info yet.");

    const bool serial = params.transport == ConnectionParams::Transport::Serial;
    if (serial && params.serialPort.isEmpty()) {
        onLogMessage("No serial port selected.");
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet("font-weight: bold; color: #a33;");
        return;
    }
    if (!serial && params.host.isEmpty()) {
        onLogMessage("No host/IP entered.");
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet("font-weight: bold; color: #a33;");
        return;
    }

    m_activeElm = (params.deviceType == ConnectionParams::DeviceType::Elm327);
    if (m_activeElm) {
        if (serial)
            m_elm->openSerial(params.serialPort);
        else
            m_elm->openNetwork(params.host, params.tcpPort);
    } else {
        if (serial)
            m_connection->openSerial(params.serialPort, params.espMode);
        else
            m_connection->openNetwork(params.host, params.tcpPort);
    }

    setConnectedUiState(true);
}

void MainWindow::setConnectedUiState(bool connected)
{
    m_connectButton->setText(connected ? "Disconnect" : "New Connection...");
}

void MainWindow::onConnected()
{
    m_statusLabel->setText("Connected");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #292;");
    m_testRequestButton->setEnabled(true);
    m_monitorButton->setEnabled(true);
    m_readVinButton->setEnabled(true);
    m_readCalIdButton->setEnabled(true);
    setDtcButtonsEnabled(true);

    // We connect over ISO 15765-4 CAN at the detected bus speed.
    m_detectedProtocol = "ISO 15765-4 (CAN)";
    m_protocolValue->setText(m_detectedProtocol);
}

void MainWindow::onDisconnected(const QString &reason)
{
    m_pidMonitor->stop();
    m_elm->stopMonitoring();
    m_monitorButton->setText("Start Monitoring");
    m_monitorButton->setEnabled(false);
    setConnectedUiState(false);
    m_statusLabel->setText("Disconnected");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #a33;");
    m_testRequestButton->setEnabled(false);
    m_readVinButton->setEnabled(false);
    m_readCalIdButton->setEnabled(false);
    setDtcButtonsEnabled(false);
    if (!reason.isEmpty())
        onLogMessage("Disconnected: " + reason);
}

void MainWindow::setDtcButtonsEnabled(bool enabled)
{
    m_readStoredButton->setEnabled(enabled);
    m_readPendingButton->setEnabled(enabled);
    m_readPermanentButton->setEnabled(enabled);
    m_clearDtcButton->setEnabled(enabled);
}

void MainWindow::onReadStoredClicked()
{
    m_dtcTable->setRowCount(0);
    onLogMessage("Reading stored DTCs (service 03)...");
    if (m_activeElm) m_elm->readStoredDtcs();
    else m_dtcClient->readStoredDtcs();
}

void MainWindow::onReadPendingClicked()
{
    m_dtcTable->setRowCount(0);
    onLogMessage("Reading pending DTCs (service 07)...");
    if (m_activeElm) m_elm->readPendingDtcs();
    else m_dtcClient->readPendingDtcs();
}

void MainWindow::onReadPermanentClicked()
{
    m_dtcTable->setRowCount(0);
    onLogMessage("Reading permanent DTCs (service 0A)...");
    if (m_activeElm) m_elm->readPermanentDtcs();
    else m_dtcClient->readPermanentDtcs();
}

void MainWindow::onClearDtcsClicked()
{
    const auto choice = QMessageBox::warning(
        this, "Clear Diagnostic Trouble Codes",
        "This clears stored trouble codes and turns off the check-engine light on the "
        "connected vehicle. It also erases freeze-frame data and resets emissions "
        "readiness monitors.\n\nAre you sure you want to continue?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (choice != QMessageBox::Yes)
        return;

    onLogMessage("Clearing DTCs (service 04)...");
    if (m_activeElm) m_elm->clearDtcs();
    else m_dtcClient->clearDtcs();
}

void MainWindow::onDtcsReceived(quint8 mode, const QStringList &codes)
{
    QString label;
    switch (mode) {
    case 0x03: label = "Stored"; break;
    case 0x07: label = "Pending"; break;
    case 0x0A: label = "Permanent"; break;
    default: label = QString("Mode %1").arg(mode); break;
    }

    if (codes.isEmpty()) {
        onLogMessage(QString("No %1 trouble codes reported.").arg(label.toLower()));
        return;
    }

    for (const QString &code : codes) {
        const int row = m_dtcTable->rowCount();
        m_dtcTable->insertRow(row);
        m_dtcTable->setItem(row, 0, new QTableWidgetItem(code));
        m_dtcTable->setItem(row, 1, new QTableWidgetItem(label));
        m_dtcTable->setItem(row, 2, new QTableWidgetItem(ObdDtcClient::describeDtc(code)));
    }
    onLogMessage(QString("Read %1 %2 trouble code(s).").arg(codes.size()).arg(label.toLower()));
}

void MainWindow::onDtcsCleared()
{
    m_dtcTable->setRowCount(0);
    onLogMessage("Trouble codes cleared. Re-read to confirm.");
    QMessageBox::information(this, "Clear DTCs",
                             "The ECU acknowledged the clear request. "
                             "Re-read stored codes to confirm they are gone.");
}

void MainWindow::onMonitorButtonClicked()
{
    const bool running = m_activeElm ? m_elm->isMonitoring() : m_pidMonitor->isRunning();
    if (running) {
        if (m_activeElm) m_elm->stopMonitoring();
        else m_pidMonitor->stop();
        m_monitorButton->setText("Start Monitoring");
        onLogMessage("Stopped live PID monitoring.");
    } else {
        if (m_activeElm) m_elm->startMonitoring();
        else m_pidMonitor->start();
        m_monitorButton->setText("Stop Monitoring");
        onLogMessage("Started live PID monitoring.");
    }
}

void MainWindow::onPidUpdated(quint8 pid, double value)
{
    // Live Data table.
    const auto it = m_pidRow.constFind(pid);
    if (it != m_pidRow.constEnd()) {
        QTableWidgetItem *item = m_pidTable->item(it.value(), 1);
        if (item)
            item->setText(QString::number(value, 'f', 1));
    }

    // Dashboard gauge.
    const auto git = m_gauges.constFind(pid);
    if (git != m_gauges.constEnd())
        git.value()->setValue(value);

    // Live chart, if this PID is the selected series.
    if (m_chartPidCombo->currentData().toUInt() == pid)
        m_chart->addSample(value);
}

void MainWindow::onSendTestRequestClicked()
{
    // Standard OBD-II functional request: mode 01 ("show current data"), PID 0x00
    // ("supported PIDs 01-20"), addressed to the broadcast functional ID 0x7DF.
    // ISO 15765-2 single-frame format: [0x02, mode, pid, padding...] padded to 8 bytes.
    if (m_activeElm) {
        m_elm->sendTestRequest();
        onLogMessage("Sent test request (Mode 01 PID 00) via ELM327.");
        return;
    }
    const QByteArray payload = QByteArray::fromHex("0201000000000000");
    const bool sent = m_connection->sendFrame(0x7DF, false, 0, payload);
    if (sent)
        onLogMessage("Sent test request (Mode 01 PID 00) to 0x7DF - watch the table for a reply on 0x7E8-0x7EF.");
    else
        onLogMessage("Failed to send test request - not connected.");
}

void MainWindow::onFrameReceived(const CanFrame &frame)
{
    // Persist to the active recording (if any) before buffering for display.
    m_logger->recordFrame(frame);

    // Just buffer here - flushPendingFrames() moves these into the table on a
    // timer so a high-rate bus can't stall the UI thread.
    m_pendingFrames.append(frame);
}

void MainWindow::flushPendingFrames()
{
    if (m_pendingFrames.isEmpty())
        return;

    const QVector<CanFrame> batch = std::move(m_pendingFrames);
    m_pendingFrames.clear();

    m_frameTable->setUpdatesEnabled(false);
    for (const CanFrame &frame : batch)
        appendFrameRow(frame);
    m_frameTable->setUpdatesEnabled(true);

    if (m_autoScrollCheck->isChecked())
        m_frameTable->scrollToBottom();

    m_frameCount += batch.size();
    m_frameCountLabel->setText(QString("Frames: %1").arg(m_frameCount));
}

void MainWindow::appendFrameRow(const CanFrame &frame)
{
    int row = m_frameTable->rowCount();
    m_frameTable->insertRow(row);

    const double seconds = frame.timestampUs / 1000000.0;
    m_frameTable->setItem(row, 0, new QTableWidgetItem(QString::number(seconds, 'f', 6)));
    m_frameTable->setItem(row, 1, new QTableWidgetItem(QString::number(frame.bus)));
    m_frameTable->setItem(row, 2, new QTableWidgetItem(formatHexId(frame.id, frame.extended)));
    m_frameTable->setItem(row, 3, new QTableWidgetItem(frame.extended ? "Yes" : "No"));
    m_frameTable->setItem(row, 4, new QTableWidgetItem(QString::number(frame.length)));
    m_frameTable->setItem(row, 5, new QTableWidgetItem(formatHexBytes(frame)));

    while (m_frameTable->rowCount() > kMaxDisplayedRows)
        m_frameTable->removeRow(0);
}

void MainWindow::onClearClicked()
{
    m_pendingFrames.clear();
    m_frameTable->setRowCount(0);
    m_frameCount = 0;
    m_frameCountLabel->setText("Frames: 0");
}

void MainWindow::onDeviceInfoReceived(int buildNumber, int singleWireMode)
{
    m_firmwareText = QString("build %1 (single-wire mode: %2)")
                          .arg(buildNumber)
                          .arg(singleWireMode ? "on" : "off");
    m_deviceInfoLabel->setText("Scanner firmware " + m_firmwareText);
    m_firmwareValue->setText(m_firmwareText);
}

void MainWindow::onBusParamsReceived(int bus0Baud, bool bus0Enabled, int bus1Baud, bool bus1Enabled)
{
    onLogMessage(QString("Bus 0: %1 baud (%2), Bus 1: %3 baud (%4)")
                     .arg(bus0Baud)
                     .arg(bus0Enabled ? "enabled" : "disabled")
                     .arg(bus1Baud)
                     .arg(bus1Enabled ? "enabled" : "disabled"));
    if (bus0Baud > 0) {
        m_detectedProtocol = QString("ISO 15765-4 (CAN, %1 kbit/s)").arg(bus0Baud / 1000);
        m_protocolValue->setText(m_detectedProtocol);
    }
}

void MainWindow::onLogMessage(const QString &text)
{
    statusBar()->showMessage(text, 5000);
    m_logView->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + "  " + text);
}

// --- Vehicle info ---

void MainWindow::onReadVinClicked()
{
    onLogMessage("Requesting VIN (Mode 09 PID 02)...");
    if (m_activeElm) m_elm->readVin();
    else m_vehicleInfo->readVin();
}

void MainWindow::onReadCalIdsClicked()
{
    onLogMessage("Requesting calibration IDs (Mode 09 PID 04)...");
    if (m_activeElm) m_elm->readCalibrationIds();
    else m_vehicleInfo->readCalibrationIds();
}

void MainWindow::onVinReceived(const QString &vin)
{
    m_vinText = vin;
    m_vinValue->setText(vin.isEmpty() ? "(empty response)" : vin);
    onLogMessage("VIN: " + vin);
}

void MainWindow::onCalibrationIdsReceived(const QStringList &ids)
{
    m_calIds = ids;
    m_calIdValue->setText(ids.isEmpty() ? "(none)" : ids.join(", "));
    onLogMessage(QString("Read %1 calibration ID(s).").arg(ids.size()));
}

void MainWindow::onCheckFirmwareClicked()
{
    // Real update-checking needs a defined firmware source (a version file/URL).
    // We surface the detected version and explain the framework honestly rather
    // than pretend to contact a server that doesn't exist.
    const QString current = m_firmwareText.isEmpty() ? "unknown (connect first)" : m_firmwareText;
    QMessageBox::information(
        this, "Firmware / Updates",
        QString("Connected scanner firmware: %1\n\n"
                "Automatic update checking requires a configured firmware source "
                "(a published version manifest). None is configured, so this build "
                "reports the installed version only. ESP32RET itself supports OTA "
                "updates over WiFi if you host an update image.")
            .arg(current));
}

// --- Data logging / replay ---

void MainWindow::onRecordButtonClicked()
{
    if (m_logger->isRecording()) {
        m_logger->stopRecording();
        m_recordButton->setText("Record...");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Record session to file", "session.csv", "CSV logs (*.csv)");
    if (path.isEmpty())
        return;
    if (m_logger->startRecording(path))
        m_recordButton->setText("Stop Recording");
}

void MainWindow::onReplayButtonClicked()
{
    if (m_logger->isReplaying()) {
        m_logger->stopReplay();
        m_replayButton->setText("Replay...");
        onLogMessage("Replay stopped.");
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, "Replay recorded session", QString(), "CSV logs (*.csv)");
    if (path.isEmpty())
        return;
    onClearClicked();
    if (m_logger->startReplay(path))
        m_replayButton->setText("Stop Replay");
}

void MainWindow::onFrameReplayed(const CanFrame &frame)
{
    // Replayed frames are shown in the raw traffic view (buffered like live
    // frames and flushed to the table by the timer).
    m_pendingFrames.append(frame);
}

void MainWindow::onReplayFinished()
{
    m_replayButton->setText("Replay...");
}

// --- Report export ---

void MainWindow::onExportReport()
{
    ReportData data;
    data.vin = m_vinText;
    data.protocol = m_detectedProtocol;
    data.firmware = m_firmwareText;
    data.calibrationIds = m_calIds;

    for (int r = 0; r < m_dtcTable->rowCount(); ++r) {
        ReportData::Dtc d;
        d.code = m_dtcTable->item(r, 0) ? m_dtcTable->item(r, 0)->text() : QString();
        d.status = m_dtcTable->item(r, 1) ? m_dtcTable->item(r, 1)->text() : QString();
        d.description = m_dtcTable->item(r, 2) ? m_dtcTable->item(r, 2)->text() : QString();
        data.dtcs.append(d);
    }

    for (int r = 0; r < m_pidTable->rowCount(); ++r) {
        ReportData::PidReading p;
        p.name = m_pidTable->item(r, 0) ? m_pidTable->item(r, 0)->text() : QString();
        p.value = m_pidTable->item(r, 1) ? m_pidTable->item(r, 1)->text() : QString();
        p.unit = m_pidTable->item(r, 2) ? m_pidTable->item(r, 2)->text() : QString();
        data.pidSnapshot.append(p);
    }

    const QString path = QFileDialog::getSaveFileName(
        this, "Export diagnostic report", "obd-report.pdf",
        "PDF report (*.pdf);;JSON (*.json);;CSV (*.csv)");
    if (path.isEmpty())
        return;

    QString error;
    bool ok = false;
    if (path.endsWith(".json", Qt::CaseInsensitive))
        ok = ReportExporter::exportJson(data, path, &error);
    else if (path.endsWith(".csv", Qt::CaseInsensitive))
        ok = ReportExporter::exportCsv(data, path, &error);
    else
        ok = ReportExporter::exportPdf(data, path, &error);

    if (ok)
        onLogMessage("Report exported to " + path);
    else
        QMessageBox::warning(this, "Export failed", "Could not write report: " + error);
}

void MainWindow::onToggleTheme(bool dark)
{
    if (dark) {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        QPalette p;
        p.setColor(QPalette::Window, QColor(0x2B, 0x2E, 0x33));
        p.setColor(QPalette::WindowText, QColor(0xE8, 0xEA, 0xED));
        p.setColor(QPalette::Base, QColor(0x24, 0x27, 0x2B));
        p.setColor(QPalette::AlternateBase, QColor(0x2F, 0x33, 0x38));
        p.setColor(QPalette::Text, QColor(0xE8, 0xEA, 0xED));
        p.setColor(QPalette::Button, QColor(0x3A, 0x3F, 0x46));
        p.setColor(QPalette::ButtonText, QColor(0xE8, 0xEA, 0xED));
        p.setColor(QPalette::Highlight, QColor(0x4C, 0xA6, 0xFF));
        p.setColor(QPalette::HighlightedText, QColor(0x10, 0x12, 0x14));
        p.setColor(QPalette::ToolTipBase, QColor(0x2B, 0x2E, 0x33));
        p.setColor(QPalette::ToolTipText, QColor(0xE8, 0xEA, 0xED));
        qApp->setPalette(p);
    } else {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        qApp->setPalette(qApp->style()->standardPalette());
    }
}
