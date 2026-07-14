#pragma once

#include <QHash>
#include <QMainWindow>
#include <QTimer>
#include <QVector>

#include "GvretConnection.h"

class ObdPidMonitor;
class ObdDtcClient;
class ObdVehicleInfo;
class SessionLogger;
class VehicleStore;
class Elm327Connection;
class GaugeWidget;
class LiveChartWidget;

QT_BEGIN_NAMESPACE
class QPushButton;
class QLabel;
class QLineEdit;
class QTableWidget;
class QCheckBox;
class QPlainTextEdit;
class QComboBox;
class QTabWidget;
class QAction;
class QListWidget;
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

    // Report
    void onExportReport();
    void onToggleTheme(bool dark);

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

private:
    void setConnectedUiState(bool connected);
    void appendFrameRow(const CanFrame &frame);
    void buildLiveDataTable();
    void buildDashboardTab(QTabWidget *tabs);
    void buildVehicleInfoTab(QTabWidget *tabs);
    void buildVehiclesTab(QTabWidget *tabs);
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

    QPushButton *m_connectButton; // "New Connection..." when idle, "Disconnect" when open
    QLabel *m_statusLabel;
    QLabel *m_deviceInfoLabel;

    // Live Data tab
    QPushButton *m_monitorButton;
    QTableWidget *m_pidTable;
    QHash<quint8, int> m_pidRow; // PID -> row index in m_pidTable

    // Dashboard tab
    QHash<quint8, GaugeWidget *> m_gauges; // PID -> gauge
    LiveChartWidget *m_chart;
    QComboBox *m_chartPidCombo;

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
    QTableWidget *m_frameTable;
    QLabel *m_frameCountLabel;

    QPlainTextEdit *m_logView;

    QString m_detectedProtocol;
    QString m_firmwareText;
    QString m_vinText;
    QStringList m_calIds;

    quint64 m_frameCount = 0;
};
