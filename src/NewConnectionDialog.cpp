#include "NewConnectionDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

NewConnectionDialog::NewConnectionDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("New Connection");
    setMinimumWidth(440);

    auto *rootLayout = new QVBoxLayout(this);

    // --- Device family ---
    auto *deviceGroup = new QGroupBox("Device");
    auto *deviceLayout = new QVBoxLayout(deviceGroup);
    m_gvretRadio = new QRadioButton("GVRET custom scanner (ESP32 / ESP32RET)");
    m_elmRadio = new QRadioButton("ELM327 commercial adapter");
    m_gvretRadio->setChecked(true);
    connect(m_gvretRadio, &QRadioButton::toggled, this, &NewConnectionDialog::updatePages);
    deviceLayout->addWidget(m_gvretRadio);
    deviceLayout->addWidget(m_elmRadio);
    rootLayout->addWidget(deviceGroup);

    // --- Transport ---
    auto *transportGroup = new QGroupBox("Connection Type");
    auto *transportLayout = new QVBoxLayout(transportGroup);
    m_serialRadio = new QRadioButton("Serial (USB or Bluetooth)");
    m_networkRadio = new QRadioButton("Network (WiFi / TCP)");
    m_serialRadio->setChecked(true);
    connect(m_serialRadio, &QRadioButton::toggled, this, &NewConnectionDialog::updatePages);
    transportLayout->addWidget(m_serialRadio);
    transportLayout->addWidget(m_networkRadio);
    rootLayout->addWidget(transportGroup);

    m_stack = new QStackedWidget(this);

    // Serial page
    auto *serialPage = new QWidget();
    auto *serialLayout = new QFormLayout(serialPage);
    m_portCombo = new QComboBox();
    m_refreshButton = new QPushButton("Refresh");
    connect(m_refreshButton, &QPushButton::clicked, this, &NewConnectionDialog::onRefreshPortsClicked);
    auto *comboAndRefresh = new QWidget();
    auto *comboLayout = new QVBoxLayout(comboAndRefresh);
    comboLayout->setContentsMargins(0, 0, 0, 0);
    comboLayout->addWidget(m_portCombo);
    comboLayout->addWidget(m_refreshButton);
    serialLayout->addRow("Port:", comboAndRefresh);
    m_espModeCheck = new QCheckBox("ESP32 mode (no flow control, DTR/RTS low)");
    m_espModeCheck->setChecked(true);
    serialLayout->addRow("", m_espModeCheck);
    m_stack->addWidget(serialPage);

    // Network page
    auto *networkPage = new QWidget();
    auto *networkLayout = new QFormLayout(networkPage);
    m_hostCombo = new QComboBox();
    m_hostCombo->setEditable(true);
    m_hostCombo->addItem("192.168.4.1");   // ESP32 SoftAP default
    m_hostCombo->addItem("192.168.0.10");  // common WiFi ELM327 default
    m_portSpin = new QSpinBox();
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(23);
    networkLayout->addRow("Host / IP:", m_hostCombo);
    networkLayout->addRow("TCP Port:", m_portSpin);
    m_portHint = new QLabel();
    m_portHint->setWordWrap(true);
    m_portHint->setStyleSheet("color: gray; font-size: 11px;");
    networkLayout->addRow("", m_portHint);
    m_stack->addWidget(networkPage);

    rootLayout->addWidget(m_stack);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    populatePorts();
    updatePages();
}

bool NewConnectionDialog::isElm() const
{
    return m_elmRadio->isChecked();
}

void NewConnectionDialog::updatePages()
{
    m_stack->setCurrentIndex(m_serialRadio->isChecked() ? 0 : 1);

    // ESP32-mode toggle is only meaningful for the GVRET custom scanner.
    m_espModeCheck->setVisible(!isElm());

    // Default network port depends on the device family.
    if (isElm()) {
        if (m_portSpin->value() == 23)
            m_portSpin->setValue(35000);
        m_portHint->setText("WiFi ELM327 adapters are usually 192.168.0.10 port 35000.");
    } else {
        if (m_portSpin->value() == 35000)
            m_portSpin->setValue(23);
        m_portHint->setText("GVRET devices use TCP port 23 (same as SavvyCAN).");
    }
}

void NewConnectionDialog::onRefreshPortsClicked()
{
    populatePorts();
}

void NewConnectionDialog::populatePorts()
{
    m_portCombo->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports)
        m_portCombo->addItem(info.portName() + " - " + info.description(), info.portName());
    if (ports.isEmpty())
        m_portCombo->addItem("No serial ports found");
}

ConnectionParams NewConnectionDialog::params() const
{
    ConnectionParams p;
    p.deviceType = isElm() ? ConnectionParams::DeviceType::Elm327
                           : ConnectionParams::DeviceType::Gvret;
    if (m_serialRadio->isChecked()) {
        p.transport = ConnectionParams::Transport::Serial;
        p.serialPort = m_portCombo->currentData().toString();
        p.espMode = m_espModeCheck->isChecked();
    } else {
        p.transport = ConnectionParams::Transport::Network;
        p.host = m_hostCombo->currentText().trimmed();
        p.tcpPort = static_cast<quint16>(m_portSpin->value());
    }
    return p;
}

bool NewConnectionDialog::getConnectionParams(QWidget *parent, ConnectionParams &outParams)
{
    NewConnectionDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    outParams = dialog.params();
    return true;
}
