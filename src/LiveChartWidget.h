#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

// A lightweight rolling strip chart painted with QPainter: keeps a fixed-length
// buffer of samples for one series and draws an auto-scaled line with axes and
// gridlines. No external charting dependency.
class LiveChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LiveChartWidget(QWidget *parent = nullptr);

    void setSeries(const QString &label, const QString &unit);
    void addSample(double value);
    void clearSamples();

    QSize minimumSizeHint() const override { return QSize(320, 160); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_label;
    QString m_unit;
    QVector<double> m_samples;
    int m_maxSamples = 300;
};
