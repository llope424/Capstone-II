#include "PreferencesDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
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

// Mini palette chip (main | secondary | details) shown beside preset names.
QIcon presetIcon(const StyleColors &c)
{
    QPixmap pm(42, 14);
    QPainter p(&pm);
    p.fillRect(0, 0, 14, 14, c.main);
    p.fillRect(14, 0, 14, 14, c.secondary);
    p.fillRect(28, 0, 14, 14, c.details);
    p.setPen(QColor(0x88, 0x88, 0x88));
    p.drawRect(0, 0, 41, 13);
    return QIcon(pm);
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
    const QStringList presets = AppStyle::presetNames();
    for (const QString &name : presets)
        m_styleCombo->addItem(presetIcon(AppStyle::colorsForPreset(name)), name);
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

    auto *hint = new QLabel("Changes preview instantly on the whole app. Editing a color "
                            "starts a Custom style from the selected preset. Cancel "
                            "restores your previous style.");
    hint->setWordWrap(true);
    styleForm->addRow(hint);

    tabs->addTab(stylePage, "Style");
    outer->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->button(QDialogButtonBox::Apply)->setToolTip("Re-apply the selected style now.");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &PreferencesDialog::applyStyleNow);
    outer->addWidget(buttons);

    connect(m_styleCombo, &QComboBox::currentTextChanged, this,
            [this]() { onPresetChanged(); });

    m_updating = false;
    refreshSwatches();
}

bool PreferencesDialog::isCustom() const
{
    return m_styleCombo->currentText() == QLatin1String("Custom");
}

void PreferencesDialog::onPresetChanged()
{
    if (m_updating)
        return;
    refreshSwatches();
    applyStyleNow();
}

void PreferencesDialog::refreshSwatches()
{
    // The buttons always show the colors currently on screen: the selected
    // preset's triple, or the custom colors when Custom is active.
    StyleColors eff;
    if (isCustom())
        eff = {m_mainColor, m_secondaryColor, m_detailsColor};
    else
        eff = AppStyle::colorsForPreset(m_styleCombo->currentText());

    m_mainButton->setText(eff.main.name().toUpper());
    m_mainButton->setStyleSheet(swatchStyle(eff.main));
    m_secondaryButton->setText(eff.secondary.name().toUpper());
    m_secondaryButton->setStyleSheet(swatchStyle(eff.secondary));
    m_detailsButton->setText(eff.details.name().toUpper());
    m_detailsButton->setStyleSheet(swatchStyle(eff.details));
}

void PreferencesDialog::refreshCustomIcon()
{
    const int idx = m_styleCombo->findText(QStringLiteral("Custom"));
    if (idx >= 0)
        m_styleCombo->setItemIcon(idx,
                                  presetIcon({m_mainColor, m_secondaryColor, m_detailsColor}));
}

void PreferencesDialog::pickColor(QColor *target)
{
    // Editing a preset's color forks it into Custom: seed all three custom
    // colors from the preset so only the picked one changes on screen.
    if (!isCustom()) {
        const StyleColors seed = AppStyle::colorsForPreset(m_styleCombo->currentText());
        m_mainColor = seed.main;
        m_secondaryColor = seed.secondary;
        m_detailsColor = seed.details;
    }

    const QColor chosen = QColorDialog::getColor(*target, this, "Choose color");
    if (!chosen.isValid())
        return;
    *target = chosen;

    m_updating = true;
    m_styleCombo->setCurrentText(QStringLiteral("Custom"));
    m_updating = false;
    refreshCustomIcon();
    refreshSwatches();
    applyStyleNow();
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
    // Undo any live preview: put the stored style back exactly as it was.
    AppSettings::setStyleName(m_origStyle);
    AppSettings::setCustomStyleColor("main", m_origMain);
    AppSettings::setCustomStyleColor("secondary", m_origSecondary);
    AppSettings::setCustomStyleColor("details", m_origDetails);
    AppStyle::apply(m_origStyle);
    QDialog::reject();
}
