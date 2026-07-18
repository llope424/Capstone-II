#include "PreferencesDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "AppSettings.h"
#include "AppStyle.h"

namespace {
QString swatchStyle(const QColor &c)
{
    const QString textColor = c.lightness() > 128 ? "#1C1C1E" : "#E8EAED";
    return QString("background-color: %1; color: %2; border: 1px solid #888; padding: 4px;")
        .arg(c.name(), textColor);
}
}

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Preferences");

    auto *outer = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    // --- General tab ---
    auto *general = new QWidget();
    auto *form = new QFormLayout(general);

    const QString deg = QString(QChar(0x00B0)); // avoid a non-ASCII source literal
    m_unitsCombo = new QComboBox(this);
    m_unitsCombo->addItem(QString("Metric (km/h, %1C, kPa)").arg(deg), false);
    m_unitsCombo->addItem(QString("Imperial (mph, %1F, psi)").arg(deg), true);
    m_unitsCombo->setCurrentIndex(AppSettings::imperialUnits() ? 1 : 0);
    form->addRow("Units:", m_unitsCombo);

    m_pollCombo = new QComboBox(this);
    m_pollCombo->addItem("Fast (100 ms)", 100);
    m_pollCombo->addItem("Normal (250 ms)", 250);
    m_pollCombo->addItem("Relaxed (500 ms)", 500);
    m_pollCombo->addItem("Slow (1000 ms)", 1000);
    m_pollCombo->setToolTip("Delay between successive live-data PID requests. "
                            "Fast rates suit direct USB links; slow rates help "
                            "congested Bluetooth/WiFi adapters.");
    const int idx = m_pollCombo->findData(AppSettings::pollIntervalMs());
    m_pollCombo->setCurrentIndex(idx >= 0 ? idx : 1);
    form->addRow("Live data rate:", m_pollCombo);

    tabs->addTab(general, "General");

    // --- Style tab ---
    auto *stylePage = new QWidget();
    auto *styleForm = new QFormLayout(stylePage);

    m_styleCombo = new QComboBox(this);
    m_styleCombo->addItems(AppStyle::presetNames());
    const int styleIdx = m_styleCombo->findText(AppSettings::styleName());
    m_styleCombo->setCurrentIndex(styleIdx >= 0 ? styleIdx : 0);
    styleForm->addRow("Preset:", m_styleCombo);

    m_mainColor = AppSettings::customStyleColor("main");
    m_secondaryColor = AppSettings::customStyleColor("secondary");
    m_detailsColor = AppSettings::customStyleColor("details");
    m_origStyle = AppSettings::styleName();
    m_origMain = m_mainColor;
    m_origSecondary = m_secondaryColor;
    m_origDetails = m_detailsColor;

    m_mainButton = new QPushButton(this);
    m_mainButton->setToolTip("Window chrome - the area outside the dashboard.");
    connect(m_mainButton, &QPushButton::clicked, this, [this]() { pickColor(&m_mainColor); });
    styleForm->addRow("Main color:", m_mainButton);

    m_secondaryButton = new QPushButton(this);
    m_secondaryButton->setToolTip("Content areas - tables, chart, and gauge faces "
                                  "(the inside of the dashboard).");
    connect(m_secondaryButton, &QPushButton::clicked, this,
            [this]() { pickColor(&m_secondaryColor); });
    styleForm->addRow("Secondary color:", m_secondaryButton);

    m_detailsButton = new QPushButton(this);
    m_detailsButton->setToolTip("Accents - gauge needles and arcs, selections, "
                                "and other small details.");
    connect(m_detailsButton, &QPushButton::clicked, this, [this]() { pickColor(&m_detailsColor); });
    styleForm->addRow("Details color:", m_detailsButton);

    auto *hint = new QLabel("Custom colors apply when the preset is \"Custom\".");
    hint->setWordWrap(true);
    styleForm->addRow(hint);

    tabs->addTab(stylePage, "Style");
    outer->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->button(QDialogButtonBox::Apply)
        ->setToolTip("Preview the selected style without closing this window.");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &PreferencesDialog::applyStyleNow);
    outer->addWidget(buttons);

    const auto syncCustomEnabled = [this]() {
        const bool custom = m_styleCombo->currentText() == QLatin1String("Custom");
        m_mainButton->setEnabled(custom);
        m_secondaryButton->setEnabled(custom);
        m_detailsButton->setEnabled(custom);
    };
    connect(m_styleCombo, &QComboBox::currentTextChanged, this, syncCustomEnabled);
    syncCustomEnabled();
    refreshSwatches();
}

void PreferencesDialog::refreshSwatches()
{
    m_mainButton->setText(m_mainColor.name().toUpper());
    m_mainButton->setStyleSheet(swatchStyle(m_mainColor));
    m_secondaryButton->setText(m_secondaryColor.name().toUpper());
    m_secondaryButton->setStyleSheet(swatchStyle(m_secondaryColor));
    m_detailsButton->setText(m_detailsColor.name().toUpper());
    m_detailsButton->setStyleSheet(swatchStyle(m_detailsColor));
}

void PreferencesDialog::pickColor(QColor *target)
{
    const QColor chosen = QColorDialog::getColor(*target, this, "Choose color");
    if (!chosen.isValid())
        return;
    *target = chosen;
    refreshSwatches();
}

void PreferencesDialog::applyStyleNow()
{
    // Persist the style pieces and restyle the app live; General-tab settings
    // wait for OK. Cancel restores the snapshot taken at dialog open.
    AppSettings::setStyleName(m_styleCombo->currentText());
    AppSettings::setCustomStyleColor("main", m_mainColor);
    AppSettings::setCustomStyleColor("secondary", m_secondaryColor);
    AppSettings::setCustomStyleColor("details", m_detailsColor);
    AppStyle::apply(AppSettings::styleName());
}

void PreferencesDialog::accept()
{
    AppSettings::setImperialUnits(m_unitsCombo->currentData().toBool());
    AppSettings::setPollIntervalMs(m_pollCombo->currentData().toInt());
    AppSettings::setStyleName(m_styleCombo->currentText());
    AppSettings::setCustomStyleColor("main", m_mainColor);
    AppSettings::setCustomStyleColor("secondary", m_secondaryColor);
    AppSettings::setCustomStyleColor("details", m_detailsColor);
    QDialog::accept();
}

void PreferencesDialog::reject()
{
    // Undo a live preview: put the stored style back exactly as it was.
    AppSettings::setStyleName(m_origStyle);
    AppSettings::setCustomStyleColor("main", m_origMain);
    AppSettings::setCustomStyleColor("secondary", m_origSecondary);
    AppSettings::setCustomStyleColor("details", m_origDetails);
    AppStyle::apply(m_origStyle);
    QDialog::reject();
}
