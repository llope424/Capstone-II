#include "GaugeWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
// Gauge sweep: start at 225 deg (lower-left), sweep 270 deg clockwise to -45
// deg (lower-right), leaving the bottom open. Angles are standard math degrees
// (0 = east, counter-clockwise positive); painting converts as needed.
constexpr double kStartAngle = 225.0;
constexpr double kSweep = 270.0;

QColor blend(const QColor &a, const QColor &b, double t)
{
    return QColor(int(a.red() + (b.red() - a.red()) * t),
                  int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}
}

GaugeWidget::GaugeWidget(const QString &label, const QString &unit,
                         double min, double max, QWidget *parent)
    : QWidget(parent), m_label(label), m_unit(unit), m_min(min), m_max(max), m_value(min)
{
}

void GaugeWidget::setValue(double value)
{
    m_value = value;
    m_hasValue = true;
    update();
}

void GaugeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int side = qMin(width(), height());
    const QPointF center(width() / 2.0, height() / 2.0);
    const double radius = side / 2.0 - 10.0;

    const double range = (m_max > m_min) ? (m_max - m_min) : 1.0;
    const double clamped = qBound(m_min, m_hasValue ? m_value : m_min, m_max);
    const double frac = (clamped - m_min) / range;

    // Style-driven colors: the face is the "secondary" (inside-the-dashboard)
    // color, the needle/value arc the "details" accent; the warn state keeps a
    // fixed alarm red regardless of theme.
    const QPalette pal = palette();
    const QColor faceColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const bool warning = m_hasWarn && m_hasValue && m_value >= m_warn;
    const QColor accent = warning ? QColor(0xE0, 0x5A, 0x4B) : pal.color(QPalette::Highlight);
    const QColor track = blend(faceColor, textColor, 0.18);
    const QColor mutedColor = blend(faceColor, textColor, 0.55);

    const QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);

    // Dial face.
    const double penWidth = qMax(6.0, radius * 0.14);
    p.setBrush(faceColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, radius + penWidth / 2 + 2, radius + penWidth / 2 + 2);

    // Track arc (full sweep).
    QPen trackPen(track, qMax(6.0, radius * 0.14), Qt::SolidLine, Qt::RoundCap);
    p.setPen(trackPen);
    p.drawArc(arcRect, int(kStartAngle * 16), int(-kSweep * 16));

    // Value arc (up to current fraction).
    QPen valuePen(accent, qMax(6.0, radius * 0.14), Qt::SolidLine, Qt::RoundCap);
    p.setPen(valuePen);
    p.drawArc(arcRect, int(kStartAngle * 16), int(-kSweep * frac * 16));

    // Tick marks (major).
    const int ticks = 8;
    p.setPen(QPen(mutedColor, 1.5));
    for (int i = 0; i <= ticks; ++i) {
        const double a = kStartAngle - kSweep * (double(i) / ticks);
        const double rad = qDegreesToRadians(a);
        const double rOuter = radius - radius * 0.20;
        const double rInner = rOuter - radius * 0.10;
        const QPointF po(center.x() + rOuter * qCos(rad), center.y() - rOuter * qSin(rad));
        const QPointF pi(center.x() + rInner * qCos(rad), center.y() - rInner * qSin(rad));
        p.drawLine(pi, po);
    }

    // Needle.
    const double needleAngle = kStartAngle - kSweep * frac;
    const double needleRad = qDegreesToRadians(needleAngle);
    const double needleLen = radius - radius * 0.22;
    const QPointF tip(center.x() + needleLen * qCos(needleRad),
                      center.y() - needleLen * qSin(needleRad));
    p.setPen(QPen(accent, qMax(2.0, radius * 0.04), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(center, tip);
    p.setBrush(accent);
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, radius * 0.07, radius * 0.07);

    // Digital value readout.
    QFont valueFont = p.font();
    valueFont.setPointSizeF(qMax(9.0, radius * 0.24));
    valueFont.setBold(true);
    p.setFont(valueFont);
    p.setPen(textColor);
    const QString valueText = m_hasValue ? QString::number(m_value, 'f', 1) : "--";
    const QRectF valueRect(center.x() - radius, center.y() + radius * 0.12,
                           radius * 2, radius * 0.42);
    p.drawText(valueRect, Qt::AlignHCenter | Qt::AlignVCenter, valueText);

    // Label + unit.
    QFont smallFont = p.font();
    smallFont.setPointSizeF(qMax(7.0, radius * 0.13));
    smallFont.setBold(false);
    p.setFont(smallFont);
    p.setPen(mutedColor);
    const QRectF labelRect(center.x() - radius, center.y() - radius * 0.55,
                           radius * 2, radius * 0.3);
    p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter, m_label);

    const QRectF unitRect(center.x() - radius, center.y() + radius * 0.52,
                          radius * 2, radius * 0.25);
    p.drawText(unitRect, Qt::AlignHCenter | Qt::AlignVCenter, m_unit);
}
