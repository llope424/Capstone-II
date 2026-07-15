#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
QT_END_NAMESPACE

class ObdMessageModel;
class Elm327EmulatorEngine;
class EmulatorTransport;

// In-app control window for the built-in ELM327 emulator. Owns the data model +
// engine, serves them over TCP or a serial (com0com) port, and shows a live log.
// This is the Widgets version shipped for peers now; the Phase-2 UI redesign will
// replace the presentation while reusing the same engine/model/transport backend.
class EmulatorWindow : public QDialog
{
    Q_OBJECT

public:
    explicit EmulatorWindow(QWidget *parent = nullptr);
    ~EmulatorWindow() override;

private slots:
    void onStartStop();
    void onScenarioChanged(const QString &name);
    void refreshSerialPorts();
    void onExchanged(const QByteArray &rx, const QByteArray &tx);
    void onStatusChanged(const QString &status);
    void onCounts(quint64 rx, quint64 tx);

private:
    void setRunningUi(bool running);

    ObdMessageModel *m_model;
    Elm327EmulatorEngine *m_engine;
    EmulatorTransport *m_transport = nullptr;

    QRadioButton *m_tcpRadio;
    QRadioButton *m_serialRadio;
    QSpinBox *m_portSpin;
    QComboBox *m_serialCombo;
    QPushButton *m_refreshButton;
    QComboBox *m_scenarioCombo;
    QPushButton *m_startStopButton;
    QLabel *m_statusLabel;
    QLabel *m_countsLabel;
    QLabel *m_hintLabel;
    QPlainTextEdit *m_log;
};
