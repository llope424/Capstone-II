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

GaugeWidget::Style GaugeWidget::styleFromString(const QString &s)
{
    if (s == QLatin1String("digital"))
        return Style::Digital;
    if (s == QLatin1String("segmented"))
        return Style::Segmented;
    if (s == QLatin1String("minimal"))
        return Style::Minimal;
    return Style::Analog;
}

QString GaugeWidget::valueText() const
{
    return m_hasValue ? QString::number(m_value, 'f', 1) : QStringLiteral("--");
}

void GaugeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    switch (m_style) {
    case Style::Digital:   paintDigital(p);   break;
    case Style::Segmented: paintSegmented(p); break;
    case Style::Minimal:   paintMinimal(p);   break;
    case Style::Analog:
    default:               paintAnalog(p);    break;
    }
}

void GaugeWidget::paintAnalog(QPainter &p)
{
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
    const bool warning = inWarn();
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

void GaugeWidget::paintDigital(QPainter &p)
{
    const QPalette pal = palette();
    const QColor faceColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor muted = blend(faceColor, textColor, 0.55);
    const QColor accent = inWarn() ? QColor(0xE0, 0x5A, 0x4B) : pal.color(QPalette::Highlight);

    const int side = qMin(width(), height());
    const QPointF center(width() / 2.0, height() / 2.0);
    const double radius = side / 2.0 - 10.0;

    // Dark round face with a subtle ring.
    p.setBrush(faceColor);
    p.setPen(QPen(blend(faceColor, textColor, 0.22), qMax(2.0, radius * 0.03)));
    p.drawEllipse(center, radius, radius);

    // Parameter name (top, muted).
    QFont nameFont = p.font();
    nameFont.setPointSizeF(qMax(7.0, radius * 0.14));
    p.setFont(nameFont);
    p.setPen(muted);
    p.drawText(QRectF(center.x() - radius, center.y() - radius * 0.64, radius * 2, radius * 0.3),
               Qt::AlignHCenter | Qt::AlignVCenter, m_label);

    // Large digits in the accent color (alarm red in a warn state).
    QFont digitFont = p.font();
    digitFont.setPointSizeF(qMax(12.0, radius * 0.46));
    digitFont.setBold(true);
    p.setFont(digitFont);
    p.setPen(accent);
    p.drawText(QRectF(center.x() - radius, center.y() - radius * 0.28, radius * 2, radius * 0.6),
               Qt::AlignHCenter | Qt::AlignVCenter, valueText());

    // This gauge's own unit, beneath the digits.
    QFont unitFont = p.font();
    unitFont.setPointSizeF(qMax(8.0, radius * 0.17));
    unitFont.setBold(true);
    p.setFont(unitFont);
    p.setPen(blend(accent, faceColor, 0.15));
    p.drawText(QRectF(center.x() - radius, center.y() + radius * 0.34, radius * 2, radius * 0.32),
               Qt::AlignHCenter | Qt::AlignVCenter, m_unit.toUpper());
}

void GaugeWidget::paintSegmented(QPainter &p)
{
    const QPalette pal = palette();
    const QColor faceColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor muted = blend(faceColor, textColor, 0.55);
    const QColor dim = blend(faceColor, textColor, 0.18);
    const QColor accent = pal.color(QPalette::Highlight);
    const QColor alarm(0xE0, 0x5A, 0x4B);

    const double w = width(), h = height();
    const QPointF center(w / 2.0, h * 0.60);
    const double radius = qMin(w, h) * 0.42;

    const double range = (m_max > m_min) ? (m_max - m_min) : 1.0;
    const double clamped = qBound(m_min, m_hasValue ? m_value : m_min, m_max);
    const double frac = (clamped - m_min) / range;
    const double warnFrac = m_hasWarn ? qBound(0.0, (m_warn - m_min) / range, 1.0) : 2.0;

    // ~13 discrete segments across a top semicircle (180 -> 0 deg, open bottom).
    const int segs = 13;
    const double gapDeg = 3.0;
    const double segDeg = (180.0 - gapDeg * (segs - 1)) / segs;
    const QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    const double thick = qMax(6.0, radius * 0.22);

    for (int i = 0; i < segs; ++i) {
        const double segMid = (i + 0.5) / segs;
        const bool on = m_hasValue && segMid <= frac;
        QColor c = dim;
        if (on)
            c = (m_hasWarn && segMid >= warnFrac) ? alarm : accent;
        const double startDeg = 180.0 - i * (segDeg + gapDeg);
        p.setPen(QPen(c, thick, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arcRect, int(startDeg * 16), int(-segDeg * 16));
    }

    // Value inside the arc.
    QFont valueFont = p.font();
    valueFont.setPointSizeF(qMax(11.0, radius * 0.34));
    valueFont.setBold(true);
    p.setFont(valueFont);
    p.setPen(inWarn() ? alarm : textColor);
    p.drawText(QRectF(center.x() - radius, center.y() - radius * 0.5, radius * 2, radius * 0.55),
               Qt::AlignHCenter | Qt::AlignBottom, valueText());

    // Name + unit below.
    QFont smallFont = p.font();
    smallFont.setPointSizeF(qMax(7.0, radius * 0.15));
    smallFont.setBold(false);
    p.setFont(smallFont);
    p.setPen(muted);
    p.drawText(QRectF(0, center.y() + radius * 0.10, w, radius * 0.30),
               Qt::AlignHCenter | Qt::AlignVCenter, m_label + "  (" + m_unit + ")");
}

void GaugeWidget::paintMinimal(QPainter &p)
{
    const QPalette pal = palette();
    const QColor faceColor = pal.color(QPalette::Base);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor muted = blend(faceColor, textColor, 0.5);
    const QColor alarm(0xE0, 0x5A, 0x4B);

    const double w = width(), h = height();
    const double u = qMin(w, h);

    // No bezel or dial: just name, value, unit. The name is elided to the
    // widget width so long parameter names never clip at the edges.
    QFont nameFont = p.font();
    nameFont.setPointSizeF(qMax(7.0, u * 0.075));
    p.setFont(nameFont);
    p.setPen(muted);
    const QString name = p.fontMetrics().elidedText(m_label, Qt::ElideRight, int(w - 8));
    p.drawText(QRectF(0, h * 0.16, w, h * 0.18), Qt::AlignHCenter | Qt::AlignVCenter, name);

    QFont valueFont = p.font();
    valueFont.setPointSizeF(qMax(16.0, u * 0.30));
    valueFont.setBold(true);
    p.setFont(valueFont);
    p.setPen(inWarn() ? alarm : textColor);
    p.drawText(QRectF(0, h * 0.30, w, h * 0.40), Qt::AlignHCenter | Qt::AlignVCenter, valueText());

    QFont unitFont = p.font();
    unitFont.setPointSizeF(qMax(8.0, u * 0.11));
    p.setFont(unitFont);
    p.setPen(muted);
    p.drawText(QRectF(0, h * 0.66, w, h * 0.18), Qt::AlignHCenter | Qt::AlignVCenter, m_unit);
}
