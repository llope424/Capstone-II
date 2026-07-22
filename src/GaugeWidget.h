#pragma once

#include <QString>
#include <QWidget>

// A parameter gauge painted with QPainter. The user can pick one of four styles
// (a global personalization preference); all share the same value/threshold
// logic and palette-driven colors, differing only in how they render:
//   Analog    - a 270-degree dial with needle, ticks, and a digital readout.
//   Digital   - a dark round face with large accent-colored digits + unit.
//   Segmented - a semicircular bar of discrete segments filling to the value.
//   Minimal   - just the value, name, and unit; no bezel or dial.
// An optional warning threshold recolors the reading once the value crosses it.
class GaugeWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Style { Analog, Digital, Segmented, Minimal };

    explicit GaugeWidget(const QString &label, const QString &unit,
                         double min, double max, QWidget *parent = nullptr);

    void setValue(double value);
    void setWarnThreshold(double value) { m_warn = value; m_hasWarn = true; update(); }
    void setStyle(Style style) { m_style = style; update(); }

    static Style styleFromString(const QString &s);

    QSize sizeHint() const override { return QSize(170, 170); }
    QSize minimumSizeHint() const override { return QSize(130, 130); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintAnalog(QPainter &p);
    void paintDigital(QPainter &p);
    void paintSegmented(QPainter &p);
    void paintMinimal(QPainter &p);
    bool inWarn() const { return m_hasWarn && m_hasValue && m_value >= m_warn; }
    QString valueText() const;

    QString m_label;
    QString m_unit;
    double m_min;
    double m_max;
    double m_value;
    bool m_hasValue = false;
    double m_warn = 0.0;
    bool m_hasWarn = false;
    Style m_style = Style::Analog;
};
