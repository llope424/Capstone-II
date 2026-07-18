#pragma once

#include <QColor>
#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QPushButton;
QT_END_NAMESPACE

// User preferences in two tabs: General (units, live-data polling rate) and
// Style (color preset incl. car-brand themes, plus the three custom colors:
// main / secondary / details). Reads current values from AppSettings on open
// and writes them back when accepted; the caller applies the new settings to
// the running UI.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    void accept() override;
    void reject() override; // reverts any live-previewed (Apply) style

private:
    void refreshSwatches();
    void pickColor(QColor *target);
    void applyStyleNow(); // Apply button: preview the style without closing

    QComboBox *m_unitsCombo;
    QComboBox *m_pollCombo;

    QComboBox *m_styleCombo;
    QPushButton *m_mainButton;
    QPushButton *m_secondaryButton;
    QPushButton *m_detailsButton;
    QColor m_mainColor;
    QColor m_secondaryColor;
    QColor m_detailsColor;

    // Snapshot at dialog-open time so Cancel can undo an Apply preview.
    QString m_origStyle;
    QColor m_origMain;
    QColor m_origSecondary;
    QColor m_origDetails;
};
