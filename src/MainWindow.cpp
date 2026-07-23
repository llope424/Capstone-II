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
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QProcess>
#include <QProgressDialog>
#include <QShortcut>
#include <QStandardPaths>
#include <QToolBar>
#include <QUrl>
#include <QVersionNumber>

#include "AppSettings.h"
#include "AppStyle.h"
#include "DashboardConfigDialog.h"
#include "Elm327Connection.h"
#include "PreferencesDialog.h"
#include "Units.h"
#include "emulator/EmulatorWindow.h"
#include "FrameSummaryModel.h"
#include "FrameTableModel.h"
#include "GaugeWidget.h"
#include "LiveChartWidget.h"
#include "NewConnectionDialog.h"
#include "ObdDtcClient.h"
#include "ObdFreezeFrameClient.h"
#include "ObdPidMonitor.h"
#include "ObdVehicleInfo.h"
#include "VinDecoder.h"
#include "ReportExporter.h"
#include "SessionLogger.h"
#include "VehicleStore.h"

namespace {
// Frame formatting/trimming now lives in FrameTableModel.

// FR-1 auto-reconnect: retries after an unexpected drop, with growing delays
// (3 s, 6 s, ... up to the cap) so a car being switched off doesn't cause an
// endless fast retry loop.
constexpr int kMaxReconnectAttempts = 5;
constexpr int kReconnectBaseDelayMs = 3000;

// The releases list rather than /releases/latest: the latter excludes
// pre-releases, and this project publishes pre-releases while in development.
const char kReleasesApiUrl[] = "https://api.github.com/repos/llope424/Capstone-II/releases?per_page=1";

// Traffic-light monitoring icons: green play, red stop, in every style. The
// thin outline uses the style's button-text color so the shape stays visible
// even when the chrome is itself red or green (e.g. the Toyota preset).
QIcon monitorIcon(bool running, const QColor &outline)
{
    const QColor fill = running ? QColor(0xD9, 0x3A, 0x2E)   // stop: red
                                : QColor(0x2E, 0xAE, 0x4F);  // start: green
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(outline, 1.4));
    p.setBrush(fill);
    if (running)
        p.drawRect(5, 5, 14, 14);
    else
        p.drawPolygon(QPolygonF({QPointF(6, 4), QPointF(20, 12), QPointF(6, 20)}));
    p.end();

    QIcon icon(pm);
    // Keep the color identity while disabled (Qt's default would grey it):
    // same shape at reduced opacity.
    QPixmap dimmed(24, 24);
    dimmed.fill(Qt::transparent);
    QPainter dp(&dimmed);
    dp.setOpacity(0.45);
    dp.drawPixmap(0, 0, pm);
    dp.end();
    icon.addPixmap(dimmed, QIcon::Disabled);
    return icon;
}

// Display ranges (and optional red-zone thresholds) for the gauges, in metric
// source units. This is the catalog the dashboard layout editor offers; the
// user picks which of these appear and in what order (SDD FR-6).
struct GaugeCatalogEntry
{
    quint8 pid;
    double min;
    double max;
    bool hasWarn;       // alert when value goes ABOVE this threshold
    double warn;
    bool hasWarnLow;    // alert when value drops BELOW this threshold
    double warnLow;
};

const QVector<GaugeCatalogEntry> &gaugeCatalog()
{
    static const QVector<GaugeCatalogEntry> catalog = {
        //  PID   min    max    hasWarn warn  hasWarnLow warnLow
        {0x0C, 0, 8000,   true,  6500,  false, 0},     // Engine RPM
        {0x0D, 0, 240,    true,  180,   false, 0},     // Vehicle Speed
        {0x05, -40, 150,  true,  105,   false, 0},     // Coolant Temperature
        {0x04, 0, 100,    true,  90,    false, 0},     // Engine Load
        {0x11, 0, 100,    false, 0,     false, 0},     // Throttle Position
        {0x0F, -40, 150,  true,  80,    false, 0},     // Intake Air Temperature
        {0x42, 0, 16,     true,  15.5,  true,  11.0},  // Control Module Voltage (11-15.5 V band)
        {0x0A, 0, 765,    false, 0,     false, 0},     // Fuel Pressure
        {0x06, -100, 100, false, 0,     false, 0},     // Short-Term Fuel Trim B1
        {0x07, -100, 100, false, 0,     false, 0},     // Long-Term Fuel Trim B1
        // O2 voltage oscillates near 0 V normally, so no low-threshold alert.
        {0x14, 0, 1.3,    false, 0,     false, 0},     // O2 Sensor 1 Voltage
        {0x15, 0, 1.3,    false, 0,     false, 0},     // O2 Sensor 2 Voltage
        {0x0B, 0, 255,    false, 0,     false, 0},     // Intake Manifold Pressure
        {0x0E, -64, 64,   false, 0,     false, 0},     // Timing Advance
        {0x10, 0, 300,    false, 0,     false, 0},     // MAF Air Flow Rate
        {0x2E, 0, 100,    false, 0,     false, 0},     // Commanded EVAP Purge
        {0x2F, 0, 100,    false, 0,     false, 0},     // Fuel Tank Level
        {0x33, 0, 120,    false, 0,     false, 0},     // Barometric Pressure
        {0x43, 0, 100,    false, 0,     false, 0},     // Absolute Load Value
        {0x44, 0, 2,      false, 0,     false, 0},     // Commanded Equivalence Ratio
        {0x45, 0, 100,    false, 0,     false, 0},     // Relative Throttle Position
        {0x46, -40, 60,   false, 0,     false, 0},     // Ambient Air Temperature
        {0x49, 0, 100,    false, 0,     false, 0},     // Accelerator Pedal Position
        {0x4C, 0, 100,    false, 0,     false, 0},     // Commanded Throttle
        {0x5C, -40, 160,  true,  130,   false, 0},     // Engine Oil Temperature
        {0x5E, 0, 60,     false, 0,     false, 0},     // Engine Fuel Rate
    };
    return catalog;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QString("OBD Suite v%1 - Raw Traffic Viewer")
                       .arg(QApplication::applicationVersion()));
    resize(1000, 650);
    m_imperial = AppSettings::imperialUnits(); // needed before the tabs are built

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

    m_freezeClient = new ObdFreezeFrameClient(m_connection, this);
    connect(m_freezeClient, &ObdFreezeFrameClient::freezeFrameDtcReceived,
            this, &MainWindow::onFreezeFrameDtc);
    connect(m_freezeClient, &ObdFreezeFrameClient::freezeFramePidReceived,
            this, &MainWindow::onFreezeFramePid);
    connect(m_freezeClient, &ObdFreezeFrameClient::logMessage, this, &MainWindow::onLogMessage);

    m_vehicleInfo = new ObdVehicleInfo(m_connection, this);
    connect(m_vehicleInfo, &ObdVehicleInfo::vinReceived, this, &MainWindow::onVinReceived);
    connect(m_vehicleInfo, &ObdVehicleInfo::calibrationIdsReceived, this, &MainWindow::onCalibrationIdsReceived);
    connect(m_vehicleInfo, &ObdVehicleInfo::logMessage, this, &MainWindow::onLogMessage);

    m_vinDecoder = new VinDecoder(this);
    connect(m_vinDecoder, &VinDecoder::decoded, this, &MainWindow::onVinDecoded);

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
    connect(m_elm, &Elm327Connection::freezeFrameDtcReceived, this, &MainWindow::onFreezeFrameDtc);
    connect(m_elm, &Elm327Connection::freezeFramePidReceived, this, &MainWindow::onFreezeFramePid);
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

    // Connection state and adapter info live in the status bar; connecting is a
    // toolbar action. No dedicated bars above the tabs.
    m_statusLabel = new QLabel();
    setStatus("Disconnected", 'r');
    m_deviceInfoLabel = new QLabel("No device info yet.");

    auto *tabs = new QTabWidget(central);

    // --- Dashboard tab (gauges + live chart) ---
    buildDashboardTab(tabs);

    // --- Live Data tab (monitoring is started from the quick-access toolbar) ---
    auto *livePage = new QWidget();
    auto *liveLayout = new QVBoxLayout(livePage);

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
    m_readFreezeButton = new QPushButton("Read Freeze Frame");
    m_readReadinessButton = new QPushButton("Readiness");
    m_readReadinessButton->setToolTip("Mode 01 PID 01 - check-engine light status, trouble-code "
                                      "count, and the emissions monitors' inspection readiness.");
    connect(m_readReadinessButton, &QPushButton::clicked,
            this, &MainWindow::onReadReadinessClicked);
    m_clearDtcButton = new QPushButton("Clear DTCs...");
    m_readStoredButton->setToolTip("Service 03 - confirmed trouble codes (MIL/check-engine codes).");
    m_readPendingButton->setToolTip("Service 07 - pending codes not yet confirmed.");
    m_readPermanentButton->setToolTip("Service 0A - permanent codes that cannot be cleared manually.");
    m_readFreezeButton->setToolTip("Service 02 - the sensor snapshot the ECU captured at the "
                                   "moment the first confirmed trouble code set.");
    m_clearDtcButton->setToolTip("Service 04 - clears stored codes and turns off the MIL. Asks for confirmation.");
    connect(m_readStoredButton, &QPushButton::clicked, this, &MainWindow::onReadStoredClicked);
    connect(m_readPendingButton, &QPushButton::clicked, this, &MainWindow::onReadPendingClicked);
    connect(m_readPermanentButton, &QPushButton::clicked, this, &MainWindow::onReadPermanentClicked);
    connect(m_readFreezeButton, &QPushButton::clicked, this, &MainWindow::onReadFreezeFrameClicked);
    connect(m_clearDtcButton, &QPushButton::clicked, this, &MainWindow::onClearDtcsClicked);
    dtcBar->addWidget(m_readStoredButton);
    dtcBar->addWidget(m_readPendingButton);
    dtcBar->addWidget(m_readPermanentButton);
    dtcBar->addWidget(m_readFreezeButton);
    dtcBar->addWidget(m_readReadinessButton);
    dtcBar->addStretch();
    dtcBar->addWidget(m_clearDtcButton);
    dtcLayout->addLayout(dtcBar);

    // I/M readiness summary (filled by Readiness; rich text).
    m_readinessLabel = new QLabel("Readiness: not read yet.");
    m_readinessLabel->setWordWrap(true);
    m_readinessLabel->setTextFormat(Qt::RichText);
    dtcLayout->addWidget(m_readinessLabel);

    m_dtcTable = new QTableWidget(0, 3, dtcPage);
    m_dtcTable->setHorizontalHeaderLabels({"Code", "Status", "Description"});
    m_dtcTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_dtcTable->verticalHeader()->setVisible(false);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dtcLayout->addWidget(m_dtcTable, 2);

    // Freeze frame (Mode 02): trigger DTC + the captured parameter snapshot.
    m_freezeInfoLabel = new QLabel("Freeze frame: not read yet.");
    dtcLayout->addWidget(m_freezeInfoLabel);
    m_freezeTable = new QTableWidget(0, 3, dtcPage);
    m_freezeTable->setHorizontalHeaderLabels({"Parameter", "Captured Value", "Unit"});
    m_freezeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_freezeTable->verticalHeader()->setVisible(false);
    m_freezeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_freezeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dtcLayout->addWidget(m_freezeTable, 1);

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

    testBar->addWidget(new QLabel("Filter ID:"));
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("e.g. 7E8");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(140);
    m_filterEdit->setToolTip("Show only frames whose ID contains this text (case-insensitive).");
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);
    testBar->addWidget(m_filterEdit);

    m_overviewCheck = new QCheckBox("Overview (per ID)");
    m_overviewCheck->setToolTip("Collapse to one row per unique ID, with a live count and last data.");
    connect(m_overviewCheck, &QCheckBox::toggled, this, &MainWindow::onOverviewToggled);
    testBar->addWidget(m_overviewCheck);
    rawLayout->addLayout(testBar);

    // Streaming frame list: model/view so columns are sortable (click a header to
    // sort; click again to reverse). The proxy sorts by FrameTableModel::SortRole
    // (numeric keys), so IDs and timestamps order by value, not by text.
    m_frameModel = new FrameTableModel(this);
    m_frameProxy = new QSortFilterProxyModel(this);
    m_frameProxy->setSourceModel(m_frameModel);
    m_frameProxy->setSortRole(FrameTableModel::SortRole);
    m_frameProxy->setFilterKeyColumn(FrameTableModel::Id);
    m_frameProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_frameView = new QTableView(rawPage);
    m_frameView->setModel(m_frameProxy);
    m_frameView->setSortingEnabled(true);
    m_frameView->sortByColumn(FrameTableModel::Time, Qt::AscendingOrder); // default: arrival order
    m_frameView->horizontalHeader()->setSectionResizeMode(FrameTableModel::Data, QHeaderView::Stretch);
    m_frameView->verticalHeader()->setVisible(false);
    m_frameView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frameView->setSelectionBehavior(QAbstractItemView::SelectRows);
    rawLayout->addWidget(m_frameView, 1);

    // Overview: one row per unique ID (hidden until the Overview box is ticked).
    m_summaryModel = new FrameSummaryModel(this);
    m_summaryProxy = new QSortFilterProxyModel(this);
    m_summaryProxy->setSourceModel(m_summaryModel);
    m_summaryProxy->setSortRole(FrameSummaryModel::SortRole);
    m_summaryProxy->setFilterKeyColumn(FrameSummaryModel::Id);
    m_summaryProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_summaryView = new QTableView(rawPage);
    m_summaryView->setModel(m_summaryProxy);
    m_summaryView->setSortingEnabled(true);
    m_summaryView->sortByColumn(FrameSummaryModel::Id, Qt::AscendingOrder);
    m_summaryView->horizontalHeader()->setSectionResizeMode(FrameSummaryModel::Data, QHeaderView::Stretch);
    m_summaryView->verticalHeader()->setVisible(false);
    m_summaryView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summaryView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_summaryView->setVisible(false);
    rawLayout->addWidget(m_summaryView, 1);

    // Ctrl+C copies the current view's selection.
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, rawPage);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::onCopyFrames);

    auto *bottomBar = new QHBoxLayout();
    m_autoScrollCheck = new QCheckBox("Auto-scroll");
    m_autoScrollCheck->setChecked(true);
    bottomBar->addWidget(m_autoScrollCheck);

    m_pauseButton = new QPushButton("Pause");
    m_pauseButton->setToolTip("Freeze the view so you can inspect and sort captured frames.");
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    bottomBar->addWidget(m_pauseButton);

    m_copyButton = new QPushButton("Copy");
    m_copyButton->setToolTip("Copy the selected rows to the clipboard (or press Ctrl+C).");
    connect(m_copyButton, &QPushButton::clicked, this, &MainWindow::onCopyFrames);
    bottomBar->addWidget(m_copyButton);

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

    // --- Vehicle tab (info read from the car + saved profiles/history) ---
    buildVehicleTab(tabs);

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

    // Status bar: connection state on the left; link quality, adapter info and
    // version on the right. Plain widgets (not showMessage) so nothing hides them.
    statusBar()->addWidget(m_statusLabel);
    m_qualityLabel = new QLabel();
    m_qualityLabel->setToolTip("Communication quality: command success rate and average "
                               "round-trip latency (ELM327), or bus frame rate (GVRET).");
    statusBar()->addPermanentWidget(m_qualityLabel);
    statusBar()->addPermanentWidget(m_deviceInfoLabel);
    auto *versionLabel = new QLabel(QString("v%1").arg(QApplication::applicationVersion()));
    statusBar()->addPermanentWidget(versionLabel);

    // FR-1: automatic reconnection after an unexpected drop.
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_connection->isOpen() || m_elm->isOpen())
            return;
        onLogMessage(QString("Auto-reconnect attempt %1 of %2...")
                         .arg(m_reconnectAttempt)
                         .arg(kMaxReconnectAttempts));
        openConnection(m_lastParams);
    });

    // FR-2: refresh the link-quality readout every 2 s while connected.
    m_qualityTimer.setInterval(2000);
    connect(&m_qualityTimer, &QTimer::timeout, this, [this]() {
        if (m_activeElm && m_elm->isOpen()) {
            const Elm327Connection::LinkQuality q = m_elm->takeLinkQuality();
            if (q.total > 0)
                m_qualityLabel->setText(QString("Link: %1% ok, %2 ms")
                                            .arg(100 * q.ok / q.total)
                                            .arg(qRound(q.avgLatencyMs)));
            else
                m_qualityLabel->setText("Link: idle");
        } else if (!m_activeElm && m_connection->isOpen()) {
            const quint64 delta = m_frameCount - m_lastFrameCount;
            m_qualityLabel->setText(QString("Bus: %1 frames/s").arg(delta / 2));
        } else {
            m_qualityLabel->clear();
        }
        m_lastFrameCount = m_frameCount;
    });
    m_qualityTimer.start();

    // FR-10: one quiet update check at startup (result only logged if newer).
    m_network = new QNetworkAccessManager(this);
    checkForUpdates(false);

    const QByteArray geometry = AppSettings::windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void MainWindow::buildVehicleTab(QTabWidget *tabs)
{
    // One tab for everything vehicle-related: live-read identification on the
    // left, the saved profile garage on the right.
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(buildVehicleInfoPane());
    splitter->addWidget(buildVehiclesPane());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    tabs->addTab(splitter, "Vehicle");
}

QWidget *MainWindow::buildVehicleInfoPane()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *heading = new QLabel("<b>Read from vehicle</b>");
    layout->addWidget(heading);

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
    m_decodedMakeValue = new QLabel("--");
    m_decodedModelValue = new QLabel("--");
    m_decodedYearValue = new QLabel("--");
    m_decodedTrimValue = new QLabel("--");
    m_decodedMakeValue->setWordWrap(true);
    form->addRow("Make:", m_decodedMakeValue);
    form->addRow("Model:", m_decodedModelValue);
    form->addRow("Model Year:", m_decodedYearValue);
    form->addRow("Trim:", m_decodedTrimValue);
    layout->addLayout(form);
    layout->addStretch();

    m_readVinButton->setEnabled(false);
    m_readCalIdButton->setEnabled(false);

    return page;
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
    auto *trimEdit = new QLineEdit(profile.trim);
    auto *notesEdit = new QLineEdit(profile.notes);
    form->addRow("Name:", nameEdit);
    form->addRow("VIN:", vinEdit);
    form->addRow("Make:", makeEdit);
    form->addRow("Model:", modelEdit);
    form->addRow("Year:", yearEdit);
    form->addRow("Trim:", trimEdit);
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
    profile.trim = trimEdit->text().trimmed();
    profile.notes = notesEdit->text().trimmed();
    if (profile.name.isEmpty())
        profile.name = profile.vin.isEmpty() ? "Unnamed vehicle" : profile.vin;
    return true;
}
} // namespace

QWidget *MainWindow::buildVehiclesPane()
{
    auto *page = new QWidget();
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->addWidget(new QLabel("<b>Saved vehicles</b>"));
    auto *outer = new QHBoxLayout();
    pageLayout->addLayout(outer, 1);

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

    refreshVehicleList();
    onVehicleSelectionChanged();
    return page;
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
    m_vehicleDetails->setText(
        QString("<b>%1</b><br>VIN: %2<br>Make/Model/Year: %3 %4 %5<br>Trim: %6<br>Notes: %7")
            .arg(v.name.toHtmlEscaped(),
                 v.vin.isEmpty() ? "-" : v.vin.toHtmlEscaped(),
                 v.make.toHtmlEscaped(), v.model.toHtmlEscaped(), v.year.toHtmlEscaped(),
                 v.trim.isEmpty() ? "-" : v.trim.toHtmlEscaped(),
                 v.notes.isEmpty() ? "-" : v.notes.toHtmlEscaped()));
    for (const DiagnosticRecord &r : v.history) {
        const QString codes = r.dtcs.isEmpty() ? "no codes" : r.dtcs.join(", ");
        m_historyList->addItem(QString("%1  -  %2").arg(r.timestamp, codes));
    }
}

void MainWindow::onAddVehicle()
{
    VehicleProfile v;
    // Pre-fill VIN and any decoded make/model/year/trim from this session.
    v.vin = m_vinText;
    v.make = m_decodedMake;
    v.model = m_decodedModel;
    v.year = m_decodedYear;
    v.trim = m_decodedTrim;
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

    auto *emulatorMenu = menuBar()->addMenu("&Emulator");
    auto *openEmuAction = emulatorMenu->addAction("&Open Emulator...");
    connect(openEmuAction, &QAction::triggered, this, &MainWindow::onOpenEmulator);

    // Top-level Preferences entry (replaces the old View menu, whose only item
    // - the theme toggle - now lives inside the Preferences dialog).
    auto *prefsMenuAction = menuBar()->addAction("&Preferences...");
    connect(prefsMenuAction, &QAction::triggered, this, &MainWindow::onPreferences);

    // Quick-access toolbar: only the core session actions, in workflow order.
    // Everything else (emulator, preferences, export) stays in the menus.
    auto *toolbar = addToolBar("Quick Access");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *style = this->style();

    m_connectAction = toolbar->addAction(style->standardIcon(QStyle::SP_DriveNetIcon),
                                         "Connect...");
    m_connectAction->setToolTip("Open or close a scanner connection.");
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::onConnectButtonClicked);
    toolbar->addSeparator();

    m_monitorAction = toolbar->addAction(
        monitorIcon(false, palette().color(QPalette::ButtonText)), "Start Monitoring");
    m_monitorAction->setToolTip("Continuously poll the standard OBD-II PIDs (shown on Live Data).");
    m_monitorAction->setEnabled(false);
    connect(m_monitorAction, &QAction::triggered, this, &MainWindow::onMonitorButtonClicked);

    m_readDtcsAction = toolbar->addAction(style->standardIcon(QStyle::SP_MessageBoxWarning),
                                          "Read DTCs");
    m_readDtcsAction->setToolTip("Read stored trouble codes (service 03).");
    m_readDtcsAction->setEnabled(false);
    connect(m_readDtcsAction, &QAction::triggered, this, &MainWindow::onReadStoredClicked);
    toolbar->addSeparator();

    auto *tbExport = toolbar->addAction(style->standardIcon(QStyle::SP_DialogSaveButton),
                                        "Export");
    tbExport->setToolTip("Export a diagnostic report (PDF / CSV / JSON).");
    connect(tbExport, &QAction::triggered, this, &MainWindow::onExportReport);
}

void MainWindow::updateMonitorAction(bool running)
{
    m_monitorRunning = running;
    m_monitorAction->setText(running ? "Stop Monitoring" : "Start Monitoring");
    m_monitorAction->setIcon(monitorIcon(running, palette().color(QPalette::ButtonText)));
}

void MainWindow::onPreferences()
{
    PreferencesDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Style (may have changed preset or custom colors); restyle the status
    // label and the palette-painted monitor icon to match the new chrome.
    AppStyle::apply(AppSettings::styleName());
    setStatus(m_statusLabel->text(), m_statusKind);
    updateMonitorAction(m_monitorRunning);

    // Units: refresh every value display so labels and numbers stay consistent.
    const bool imperial = AppSettings::imperialUnits();
    if (imperial != m_imperial) {
        m_imperial = imperial;
        applyDisplayUnits();
    }

    // Rebuild the gauges so a changed gauge style takes effect (also harmless
    // if only colors changed - the gauges repaint from the new palette).
    rebuildGauges();

    // Poll rate: applies on the next start; restart live monitoring if active
    // so the change is immediate.
    const bool running = m_activeElm ? m_elm->isMonitoring() : m_pidMonitor->isRunning();
    if (running) {
        if (m_activeElm) {
            m_elm->stopMonitoring();
            m_elm->startMonitoring(AppSettings::pollIntervalMs());
        } else {
            m_pidMonitor->stop();
            m_pidMonitor->start(AppSettings::pollIntervalMs());
        }
    }
}

void MainWindow::onConfigureDashboard()
{
    // Offer every catalog PID the monitor actually knows, in catalog order.
    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();
    QVector<QPair<int, QString>> available;
    for (const GaugeCatalogEntry &entry : gaugeCatalog()) {
        for (const PidDefinition &def : defs) {
            if (def.pid == entry.pid) {
                available.append({entry.pid, def.name});
                break;
            }
        }
    }

    DashboardConfigDialog dialog(available, AppSettings::dashboardPids(),
                                 AppSettings::dashboardColumns(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    AppSettings::setDashboardPids(dialog.selectedPids());
    AppSettings::setDashboardColumns(dialog.columns());
    rebuildGauges();
}

void MainWindow::rebuildGauges()
{
    // Tear down the existing grid (widgets and layout items both).
    while (QLayoutItem *item = m_gaugeGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_gauges.clear();

    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();
    const QList<int> pids = AppSettings::dashboardPids();
    const int columns = qMax(1, AppSettings::dashboardColumns());

    int col = 0, rowIdx = 0;
    for (int pidValue : pids) {
        const GaugeCatalogEntry *cfg = nullptr;
        for (const GaugeCatalogEntry &entry : gaugeCatalog()) {
            if (entry.pid == pidValue) {
                cfg = &entry;
                break;
            }
        }
        if (!cfg)
            continue;
        // Hide gauges the connected vehicle says it will never answer.
        if (isPidKnownUnsupported(cfg->pid))
            continue;

        QString name, unit;
        for (const PidDefinition &def : defs) {
            if (def.pid == cfg->pid) {
                name = def.name;
                unit = def.unit;
                break;
            }
        }
        if (name.isEmpty())
            continue;

        // Ranges are cataloged in metric; convert the whole dial when the user
        // prefers imperial so the needle and numbers agree.
        const double min = Units::display(cfg->min, unit, m_imperial);
        const double max = Units::display(cfg->max, unit, m_imperial);
        auto *gauge = new GaugeWidget(name, Units::displayUnit(unit, m_imperial),
                                      min, max, m_gaugeGrid->parentWidget());
       if (cfg->hasWarn)
            gauge->setWarnThreshold(Units::display(cfg->warn, unit, m_imperial));
        if (cfg->hasWarnLow)
            gauge->setWarnLowThreshold(Units::display(cfg->warnLow, unit, m_imperial));
        gauge->setStyle(GaugeWidget::styleFromString(AppSettings::gaugeStyle()));
        m_gaugeGrid->addWidget(gauge, rowIdx, col);
        m_gauges.insert(cfg->pid, gauge);

        if (++col >= columns) {
            col = 0;
            ++rowIdx;
        }
    }
}

void MainWindow::applyDisplayUnits()
{
    // Unit column labels on the Live Data tab; stale values are reset so a
    // number is never shown against the wrong unit.
    for (auto it = m_pidRow.constBegin(); it != m_pidRow.constEnd(); ++it) {
        QTableWidgetItem *unitItem = m_pidTable->item(it.value(), 2);
        if (unitItem)
            unitItem->setText(Units::displayUnit(m_pidUnit.value(it.key()), m_imperial));
        QTableWidgetItem *valueItem = m_pidTable->item(it.value(), 1);
        if (valueItem)
            valueItem->setText("--");
    }
    // Same treatment for the freeze-frame snapshot table.
    for (auto it = m_freezePidRow.constBegin(); it != m_freezePidRow.constEnd(); ++it) {
        QTableWidgetItem *unitItem = m_freezeTable->item(it.value(), 2);
        if (unitItem)
            unitItem->setText(Units::displayUnit(m_pidUnit.value(it.key()), m_imperial));
        QTableWidgetItem *valueItem = m_freezeTable->item(it.value(), 1);
        if (valueItem)
            valueItem->setText("--");
    }
    rebuildGauges();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    AppSettings::setWindowGeometry(saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::downloadAndInstallUpdate(const QString &zipUrl)
{
    const QString zipPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath("ObdSuite-update.zip");

    QNetworkRequest request{QUrl(zipUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "ObdSuite");
    QNetworkReply *reply = m_network->get(request);

    auto *progress = new QProgressDialog("Downloading update...", "Cancel", 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
                if (total > 0)
                    progress->setValue(int(received * 100 / total));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, zipPath]() {
        progress->close();
        progress->deleteLater();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                onLogMessage("Update download failed: " + reply->errorString());
            return;
        }
        QFile file(zipPath);
        if (!file.open(QIODevice::WriteOnly)) {
            onLogMessage("Cannot write update file: " + zipPath);
            return;
        }
        file.write(reply->readAll());
        file.close();
        launchUpdaterAndQuit(zipPath);
    });
}

void MainWindow::launchUpdaterAndQuit(const QString &zipPath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString bundledScript = QDir(appDir).filePath("updater.ps1");
    if (!QFile::exists(bundledScript)) {
        onLogMessage("updater.ps1 not found next to the app - install manually by "
                     "unzipping " + zipPath + " over " + appDir + " after closing.");
        return;
    }

    // Run a temp copy so extraction can freely overwrite the bundled script.
    const QString tempScript =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath("ObdSuite-updater.ps1");
    QFile::remove(tempScript);
    if (!QFile::copy(bundledScript, tempScript)) {
        onLogMessage("Could not stage the updater script.");
        return;
    }

    const QStringList args = {"-NoProfile",
                              "-ExecutionPolicy",
                              "Bypass",
                              "-File",
                              tempScript,
                              "-ProcessId",
                              QString::number(QCoreApplication::applicationPid()),
                              "-Zip",
                              zipPath,
                              "-Dest",
                              appDir};
    if (!QProcess::startDetached("powershell.exe", args)) {
        onLogMessage("Could not start the update helper; install manually by unzipping "
                     + zipPath + " over " + appDir + " after closing.");
        return;
    }
    onLogMessage("Update downloaded - closing to install. The app will restart.");
    close();
}

void MainWindow::onReadReadinessClicked()
{
    m_readinessLabel->setText("Reading readiness...");
    if (m_activeElm) {
        m_elm->readReadiness();
    } else {
        QByteArray payload;
        payload.append(char(0x02));
        payload.append(char(0x01));
        payload.append(char(0x01));
        while (payload.size() < 8)
            payload.append(char(0x00));
        m_connection->sendFrame(0x7DF, false, 0, payload);
    }
    onLogMessage("Reading I/M readiness (Mode 01 PID 01)...");
}

void MainWindow::handleReadiness(const quint8 *d)
{
    // SAE J1979: A = MIL bit + DTC count; B = continuous monitors (bits 0-2
    // available, bits 4-6 incomplete); C/D = non-continuous monitors
    // (available / incomplete bitmasks).
    const bool mil = d[0] & 0x80;
    const int dtcCount = d[0] & 0x7F;

    struct Monitor { QString name; bool available; bool incomplete; };
    QList<Monitor> monitors;
    monitors.append({QStringLiteral("Misfire"), bool(d[1] & 0x01), bool(d[1] & 0x10)});
    monitors.append({QStringLiteral("Fuel System"), bool(d[1] & 0x02), bool(d[1] & 0x20)});
    monitors.append({QStringLiteral("Components"), bool(d[1] & 0x04), bool(d[1] & 0x40)});
    static const char *nonContinuous[8] = {"Catalyst",      "Heated Catalyst",
                                           "EVAP System",   "Secondary Air",
                                           "A/C Refrigerant", "O2 Sensor",
                                           "O2 Heater",     "EGR System"};
    for (int bit = 0; bit < 8; ++bit)
        monitors.append({QString::fromLatin1(nonContinuous[bit]), bool(d[2] & (1 << bit)),
                         bool(d[3] & (1 << bit))});

    const bool darkBg = palette().color(QPalette::Window).lightness() < 128;
    const QString ok = darkBg ? "#5BD75B" : "#1E7A1E";
    const QString bad = darkBg ? "#F26D6D" : "#A33333";
    const QString mut = darkBg ? "#9A9A9A" : "#777777";

    QStringList parts;
    int notReady = 0;
    for (const Monitor &m : monitors) {
        if (!m.available) {
            parts << QString("<span style='color:%1'>%2: n/a</span>").arg(mut, m.name);
        } else if (m.incomplete) {
            parts << QString("<span style='color:%1'>%2: not ready</span>").arg(bad, m.name);
            ++notReady;
        } else {
            parts << QString("<span style='color:%1'>%2: ready</span>").arg(ok, m.name);
        }
    }
    const QString milText = mil ? QString("<b><span style='color:%1'>Check-engine light ON</span></b>").arg(bad)
                                : QString("<b><span style='color:%1'>Check-engine light off</span></b>").arg(ok);
    m_readinessLabel->setText(QString("%1 &nbsp;·&nbsp; %2 stored code(s) &nbsp;·&nbsp; %3")
                                  .arg(milText)
                                  .arg(dtcCount)
                                  .arg(parts.join(QStringLiteral(" &nbsp; "))));
    onLogMessage(QString("Readiness: MIL %1, %2 stored code(s), %3 monitor(s) not ready.")
                     .arg(mil ? "ON" : "off")
                     .arg(dtcCount)
                     .arg(notReady));
}

void MainWindow::handleSupportMask(quint8 basePid, const quint8 *mask)
{
    m_supportMasksSeen |= quint8(1 << (basePid / 0x20));
    // Byte A bit 7 = basePid+1 ... byte D bit 0 = basePid+32 (SAE J1979).
    // Masks from several ECUs are OR'd together: supported anywhere counts.
    for (int byte = 0; byte < 4; ++byte)
        for (int bit = 0; bit < 8; ++bit)
            if (mask[byte] & (0x80 >> bit))
                m_supportedPids.insert(quint8(basePid + byte * 8 + bit + 1));
    updatePidSupportUi();
}

bool MainWindow::isPidKnownUnsupported(quint8 pid) const
{
    const quint8 base = quint8(((pid - 1) / 0x20) * 0x20);
    if (!(m_supportMasksSeen & (1 << (base / 0x20))))
        return false; // that range's mask hasn't arrived; assume supported
    return !m_supportedPids.contains(pid);
}

void MainWindow::markPidRowUnsupported(quint8 pid, bool unsupported)
{
    const auto it = m_pidRow.constFind(pid);
    if (it == m_pidRow.constEnd())
        return;
    for (int col = 0; col < 3; ++col) {
        QTableWidgetItem *item = m_pidTable->item(it.value(), col);
        if (!item)
            continue;
        item->setForeground(unsupported ? QBrush(Qt::gray) : QBrush());
        item->setToolTip(unsupported
                             ? QStringLiteral("Not supported by this vehicle (per the "
                                              "Mode 01 supported-PID bitmask).")
                             : QString());
    }
    if (QTableWidgetItem *value = m_pidTable->item(it.value(), 1))
        value->setText(unsupported ? QStringLiteral("n/a") : QStringLiteral("--"));
}

void MainWindow::updatePidSupportUi()
{
    QSet<quint8> unsupported;
    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();
    for (const PidDefinition &def : defs)
        if (isPidKnownUnsupported(def.pid))
            unsupported.insert(def.pid);
    if (unsupported == m_knownUnsupported)
        return;

    QStringList names;
    for (const PidDefinition &def : defs) {
        const bool now = unsupported.contains(def.pid);
        if (now != m_knownUnsupported.contains(def.pid))
            markPidRowUnsupported(def.pid, now);
        if (now)
            names << def.name;
    }
    m_knownUnsupported = unsupported;
    rebuildGauges();
    if (!names.isEmpty())
        onLogMessage(QString("Vehicle does not support: %1 (marked n/a; gauges hidden).")
                         .arg(names.join(", ")));
}

void MainWindow::resetPidSupport()
{
    m_supportedPids.clear();
    m_supportMasksSeen = 0;
    m_pidAlertState.clear(); // a new/closed session starts with no active alerts
    if (m_knownUnsupported.isEmpty())
        return;
    for (quint8 pid : m_knownUnsupported)
        markPidRowUnsupported(pid, false);
    m_knownUnsupported.clear();
    rebuildGauges();
}

void MainWindow::onOpenEmulator()
{
    // Non-modal so the app can connect to the emulator it is serving (e.g. over
    // TCP localhost) while this window stays open. Created once, reused after.
    if (!m_emulatorWindow)
        m_emulatorWindow = new EmulatorWindow(this);
    m_emulatorWindow->show();
    m_emulatorWindow->raise();
    m_emulatorWindow->activateWindow();
}

void MainWindow::buildLiveDataTable()
{
    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();
    m_pidTable->setRowCount(defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        const PidDefinition &def = defs.at(i);
        m_pidTable->setItem(i, 0, new QTableWidgetItem(def.name));
        m_pidTable->setItem(i, 1, new QTableWidgetItem("--"));
        m_pidTable->setItem(i, 2, new QTableWidgetItem(Units::displayUnit(def.unit, m_imperial)));
        m_pidRow.insert(def.pid, i);
        m_pidUnit.insert(def.pid, def.unit);
    }

    // Same parameter list for the freeze-frame snapshot table.
    m_freezeTable->setRowCount(defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        const PidDefinition &def = defs.at(i);
        m_freezeTable->setItem(i, 0, new QTableWidgetItem(def.name));
        m_freezeTable->setItem(i, 1, new QTableWidgetItem("--"));
        m_freezeTable->setItem(i, 2,
                               new QTableWidgetItem(Units::displayUnit(def.unit, m_imperial)));
        m_freezePidRow.insert(def.pid, i);
    }
}

void MainWindow::buildDashboardTab(QTabWidget *tabs)
{
    auto *dashPage = new QWidget();
    auto *dashLayout = new QVBoxLayout(dashPage);

    // Gauge grid inside a scroll area so it stays usable on small windows. The
    // grid contents come from the saved layout config; see rebuildGauges().
    auto *gaugeContainer = new QWidget();
    m_gaugeGrid = new QGridLayout(gaugeContainer);
    const QVector<PidDefinition> &defs = m_pidMonitor->definitions();

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(gaugeContainer);
    dashLayout->addWidget(scroll, 2);

    rebuildGauges();

    // Live chart with a PID selector, plus the dashboard layout editor.
    auto *chartBar = new QHBoxLayout();
    chartBar->addWidget(new QLabel("Chart:"));
    m_chartPidCombo = new QComboBox();
    for (const PidDefinition &def : defs)
        m_chartPidCombo->addItem(def.name, def.pid);
    chartBar->addWidget(m_chartPidCombo);
    chartBar->addStretch();
    auto *configButton = new QPushButton("Configure Layout...");
    configButton->setToolTip("Choose which gauges appear on the dashboard, their order, "
                             "and how many fit per row. The layout is remembered.");
    connect(configButton, &QPushButton::clicked, this, &MainWindow::onConfigureDashboard);
    chartBar->addWidget(configButton);
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
        m_userDisconnect = true; // intentional: no auto-reconnect
        m_reconnectTimer.stop();
        m_reconnectAttempt = 0;
        if (m_activeElm) {
            m_elm->stopMonitoring();
            m_elm->close();
        } else {
            m_pidMonitor->stop();
            m_connection->close();
        }
        updateMonitorAction(false);
        setConnectedUiState(false);
        setStatus("Disconnected", 'r');
        m_testRequestButton->setEnabled(false);
        m_monitorAction->setEnabled(false);
        setDtcButtonsEnabled(false);
        m_readVinButton->setEnabled(false);
        m_readCalIdButton->setEnabled(false);
        resetPidSupport();
        return;
    }

    // A fresh manual connection supersedes any pending auto-reconnect.
    m_reconnectTimer.stop();
    m_reconnectAttempt = 0;

    ConnectionParams params;
    if (!NewConnectionDialog::getConnectionParams(this, params))
        return; // user cancelled

    const bool serial = params.transport == ConnectionParams::Transport::Serial;
    if (serial && params.serialPort.isEmpty()) {
        onLogMessage("No serial port selected.");
        setStatus("Disconnected", 'r');
        return;
    }
    if (!serial && params.host.isEmpty()) {
        onLogMessage("No host/IP entered.");
        setStatus("Disconnected", 'r');
        return;
    }

    m_lastParams = params;
    m_haveParams = true;
    openConnection(params);
}

void MainWindow::openConnection(const ConnectionParams &params)
{
    m_userDisconnect = false;
    setStatus("Connecting...", 'y');
    m_deviceInfoLabel->setText("No device info yet.");

    const bool serial = params.transport == ConnectionParams::Transport::Serial;
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

void MainWindow::scheduleReconnect()
{
    if (m_reconnectAttempt >= kMaxReconnectAttempts) {
        onLogMessage(QString("Auto-reconnect gave up after %1 attempts. Use Connect to retry.")
                         .arg(kMaxReconnectAttempts));
        m_reconnectAttempt = 0;
        return;
    }
    ++m_reconnectAttempt;
    const int delayMs = kReconnectBaseDelayMs * m_reconnectAttempt;
    setStatus(QString("Reconnecting in %1 s (attempt %2 of %3)...")
                  .arg(delayMs / 1000)
                  .arg(m_reconnectAttempt)
                  .arg(kMaxReconnectAttempts),
              'y');
    onLogMessage(QString("Connection lost - retrying in %1 s.").arg(delayMs / 1000));
    m_reconnectTimer.start(delayMs);
}

void MainWindow::setConnectedUiState(bool connected)
{
    m_connectAction->setText(connected ? "Disconnect" : "Connect...");
}

void MainWindow::setStatus(const QString &text, char kind)
{
    m_statusKind = kind;
    m_statusLabel->setText(text);
    // Pick light or dark variants so the state color never blends into the
    // active style's chrome (e.g. dark red on Toyota red, dark green on blue).
    const bool darkBg = palette().color(QPalette::Window).lightness() < 128;
    const char *color = kind == 'g' ? (darkBg ? "#5BD75B" : "#1E7A1E")
                        : kind == 'y' ? (darkBg ? "#E6C34A" : "#9A7B00")
                                      : (darkBg ? "#F26D6D" : "#A33333");
    m_statusLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(color));
}

void MainWindow::onConnected()
{
    if (m_reconnectAttempt > 0) {
        onLogMessage(QString("Reconnected (attempt %1).").arg(m_reconnectAttempt));
        m_reconnectAttempt = 0;
    }
    setStatus("Connected", 'g');
    m_testRequestButton->setEnabled(true);
    m_monitorAction->setEnabled(true);
    m_readVinButton->setEnabled(true);
    m_readCalIdButton->setEnabled(true);
    setDtcButtonsEnabled(true);

    // We connect over ISO 15765-4 CAN at the detected bus speed.
    m_detectedProtocol = "ISO 15765-4 (CAN)";
    m_protocolValue->setText(m_detectedProtocol);

    // Ask which Mode 01 PIDs this vehicle implements (masks 00/20/40) so the
    // UI can mark parameters the car will never answer. Replies are picked up
    // in onFrameReceived().
    resetPidSupport();
    if (m_activeElm) {
        m_elm->queryPidSupport();
    } else {
        for (int i = 0; i < 3; ++i) {
            QTimer::singleShot(200 * i, this, [this, i]() {
                if (!m_connection->isOpen())
                    return;
                QByteArray payload;
                payload.append(char(0x02));
                payload.append(char(0x01));
                payload.append(char(i * 0x20));
                while (payload.size() < 8)
                    payload.append(char(0x00));
                m_connection->sendFrame(0x7DF, false, 0, payload);
            });
        }
    }
}

void MainWindow::onDisconnected(const QString &reason)
{
    m_pidMonitor->stop();
    m_elm->stopMonitoring();
    updateMonitorAction(false);
    m_monitorAction->setEnabled(false);
    setConnectedUiState(false);
    setStatus(reason.isEmpty() ? QStringLiteral("Disconnected")
                               : QString("Disconnected: %1").arg(reason),
              'r');
    m_testRequestButton->setEnabled(false);
    m_readVinButton->setEnabled(false);
    m_readCalIdButton->setEnabled(false);
    setDtcButtonsEnabled(false);
    resetPidSupport();
    if (!reason.isEmpty())
        onLogMessage("Disconnected: " + reason);

    // FR-1: an unexpected drop (not a user-initiated disconnect) retries.
    if (AppSettings::autoReconnect() && m_haveParams && !m_userDisconnect)
        scheduleReconnect();
}

void MainWindow::setDtcButtonsEnabled(bool enabled)
{
    m_readStoredButton->setEnabled(enabled);
    m_readPendingButton->setEnabled(enabled);
    m_readPermanentButton->setEnabled(enabled);
    m_readFreezeButton->setEnabled(enabled);
    m_readReadinessButton->setEnabled(enabled);
    m_clearDtcButton->setEnabled(enabled);
    if (m_readDtcsAction) // toolbar exists only after buildMenus()
        m_readDtcsAction->setEnabled(enabled);
}

void MainWindow::onReadFreezeFrameClicked()
{
    m_freezeInfoLabel->setText("Reading freeze frame...");
    for (auto it = m_freezePidRow.constBegin(); it != m_freezePidRow.constEnd(); ++it)
        if (QTableWidgetItem *item = m_freezeTable->item(it.value(), 1))
            item->setText("--");

    if (m_activeElm)
        m_elm->readFreezeFrame();
    else
        m_freezeClient->read();
    onLogMessage("Reading freeze frame (service 02)...");
}

void MainWindow::onFreezeFrameDtc(const QString &code, bool present)
{
    if (!present) {
        m_freezeInfoLabel->setText(
            "No freeze frame stored (the ECU captures one when a trouble code sets).");
        onLogMessage("No freeze frame stored.");
        return;
    }
    m_freezeInfoLabel->setText(QString("Freeze frame captured when %1 set - %2")
                                   .arg(code, ObdDtcClient::describeDtc(code)));
    onLogMessage("Freeze frame trigger DTC: " + code);
}

void MainWindow::onFreezeFramePid(quint8 pid, double value, bool ok)
{
    const auto it = m_freezePidRow.constFind(pid);
    if (it == m_freezePidRow.constEnd())
        return;
    QTableWidgetItem *item = m_freezeTable->item(it.value(), 1);
    if (!item)
        return;
    if (!ok) {
        item->setText("n/a");
        return;
    }
    const double shown = Units::display(value, m_pidUnit.value(pid), m_imperial);
    item->setText(QString::number(shown, 'f', 1));
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
        updateMonitorAction(false);
        onLogMessage("Stopped live PID monitoring.");
    } else {
        const int interval = AppSettings::pollIntervalMs();
        if (m_activeElm) m_elm->startMonitoring(interval);
        else m_pidMonitor->start(interval);
        updateMonitorAction(true);
        onLogMessage(QString("Started live PID monitoring (%1 ms between requests).").arg(interval));
    }
}

void MainWindow::onPidUpdated(quint8 pid, double value)
{
    // Decode formulas produce metric values; convert once here so the table,
    // gauges, and chart all show the same (possibly imperial) number.
    const double shown = Units::display(value, m_pidUnit.value(pid), m_imperial);

    // Live Data table.
    const auto it = m_pidRow.constFind(pid);
    if (it != m_pidRow.constEnd()) {
        QTableWidgetItem *item = m_pidTable->item(it.value(), 1);
        if (item)
            item->setText(QString::number(shown, 'f', 1));
    }

    // Dashboard gauge.
    const auto git = m_gauges.constFind(pid);
    if (git != m_gauges.constEnd())
        git.value()->setValue(shown);

   // Live chart, if this PID is the selected series.
    if (m_chartPidCombo->currentData().toUInt() == pid)
        m_chart->addSample(shown);

    // Live-data threshold alerts (Nurdos Meirambek).
    updateThresholdAlert(pid, shown);
}

void MainWindow::updateThresholdAlert(quint8 pid, double shown)
{
    // Find this PID's thresholds, if it has any.
    const GaugeCatalogEntry *cfg = nullptr;
    for (const GaugeCatalogEntry &entry : gaugeCatalog()) {
        if (entry.pid == pid) {
            cfg = &entry;
            break;
        }
    }
    if (!cfg)
        return;

    const QString unit = m_pidUnit.value(pid);
    // Compare in display units so the thresholds track the metric/imperial choice.
    const double high = Units::display(cfg->warn, unit, m_imperial);
    const double low = Units::display(cfg->warnLow, unit, m_imperial);

    int newState = 0;
    if (cfg->hasWarn && shown >= high)
        newState = 1;
    else if (cfg->hasWarnLow && shown <= low)
        newState = -1;

    const int oldState = m_pidAlertState.value(pid, 0);
    if (newState == oldState)
        return; // edge-triggered: only act when the state actually changes
    m_pidAlertState.insert(pid, newState);

    QString pidName;
    for (const PidDefinition &def : ObdPidMonitor::standardPids())
        if (def.pid == pid) { pidName = def.name; break; }
    const QString unitLabel = Units::displayUnit(unit, m_imperial);
    const QString valueText = QString::number(shown, 'f', 1) + " " + unitLabel;

    if (newState != 0) {
        // Entered (or switched) an alert state.
        const QString dir = newState > 0 ? QStringLiteral("HIGH") : QStringLiteral("LOW");
        setStatus(QString::fromUtf8("\xe2\x9a\xa0 ") + pidName + " " + dir + ": " + valueText, 'r');
        onLogMessage(QString("ALERT: %1 %2 (%3).").arg(pidName, dir, valueText));
    } else {
        // Recovered. Announce it, and restore the status once nothing else is
        // still alerting (another PID may still be out of range).
        onLogMessage(QString("%1 back to normal (%2).").arg(pidName, valueText));
        bool anyActive = false;
        for (auto it = m_pidAlertState.constBegin(); it != m_pidAlertState.constEnd(); ++it)
            if (it.value() != 0) { anyActive = true; break; }
        if (!anyActive && (m_connection->isOpen() || m_elm->isOpen()))
            setStatus("Connected", 'g');
    }
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
    // Sniff supported-PID bitmask replies (41 00/20/40 + 4 mask bytes) and
    // readiness replies (41 01 + 4 status bytes) from an OBD response ID.
    // GVRET frames carry the ISO-TP PCI byte first; ELM327 synthesized frames
    // start directly at the service byte.
    if (frame.id >= 0x7E8 && frame.id <= 0x7EF) {
        for (int j = 0; j <= 1; ++j) {
            if (j + 6 > frame.length || frame.data[j] != 0x41)
                continue;
            const quint8 pid = frame.data[j + 1];
            if (pid == 0x00 || pid == 0x20 || pid == 0x40) {
                handleSupportMask(pid, &frame.data[j + 2]);
                break;
            }
            if (pid == 0x01) {
                handleReadiness(&frame.data[j + 2]);
                break;
            }
        }
    }

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

    // While paused, freeze the view: drop pending display frames (recording, if
    // active, still captured them upstream in onFrameReceived).
    if (m_paused) {
        m_pendingFrames.clear();
        return;
    }

    const QVector<CanFrame> batch = std::move(m_pendingFrames);
    m_pendingFrames.clear();

    m_frameModel->addFrames(batch);
    m_summaryModel->addFrames(batch); // keeps the per-ID overview live

    // Auto-scroll only makes sense in the streaming view in arrival order; skip
    // it if the user is in overview mode or has sorted by another column.
    if (m_autoScrollCheck->isChecked() && !m_overviewCheck->isChecked()
        && m_frameView->horizontalHeader()->sortIndicatorSection() == FrameTableModel::Time
        && m_frameView->horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder)
        m_frameView->scrollToBottom();

    m_frameCount += batch.size();
    m_frameCountLabel->setText(QString("Frames: %1").arg(m_frameCount));
}

void MainWindow::onClearClicked()
{
    m_pendingFrames.clear();
    m_frameModel->clear();
    m_summaryModel->clear();
    m_frameCount = 0;
    m_frameCountLabel->setText("Frames: 0");
}

void MainWindow::onPauseClicked()
{
    m_paused = !m_paused;
    m_pauseButton->setText(m_paused ? "Resume" : "Pause");
    onLogMessage(m_paused ? "Raw traffic view paused." : "Raw traffic view resumed.");
}

void MainWindow::onFilterChanged(const QString &text)
{
    // Apply the same ID filter to both the streaming and overview proxies so it
    // works whichever view is showing.
    m_frameProxy->setFilterFixedString(text);
    m_summaryProxy->setFilterFixedString(text);
}

void MainWindow::onOverviewToggled(bool on)
{
    m_frameView->setVisible(!on);
    m_summaryView->setVisible(on);
    // Auto-scroll and Pause only apply to the streaming view.
    m_autoScrollCheck->setEnabled(!on);
    m_pauseButton->setEnabled(!on);
}

void MainWindow::onCopyFrames()
{
    QTableView *view = m_overviewCheck->isChecked() ? m_summaryView : m_frameView;
    const QModelIndexList rows = view->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        onLogMessage("Nothing selected to copy.");
        return;
    }

    auto *model = view->model();
    const int cols = model->columnCount();

    // Header line, then one tab-separated line per selected row (in view order).
    QStringList lines;
    QStringList header;
    for (int c = 0; c < cols; ++c)
        header << model->headerData(c, Qt::Horizontal).toString();
    lines << header.join('\t');

    QList<QModelIndex> sorted = rows;
    std::sort(sorted.begin(), sorted.end(),
              [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &idx : sorted) {
        QStringList cells;
        for (int c = 0; c < cols; ++c)
            cells << model->index(idx.row(), c).data(Qt::DisplayRole).toString();
        lines << cells.join('\t');
    }

    QGuiApplication::clipboard()->setText(lines.join('\n'));
    onLogMessage(QString("Copied %1 row(s) to the clipboard.").arg(rows.size()));
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

    m_decodedMake.clear();
    m_decodedModel.clear();
    m_decodedYear.clear();
    m_decodedTrim.clear();
    m_decodedMakeValue->setText("--");
    m_decodedModelValue->setText("--");
    m_decodedYearValue->setText("--");
    m_decodedTrimValue->setText("--");

    if (!vin.isEmpty()) {
        onLogMessage("Decoding VIN...");
        m_vinDecoder->decode(vin);
    }
}

void MainWindow::onVinDecoded(const VinDecodeResult &result)
{
    if (!result.valid) {
        onLogMessage("VIN decode failed: " + result.error);
        return;
    }

    m_decodedMake = result.make;
    m_decodedModel = result.model;
    m_decodedYear = result.modelYear;
    m_decodedTrim = result.trim;

    m_decodedMakeValue->setText(result.make.isEmpty() ? "(unknown)" : result.make);
    m_decodedModelValue->setText(result.model.isEmpty() ? "(unknown)" : result.model);
    m_decodedYearValue->setText(result.modelYear.isEmpty() ? "(unknown)" : result.modelYear);
    m_decodedTrimValue->setText(result.trim.isEmpty() ? "(unknown)" : result.trim);

    if (result.fromOnline)
        onLogMessage(QString("VIN decoded (NHTSA): %1 %2 %3 %4")
                         .arg(result.modelYear, result.make, result.model, result.trim));
    else
        onLogMessage("VIN decoded offline only: " + result.error);
}

void MainWindow::onCalibrationIdsReceived(const QStringList &ids)
{
    m_calIds = ids;
    m_calIdValue->setText(ids.isEmpty() ? "(none)" : ids.join(", "));
    onLogMessage(QString("Read %1 calibration ID(s).").arg(ids.size()));
}

void MainWindow::onCheckFirmwareClicked()
{
    // FR-10: app releases are published on the project's GitHub, so software
    // update checking is real; the result (update offer, up-to-date, or the
    // failure reason) is reported in a dialog when the check completes.
    checkForUpdates(true);
}

void MainWindow::checkForUpdates(bool verbose)
{
    m_updateCheckVerbose = verbose;
    QNetworkRequest request{QUrl(QString::fromLatin1(kReleasesApiUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, "ObdSuite"); // GitHub requires a UA
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (m_updateCheckVerbose) {
                onLogMessage("Update check failed: " + reply->errorString());
                QMessageBox::warning(this, "Update Check",
                                     "Could not check GitHub for updates:\n"
                                         + reply->errorString());
            }
            return;
        }
        const QJsonObject release =
            QJsonDocument::fromJson(reply->readAll()).array().first().toObject();
        const QString tag = release.value("tag_name").toString();
        QString remoteStr = tag;
        if (remoteStr.startsWith('v') || remoteStr.startsWith('V'))
            remoteStr.remove(0, 1);
        const QVersionNumber remote = QVersionNumber::fromString(remoteStr);
        const QVersionNumber local =
            QVersionNumber::fromString(QApplication::applicationVersion());
        if (!remote.isNull() && remote > local) {
            const QString pageUrl = release.value("html_url").toString();
            // Prefer the packaged ZIP asset for a direct download.
            QString zipUrl;
            const QJsonArray assets = release.value("assets").toArray();
            for (const QJsonValue &asset : assets) {
                const QString url =
                    asset.toObject().value("browser_download_url").toString();
                if (url.endsWith(".zip", Qt::CaseInsensitive)) {
                    zipUrl = url;
                    break;
                }
            }
            onLogMessage(QString("Update available: %1 (installed: v%2) - %3")
                             .arg(tag, QApplication::applicationVersion(), pageUrl));

            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle("Update Available");
            box.setText(QString("OBD Suite %1 is available (installed: v%2).")
                            .arg(tag, QApplication::applicationVersion()));
            box.setInformativeText(zipUrl.isEmpty()
                                       ? QStringLiteral("This release has no packaged ZIP; "
                                                        "see the release page.")
                                       : QStringLiteral(
                                             "Download && Install fetches the update, closes "
                                             "OBD Suite, swaps in the new files and restarts "
                                             "it. Settings and vehicle data are preserved."));
            QPushButton *installButton =
                zipUrl.isEmpty() ? nullptr
                                 : box.addButton("Download && Install", QMessageBox::AcceptRole);
            QPushButton *pageButton =
                box.addButton("Open Release Page", QMessageBox::ActionRole);
            box.addButton("Later", QMessageBox::RejectRole);
            box.exec();
            if (installButton && box.clickedButton() == installButton)
                downloadAndInstallUpdate(zipUrl);
            else if (box.clickedButton() == pageButton)
                QDesktopServices::openUrl(QUrl(pageUrl));
        } else if (m_updateCheckVerbose) {
            onLogMessage(QString("Application is up to date (v%1; latest release: %2).")
                             .arg(QApplication::applicationVersion(),
                                  tag.isEmpty() ? QStringLiteral("none") : tag));
            const QString firmware =
                m_firmwareText.isEmpty() ? QStringLiteral("unknown (connect first)")
                                         : m_firmwareText;
            QMessageBox::information(
                this, "Update Check",
                QString("You are running the latest version (v%1).\n"
                        "Latest published release: %2.\n\n"
                        "Connected scanner firmware: %3")
                    .arg(QApplication::applicationVersion(),
                         tag.isEmpty() ? QStringLiteral("none") : tag, firmware));
        }
    });
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

