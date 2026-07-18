#include "AppStyle.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include "AppSettings.h"

namespace {

QColor blend(const QColor &a, const QColor &b, double t)
{
    return QColor(int(a.red() + (b.red() - a.red()) * t),
                  int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

// Readable text for a given background: near-black on light, near-white on dark.
QColor textFor(const QColor &bg)
{
    return bg.lightness() > 128 ? QColor(0x1C, 0x1C, 0x1E) : QColor(0xE8, 0xEA, 0xED);
}

QPalette buildPalette(const StyleColors &c)
{
    const QColor windowText = c.textOverride.isValid() ? c.textOverride : textFor(c.main);
    const QColor baseText = c.textOverride.isValid() ? c.textOverride : textFor(c.secondary);
    const QColor button = c.main.lightness() > 128 ? c.main.darker(106) : c.main.lighter(135);

    QPalette p;
    p.setColor(QPalette::Window, c.main);
    p.setColor(QPalette::WindowText, windowText);
    p.setColor(QPalette::Base, c.secondary);
    p.setColor(QPalette::AlternateBase,
               c.secondary.lightness() > 128 ? c.secondary.darker(104) : c.secondary.lighter(125));
    p.setColor(QPalette::Text, baseText);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, windowText);
    p.setColor(QPalette::Highlight, c.details);
    p.setColor(QPalette::HighlightedText, textFor(c.details));
    p.setColor(QPalette::ToolTipBase, c.main);
    p.setColor(QPalette::ToolTipText, windowText);
    p.setColor(QPalette::Link, c.details);
    p.setColor(QPalette::PlaceholderText, blend(c.secondary, baseText, 0.5));

    // Disabled state: halfway between text and its background.
    p.setColor(QPalette::Disabled, QPalette::WindowText, blend(c.main, windowText, 0.4));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, blend(button, windowText, 0.4));
    p.setColor(QPalette::Disabled, QPalette::Text, blend(c.secondary, baseText, 0.4));
    p.setColor(QPalette::Disabled, QPalette::Highlight, blend(c.main, c.details, 0.5));
    return p;
}

} // namespace

namespace AppStyle
{

QStringList presetNames()
{
    return {"Light", "Dark", "Toyota", "Ford", "BMW", "Matrix", "Custom"};
}

StyleColors colorsForPreset(const QString &name)
{
    if (name == QLatin1String("Dark"))
        return {QColor(0x2B, 0x2E, 0x33), QColor(0x24, 0x27, 0x2B), QColor(0x4C, 0xA6, 0xFF), QColor()};
    if (name == QLatin1String("Toyota")) // black / red / white
        return {QColor(0x17, 0x17, 0x1A), QColor(0x23, 0x23, 0x27), QColor(0xEB, 0x0A, 0x1E), QColor()};
    if (name == QLatin1String("Ford")) // blue / white
        return {QColor(0x00, 0x34, 0x78), QColor(0xF4, 0xF8, 0xFD), QColor(0x3C, 0x7D, 0xD9), QColor()};
    if (name == QLatin1String("BMW")) // black / blue / white
        return {QColor(0x1A, 0x1D, 0x21), QColor(0x24, 0x28, 0x2E), QColor(0x1C, 0x69, 0xD4), QColor()};
    if (name == QLatin1String("Matrix")) // green console-on-black
        return {QColor(0x00, 0x00, 0x00), QColor(0x05, 0x0D, 0x06), QColor(0x00, 0xFF, 0x41),
                QColor(0x00, 0xD9, 0x39)};
    if (name == QLatin1String("Custom"))
        return {AppSettings::customStyleColor("main"), AppSettings::customStyleColor("secondary"),
                AppSettings::customStyleColor("details"), QColor()};
    // "Light" and anything unknown.
    return {QColor(0xF2, 0xF2, 0xF2), QColor(0xFF, 0xFF, 0xFF), QColor(0x38, 0x74, 0xD8), QColor()};
}

void apply(const QString &presetName)
{
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setPalette(buildPalette(colorsForPreset(presetName)));
}

}
