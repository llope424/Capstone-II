#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
QT_END_NAMESPACE

// User preferences: theme, measurement units, and live-data polling rate.
// Reads current values from AppSettings on open and writes them back when
// accepted; the caller applies the new settings to the running UI.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    void accept() override;

private:
    QComboBox *m_themeCombo;
    QComboBox *m_unitsCombo;
    QComboBox *m_pollCombo;
};
