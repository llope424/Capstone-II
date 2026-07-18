#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

// Application-wide color styling built from three user-facing roles:
//   main      - window chrome, the "outside" of the dashboard
//   secondary - content areas (tables, chart, gauge faces), the "inside"
//   details   - accent: gauge needles/arcs, highlights, selections
// Presets are fixed triples of those roles (several inspired by car brands);
// "Custom" reads the user's own three colors from AppSettings.
struct StyleColors
{
    QColor main;
    QColor secondary;
    QColor details;
    QColor textOverride;       // invalid = derive text from background luminance
    QColor windowTextOverride; // chrome text only (menu/labels), overrides derivation
    QColor buttonOverride;     // buttons/tabs; invalid = derived by lightening main
                               // (which turns saturated reds pink - override to avoid)
};

namespace AppStyle
{
// Preset names in display order; "Custom" is always last.
QStringList presetNames();

StyleColors colorsForPreset(const QString &name);

// Applies the preset (Fusion style + a palette derived from its colors) to
// the whole application. Unknown names fall back to "Light".
void apply(const QString &presetName);
}
