#include "PreferencesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>

#include "AppSettings.h"

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Preferences");

    auto *layout = new QFormLayout(this);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItem("Light", false);
    m_themeCombo->addItem("Dark", true);
    m_themeCombo->setCurrentIndex(AppSettings::darkTheme() ? 1 : 0);
    layout->addRow("Theme:", m_themeCombo);

    const QString deg = QString(QChar(0x00B0)); // avoid a non-ASCII source literal
    m_unitsCombo = new QComboBox(this);
    m_unitsCombo->addItem(QString("Metric (km/h, %1C, kPa)").arg(deg), false);
    m_unitsCombo->addItem(QString("Imperial (mph, %1F, psi)").arg(deg), true);
    m_unitsCombo->setCurrentIndex(AppSettings::imperialUnits() ? 1 : 0);
    layout->addRow("Units:", m_unitsCombo);

    m_pollCombo = new QComboBox(this);
    m_pollCombo->addItem("Fast (100 ms)", 100);
    m_pollCombo->addItem("Normal (250 ms)", 250);
    m_pollCombo->addItem("Relaxed (500 ms)", 500);
    m_pollCombo->addItem("Slow (1000 ms)", 1000);
    m_pollCombo->setToolTip("Delay between successive live-data PID requests. "
                            "Fast rates suit direct USB links; slow rates help "
                            "congested Bluetooth/WiFi adapters.");
    const int current = AppSettings::pollIntervalMs();
    const int idx = m_pollCombo->findData(current);
    m_pollCombo->setCurrentIndex(idx >= 0 ? idx : 1);
    layout->addRow("Live data rate:", m_pollCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);
}

void PreferencesDialog::accept()
{
    AppSettings::setDarkTheme(m_themeCombo->currentData().toBool());
    AppSettings::setImperialUnits(m_unitsCombo->currentData().toBool());
    AppSettings::setPollIntervalMs(m_pollCombo->currentData().toInt());
    QDialog::accept();
}
