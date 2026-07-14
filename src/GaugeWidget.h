#pragma once

#include <QString>
#include <QWidget>

// A round analog gauge painted with QPainter: a 270-degree value arc with tick
// marks, a needle, and a digital readout with label and unit. An optional
// warning threshold recolors the arc/needle once the value crosses it.
class GaugeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GaugeWidget(const QString &label, const QString &unit,
                         double min, double max, QWidget *parent = nullptr);

    void setValue(double value);
    void setWarnThreshold(double value) { m_warn = value; m_hasWarn = true; update(); }

    QSize sizeHint() const override { return QSize(170, 170); }
    QSize minimumSizeHint() const override { return QSize(130, 130); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_label;
    QString m_unit;
    double m_min;
    double m_max;
    double m_value;
    bool m_hasValue = false;
    double m_warn = 0.0;
    bool m_hasWarn = false;
};
