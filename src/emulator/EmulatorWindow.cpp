#include "EmulatorWindow.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QVBoxLayout>

#include "Elm327EmulatorEngine.h"
#include "EmulatorTransport.h"
#include "ObdMessageModel.h"

EmulatorWindow::EmulatorWindow(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("ELM327 Emulator"));
    setMinimumSize(560, 460);

    m_model = new ObdMessageModel(this);
    m_model->loadBundled();
    m_engine = new Elm327EmulatorEngine(m_model, this);

    auto *root = new QVBoxLayout(this);

    // --- Transport ---
    auto *transportGroup = new QGroupBox(tr("Transport"), this);
    auto *tLayout = new QFormLayout(transportGroup);
    m_tcpRadio = new QRadioButton(tr("TCP (no setup needed)"), this);
    m_serialRadio = new QRadioButton(tr("Serial (com0com virtual port)"), this);
    m_tcpRadio->setChecked(true);
    tLayout->addRow(m_tcpRadio);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(35000);
    tLayout->addRow(tr("TCP port:"), m_portSpin);

    tLayout->addRow(m_serialRadio);
    auto *serialRow = new QHBoxLayout();
    m_serialCombo = new QComboBox(this);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    serialRow->addWidget(m_serialCombo, 1);
    serialRow->addWidget(m_refreshButton);
    tLayout->addRow(tr("Serial port:"), serialRow);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    tLayout->addRow(m_hintLabel);
    root->addWidget(transportGroup);

    // --- Scenario + controls ---
    auto *controlRow = new QHBoxLayout();
    controlRow->addWidget(new QLabel(tr("Scenario:"), this));
    m_scenarioCombo = new QComboBox(this);
    m_scenarioCombo->addItems(m_model->scenarioNames());
    if (!m_model->activeScenario().isEmpty())
        m_scenarioCombo->setCurrentText(m_model->activeScenario());
    controlRow->addWidget(m_scenarioCombo, 1);
    m_startStopButton = new QPushButton(this);
    controlRow->addWidget(m_startStopButton);
    root->addLayout(controlRow);

    auto *statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("Stopped"), this);
    m_countsLabel = new QLabel(tr("RX 0 / TX 0"), this);
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();
    statusRow->addWidget(m_countsLabel);
    root->addLayout(statusRow);

    // --- Exchange log ---
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setPlaceholderText(tr("Request / response traffic appears here once started."));
    root->addWidget(m_log, 1);

    connect(m_startStopButton, &QPushButton::clicked, this, &EmulatorWindow::onStartStop);
    connect(m_refreshButton, &QPushButton::clicked, this, &EmulatorWindow::refreshSerialPorts);
    connect(m_scenarioCombo, &QComboBox::currentTextChanged, this,
            &EmulatorWindow::onScenarioChanged);

    refreshSerialPorts();
    setRunningUi(false);
}

EmulatorWindow::~EmulatorWindow()
{
    if (m_transport)
        m_transport->stop();
}

void EmulatorWindow::onStartStop()
{
    if (m_transport && m_transport->running()) {
        m_transport->stop();
        m_transport->deleteLater();
        m_transport = nullptr;
        setRunningUi(false);
        return;
    }

    if (m_tcpRadio->isChecked()) {
        m_transport = new TcpEmulatorTransport(m_engine, quint16(m_portSpin->value()), this);
    } else {
        const QString portName = m_serialCombo->currentData().toString();
        if (portName.isEmpty()) {
            m_log->appendPlainText(tr("No serial port selected."));
            return;
        }
        m_transport = new SerialEmulatorTransport(m_engine, portName, this);
    }

    connect(m_transport, &EmulatorTransport::exchanged, this, &EmulatorWindow::onExchanged);
    connect(m_transport, &EmulatorTransport::statusChanged, this,
            &EmulatorWindow::onStatusChanged);
    connect(m_transport, &EmulatorTransport::counts, this, &EmulatorWindow::onCounts);

    QString err;
    if (!m_transport->start(&err)) {
        m_log->appendPlainText(tr("Failed to start: %1").arg(err));
        m_transport->deleteLater();
        m_transport = nullptr;
        return;
    }

    if (m_tcpRadio->isChecked()) {
        const quint16 port = static_cast<TcpEmulatorTransport *>(m_transport)->port();
        m_log->appendPlainText(
            tr("Listening on TCP 127.0.0.1:%1 — connect via New Connection -> ELM327 -> Network.")
                .arg(port));
    } else {
        m_log->appendPlainText(tr("Serving on %1 — connect the app to the paired com0com port.")
                                   .arg(m_serialCombo->currentData().toString()));
    }
    setRunningUi(true);
}

void EmulatorWindow::onScenarioChanged(const QString &name)
{
    m_model->setActiveScenario(name);
    m_log->appendPlainText(tr("Scenario -> %1").arg(name));
}

void EmulatorWindow::refreshSerialPorts()
{
    m_serialCombo->clear();
    bool sawCom0com = false;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString desc = info.description();
        const bool isCom0com = desc.contains(QStringLiteral("com0com"), Qt::CaseInsensitive);
        sawCom0com = sawCom0com || isCom0com;
        m_serialCombo->addItem(info.portName() + (desc.isEmpty() ? QString() : " - " + desc),
                               info.portName());
    }
    if (ports.isEmpty())
        m_serialCombo->addItem(tr("No serial ports found"), QString());

    m_hintLabel->setText(sawCom0com
                             ? tr("com0com pair detected. Pick one end here; connect the app to "
                                  "the other end.")
                             : tr("No com0com port detected. TCP needs no setup. For serial, "
                                  "install com0com and create a COM11<->COM12 pair."));
}

void EmulatorWindow::onExchanged(const QByteArray &rx, const QByteArray &tx)
{
    QByteArray t = tx;
    t.replace('\r', "\\r");
    m_log->appendPlainText(QStringLiteral("%1 -> %2")
                               .arg(QString::fromLatin1(rx), QString::fromLatin1(t)));
}

void EmulatorWindow::onStatusChanged(const QString &status) { m_statusLabel->setText(status); }

void EmulatorWindow::onCounts(quint64 rx, quint64 tx)
{
    m_countsLabel->setText(tr("RX %1 / TX %2").arg(rx).arg(tx));
}

void EmulatorWindow::setRunningUi(bool running)
{
    m_startStopButton->setText(running ? tr("Stop") : tr("Start Emulator"));
    m_tcpRadio->setEnabled(!running);
    m_serialRadio->setEnabled(!running);
    m_portSpin->setEnabled(!running);
    m_serialCombo->setEnabled(!running);
    m_refreshButton->setEnabled(!running);
    if (!running)
        m_statusLabel->setText(tr("Stopped"));
}
