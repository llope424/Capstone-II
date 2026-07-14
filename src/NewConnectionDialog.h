#pragma once

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QRadioButton;
class QComboBox;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QStackedWidget;
class QLabel;
QT_END_NAMESPACE

struct ConnectionParams
{
    enum class DeviceType { Gvret, Elm327 } deviceType = DeviceType::Gvret;
    enum class Transport { Serial, Network } transport = Transport::Serial;

    QString serialPort;
    bool espMode = true;        // GVRET serial only
    QString host;
    quint16 tcpPort = 23;       // GVRET default 23; ELM327 WiFi default 35000
};

// "New Connection" picker: choose the device family (GVRET custom scanner vs an
// ELM327 commercial adapter) and the transport (Serial USB/Bluetooth, or Network
// WiFi/TCP).
class NewConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewConnectionDialog(QWidget *parent = nullptr);

    ConnectionParams params() const;
    static bool getConnectionParams(QWidget *parent, ConnectionParams &outParams);

private slots:
    void onRefreshPortsClicked();
    void updatePages();

private:
    void populatePorts();
    bool isElm() const;

    QRadioButton *m_gvretRadio;
    QRadioButton *m_elmRadio;

    QRadioButton *m_serialRadio;
    QRadioButton *m_networkRadio;
    QStackedWidget *m_stack;

    // Serial page
    QComboBox *m_portCombo;
    QPushButton *m_refreshButton;
    QCheckBox *m_espModeCheck;

    // Network page
    QComboBox *m_hostCombo;
    QSpinBox *m_portSpin;
    QLabel *m_portHint;
};
