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
    const QColor windowText = c.windowTextOverride.isValid()
                                  ? c.windowTextOverride
                                  : (c.textOverride.isValid() ? c.textOverride : textFor(c.main));
    const QColor baseText = c.textOverride.isValid() ? c.textOverride : textFor(c.secondary);
    const QColor button = c.buttonOverride.isValid()
                              ? c.buttonOverride
                              : (c.main.lightness() > 128 ? c.main.darker(106)
                                                          : c.main.lighter(135));
    const QColor buttonText = c.windowTextOverride.isValid()
                                  ? c.windowTextOverride
                                  : (c.textOverride.isValid() ? c.textOverride : textFor(button));

    QPalette p;
    p.setColor(QPalette::Window, c.main);
    p.setColor(QPalette::WindowText, windowText);
    p.setColor(QPalette::Base, c.secondary);
    p.setColor(QPalette::AlternateBase,
               c.secondary.lightness() > 128 ? c.secondary.darker(104) : c.secondary.lighter(125));
    p.setColor(QPalette::Text, baseText);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, buttonText);
    p.setColor(QPalette::Highlight, c.details);
    p.setColor(QPalette::HighlightedText, textFor(c.details));
    p.setColor(QPalette::ToolTipBase, c.main);
    p.setColor(QPalette::ToolTipText, windowText);
    p.setColor(QPalette::Link, c.details);
    p.setColor(QPalette::PlaceholderText, blend(c.secondary, baseText, 0.5));

    // Disabled state: halfway between text and its background.
    p.setColor(QPalette::Disabled, QPalette::WindowText, blend(c.main, windowText, 0.4));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, blend(button, buttonText, 0.4));
    p.setColor(QPalette::Disabled, QPalette::Text, blend(c.secondary, baseText, 0.4));
    p.setColor(QPalette::Disabled, QPalette::Highlight, blend(c.main, c.details, 0.5));
    return p;
}

} // namespace

namespace AppStyle
{

QStringList presetNames()
{
    return {"Light", "Dark",        "Toyota",   "Ford",   "BMW",    "Ferrari",
            "Lamborghini", "Dodge", "DeadPool", "Matrix", "Custom"};
}

StyleColors colorsForPreset(const QString &name)
{
    if (name == QLatin1String("Dark"))
        return {QColor(0x2B, 0x2E, 0x33), QColor(0x24, 0x27, 0x2B), QColor(0x4C, 0xA6, 0xFF)};
    if (name == QLatin1String("Toyota")) // official red/black/white: red chrome, black dials
        // and black buttons/tabs with white lettering. The button color is
        // overridden because deriving it by lightening the red reads as pink;
        // white buttons would break tab labels, which share the chrome text
        // color (white) and need a dark surface under them.
        return {QColor(0xEB, 0x0A, 0x1E), QColor(0x14, 0x14, 0x16), QColor(0xFF, 0xFF, 0xFF),
                QColor(), QColor(), QColor(0x1A, 0x1A, 0x1C)};
    if (name == QLatin1String("Ford")) // official Ford Blue chrome, white dials, Grabber accent
        return {QColor(0x00, 0x09, 0x5B), QColor(0xF4, 0xF8, 0xFD), QColor(0x17, 0x00, 0xF4),
                QColor(), QColor(0xFF, 0xFF, 0xFF)};
    if (name == QLatin1String("BMW")) // roundel white/black/blue: white chrome, black dials
        return {QColor(0xF5, 0xF6, 0xF8), QColor(0x10, 0x12, 0x14), QColor(0x00, 0x66, 0xB1)};
    if (name == QLatin1String("Ferrari")) // Rosso Corsa chrome, tricolore-green dials, yellow details
        return {QColor(0xD4, 0x00, 0x00), QColor(0x0F, 0x35, 0x21), QColor(0xFF, 0xCC, 0x00)};
    if (name == QLatin1String("Lamborghini")) // official black and gold
        return {QColor(0x00, 0x00, 0x00), QColor(0x16, 0x14, 0x0F), QColor(0xFE, 0xD2, 0x60)};
    if (name == QLatin1String("Dodge")) // muscle black with Dodge-red details
        return {QColor(0x0E, 0x0E, 0x10), QColor(0x1C, 0x1B, 0x1E), QColor(0xBA, 0x0C, 0x2F)};
    if (name == QLatin1String("DeadPool")) // pink and white with red details
        return {QColor(0xE8, 0x5D, 0x88), QColor(0xFF, 0xFF, 0xFF), QColor(0xC8, 0x10, 0x2E)};
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
