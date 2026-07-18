#include "LiveChartWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

LiveChartWidget::LiveChartWidget(QWidget *parent) : QWidget(parent)
{
    m_samples.reserve(m_maxSamples);
}

void LiveChartWidget::setSeries(const QString &label, const QString &unit)
{
    m_label = label;
    m_unit = unit;
    m_samples.clear();
    update();
}

void LiveChartWidget::addSample(double value)
{
    m_samples.append(value);
    while (m_samples.size() > m_maxSamples)
        m_samples.removeFirst();
    update();
}

void LiveChartWidget::clearSamples()
{
    m_samples.clear();
    update();
}

void LiveChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Style-driven colors: "secondary" background, "details" series line.
    const QPalette pal = palette();
    const QColor bg = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    auto blend = [](const QColor &a, const QColor &b, double t) {
        return QColor(int(a.red() + (b.red() - a.red()) * t),
                      int(a.green() + (b.green() - a.green()) * t),
                      int(a.blue() + (b.blue() - a.blue()) * t));
    };
    const QColor grid = blend(bg, text, 0.18);
    const QColor axisText = blend(bg, text, 0.55);
    const QColor line = pal.color(QPalette::Highlight);
    const QColor titleColor = text;

    p.fillRect(rect(), bg);

    const int leftMargin = 56;
    const int rightMargin = 12;
    const int topMargin = 24;
    const int bottomMargin = 18;
    const QRectF plot(leftMargin, topMargin,
                      width() - leftMargin - rightMargin,
                      height() - topMargin - bottomMargin);

    // Title.
    p.setPen(titleColor);
    QFont titleFont = p.font();
    titleFont.setBold(true);
    p.setFont(titleFont);
    const QString title = m_label.isEmpty()
                              ? QStringLiteral("Select a parameter to chart")
                              : (m_unit.isEmpty() ? m_label : QString("%1 (%2)").arg(m_label, m_unit));
    p.drawText(QRectF(leftMargin, 2, plot.width(), topMargin - 2),
               Qt::AlignLeft | Qt::AlignVCenter, title);

    p.setPen(QPen(grid, 1));
    p.drawRect(plot);

    if (m_samples.isEmpty()) {
        p.setPen(axisText);
        p.drawText(plot, Qt::AlignCenter, "Waiting for data...");
        return;
    }

    double minV = m_samples.first();
    double maxV = m_samples.first();
    for (double v : m_samples) {
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }
    if (qFuzzyCompare(minV, maxV)) {
        minV -= 1.0;
        maxV += 1.0;
    } else {
        const double pad = (maxV - minV) * 0.1;
        minV -= pad;
        maxV += pad;
    }

    // Horizontal gridlines + Y labels.
    QFont smallFont = p.font();
    smallFont.setBold(false);
    smallFont.setPointSizeF(qMax(7.0, smallFont.pointSizeF() - 1));
    p.setFont(smallFont);
    const int hLines = 4;
    for (int i = 0; i <= hLines; ++i) {
        const double t = double(i) / hLines;
        const double y = plot.bottom() - t * plot.height();
        const double val = minV + t * (maxV - minV);
        p.setPen(QPen(grid, 1, Qt::DotLine));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(axisText);
        p.drawText(QRectF(0, y - 8, leftMargin - 6, 16),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(val, 'f', 1));
    }

    // The series line.
    QPainterPath path;
    const int n = m_samples.size();
    for (int i = 0; i < n; ++i) {
        const double x = plot.left() + (n == 1 ? 0.0 : double(i) / (n - 1) * plot.width());
        const double norm = (m_samples.at(i) - minV) / (maxV - minV);
        const double y = plot.bottom() - norm * plot.height();
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }
    p.setPen(QPen(line, 2));
    p.drawPath(path);
}
