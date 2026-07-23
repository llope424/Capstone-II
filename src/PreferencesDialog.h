#pragma once

#include <QColor>
#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QPushButton;
QT_END_NAMESPACE

// User preferences in two tabs: General (units, live-data polling rate) and
// Style. Style selection is live: choosing a preset (each shown with a mini
// palette chip) or editing a color restyles the whole application instantly.
// Editing a color while a preset is selected seeds "Custom" from that preset,
// so users can start from a brand style and tweak it. OK keeps everything,
// Cancel restores the style exactly as it was when the dialog opened.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    void accept() override;
    void reject() override; // reverts any live-previewed style

private:
    void onPresetChanged();
    void pickColor(QColor *target);
    void refreshSwatches();
    void refreshCustomIcon();
    void applyStyleNow();
    bool isCustom() const;

    QComboBox *m_unitsCombo;
    QComboBox *m_pollCombo;
    QCheckBox *m_reconnectCheck;
    QComboBox *m_gaugeStyleCombo;

    QComboBox *m_styleCombo;
    QPushButton *m_mainButton;
    QPushButton *m_secondaryButton;
    QPushButton *m_detailsButton;
    QColor m_mainColor;
    QColor m_secondaryColor;
    QColor m_detailsColor;

    // Snapshot at dialog-open time so Cancel can undo live previews.
    QString m_origStyle;
    QColor m_origMain;
    QColor m_origSecondary;
    QColor m_origDetails;

    bool m_updating = true; // suppresses live-apply during construction
};
