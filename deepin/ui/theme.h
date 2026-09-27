// Semantic status colors only. All structural theming (backgrounds, borders,
// accent, fonts) comes from the DTK widget style / QPalette so the UI follows
// the dde light & dark themes natively. Layout/interaction follow the macOS
// mockups; visual style is pure DDE.
#ifndef FLOWGRID_THEME_H
#define FLOWGRID_THEME_H

#include <QColor>

#include <DGuiApplicationHelper>
#include <DPalette>

namespace Theme {

// Authoritative DTK palette (gotchas 2.2: prefer DPalette semantic roles over
// QPalette and never hardcode theme colors).
inline DTK_GUI_NAMESPACE::DPalette applicationPalette() {
    return DTK_GUI_NAMESPACE::DGuiApplicationHelper::instance()->applicationPalette();
}

// Semantic status colors (readable on both dde light and dark themes).
inline constexpr QColor kSuccess{0x00, 0xB3, 0x6E};
inline constexpr QColor kWarn{0xFF, 0x8C, 0x00};
inline constexpr QColor kBad{0xE0, 0x43, 0x43};

inline QColor latencyColor(double ms) {
    if (ms > 4.0) return kBad;
    if (ms > 2.0) return kWarn;
    return kSuccess;
}

} // namespace Theme

#endif // FLOWGRID_THEME_H
