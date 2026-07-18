#pragma once

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QTimer>
#include <QVector>

#include "GvretConnection.h"

class ObdPidMonitor;
class ObdDtcClient;
class ObdVehicleInfo;
class SessionLogger;
class VehicleStore;
class Elm327Connection;
class FrameTableModel;
class FrameSummaryModel;
class GaugeWidget;
class LiveChartWidget;
class EmulatorWindow;

QT_BEGIN_NAMESPACE
class QPushButton;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTableView;
class QSortFilterProxyModel;
class QCheckBox;
class QPlainTextEdit;
class QComboBox;
class QTabWidget;
class QAction;
class QListWidget;
class QGridLayout;
class QCloseEvent;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onConnectButtonClicked();
    void onClearClicked();
    void onSendTestRequestClicked();
    void onMonitorButtonClicked();
    void onPidUpdated(quint8 pid, double value);
    void onReadStoredClicked();
    void onReadPendingClicked();
    void onReadPermanentClicked();
    void onClearDtcsClicked();
    void onDtcsReceived(quint8 mode, const QStringList &codes);
    void onDtcsCleared();

    // Vehicle info
    void onReadVinClicked();
    void onReadCalIdsClicked();
    void onVinReceived(const QString &vin);
    void onCalibrationIdsReceived(const QStringList &ids);
    void onCheckFirmwareClicked();

    // Logging / replay
    void onRecordButtonClicked();
    void onReplayButtonClicked();
    void onFrameReplayed(const CanFrame &frame);
    void onReplayFinished();
    void onPauseClicked();
    void onFilterChanged(const QString &text);
    void onOverviewToggled(bool on);
    void onCopyFrames();

    // Report
    void onExportReport();

    // Personalization
    void onPreferences();
    void onConfigureDashboard();

    // Emulator (in-app ELM327 emulator control window)
    void onOpenEmulator();

    // Vehicles / history
    void onAddVehicle();
    void onEditVehicle();
    void onDeleteVehicle();
    void onVehicleSelectionChanged();
    void onSaveSessionToVehicle();

    void onConnected();
    void onDisconnected(const QString &reason);
    void onFrameReceived(const CanFrame &frame);
    void onDeviceInfoReceived(int buildNumber, int singleWireMode);
    void onBusParamsReceived(int bus0Baud, bool bus0Enabled, int bus1Baud, bool bus1Enabled);
    void onLogMessage(const QString &text);
    void flushPendingFrames();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setConnectedUiState(bool connected);
    void updateMonitorAction(bool running);
    void buildLiveDataTable();
    void buildDashboardTab(QTabWidget *tabs);
    void rebuildGauges();
    void applyDisplayUnits();

    // Supported-PID detection (Mode 01 masks 00/20/40, queried on connect).
    void resetPidSupport();
    void handleSupportMask(quint8 basePid, const quint8 *mask);
    void updatePidSupportUi();
    bool isPidKnownUnsupported(quint8 pid) const;
    void markPidRowUnsupported(quint8 pid, bool unsupported);
    void buildVehicleTab(QTabWidget *tabs);
    QWidget *buildVehicleInfoPane();
    QWidget *buildVehiclesPane();
    void refreshVehicleList();
    void buildMenus();
    void setDtcButtonsEnabled(bool enabled);

    GvretConnection *m_connection;
    ObdPidMonitor *m_pidMonitor;
    ObdDtcClient *m_dtcClient;
    ObdVehicleInfo *m_vehicleInfo;
    SessionLogger *m_logger;
    VehicleStore *m_vehicleStore;
    Elm327Connection *m_elm;   // commercial-adapter backend
    bool m_activeElm = false;  // true when the ELM327 backend is the live connection

    // Incoming frames are buffered here and flushed to the table on a timer so a
    // busy CAN bus (thousands of frames/sec) can't lock up or crash the UI by
    // touching the widget once per frame.
    QVector<CanFrame> m_pendingFrames;
    QTimer m_flushTimer;

    // Quick-access toolbar actions; the status bar carries the labels.
    QAction *m_connectAction = nullptr;  // "Connect..." when idle, "Disconnect" when open
    QAction *m_monitorAction = nullptr;  // Start/Stop Monitoring toggle
    QAction *m_readDtcsAction = nullptr; // enabled only while connected
    QLabel *m_statusLabel;
    QLabel *m_deviceInfoLabel;

    // Live Data tab
    QTableWidget *m_pidTable;
    QHash<quint8, int> m_pidRow; // PID -> row index in m_pidTable

    // Dashboard tab
    QHash<quint8, GaugeWidget *> m_gauges; // PID -> gauge
    QGridLayout *m_gaugeGrid = nullptr;    // rebuilt from the saved layout config
    LiveChartWidget *m_chart;
    QComboBox *m_chartPidCombo;

    // Personalization state
    bool m_imperial = false;            // cached AppSettings::imperialUnits()
    QHash<quint8, QString> m_pidUnit;   // PID -> metric source unit

    // What the connected vehicle reports it implements. A PID counts as
    // unsupported only once the bitmask covering its range has arrived.
    QSet<quint8> m_supportedPids;
    quint8 m_supportMasksSeen = 0;      // bit n = mask for base 0x20*n received
    QSet<quint8> m_knownUnsupported;    // currently marked n/a in the UI

    // Trouble Codes tab
    QPushButton *m_readStoredButton;
    QPushButton *m_readPendingButton;
    QPushButton *m_readPermanentButton;
    QPushButton *m_clearDtcButton;
    QTableWidget *m_dtcTable;

    // Vehicle Info tab
    QPushButton *m_readVinButton;
    QPushButton *m_readCalIdButton;
    QPushButton *m_checkFirmwareButton;
    QLabel *m_vinValue;
    QLabel *m_protocolValue;
    QLabel *m_calIdValue;
    QLabel *m_firmwareValue;

    // Vehicles tab
    QListWidget *m_vehicleList;
    QLabel *m_vehicleDetails;
    QListWidget *m_historyList;
    QPushButton *m_editVehicleButton;
    QPushButton *m_deleteVehicleButton;
    QPushButton *m_saveSessionButton;

    // Raw Traffic tab
    QPushButton *m_testRequestButton;
    QCheckBox *m_autoScrollCheck;
    QPushButton *m_clearButton;
    QPushButton *m_recordButton;
    QPushButton *m_replayButton;
    QPushButton *m_pauseButton;
    QPushButton *m_copyButton;
    QLineEdit *m_filterEdit;
    QCheckBox *m_overviewCheck;
    QTableView *m_frameView;
    FrameTableModel *m_frameModel;
    QSortFilterProxyModel *m_frameProxy;
    QTableView *m_summaryView;
    FrameSummaryModel *m_summaryModel;
    QSortFilterProxyModel *m_summaryProxy;
    QLabel *m_frameCountLabel;
    bool m_paused = false;

    QPlainTextEdit *m_logView;

    QString m_detectedProtocol;
    QString m_firmwareText;
    QString m_vinText;
    QStringList m_calIds;

    quint64 m_frameCount = 0;

    EmulatorWindow *m_emulatorWindow = nullptr; // lazily created, non-modal
};
