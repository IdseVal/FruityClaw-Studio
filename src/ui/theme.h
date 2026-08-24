// The visual voice of the editing surfaces: a deep graphite ground for long
// sessions, one warm amber accent doing all the pointing (playhead, focus,
// armed tool), and a small cycle of muted Track hues. Deliberately not a
// borrowed identity (core document section 8.2): behaviour follows FL Studio,
// the look does not.
#pragma once

#include <QColor>

#include "core/primitives.h"

namespace ui::theme {

inline const QColor kCanvas{0x14, 0x17, 0x1C};
inline const QColor kPanel{0x1C, 0x20, 0x28};
inline const QColor kRuler{0x19, 0x1D, 0x24};
inline const QColor kGridBar{0x2A, 0x2F, 0x39};
inline const QColor kGridBeat{0x1E, 0x23, 0x2B};
inline const QColor kTextPrimary{0xC9, 0xD1, 0xD9};
inline const QColor kTextSecondary{0x78, 0x82, 0x8F};
inline const QColor kAccent{0xE8, 0xA1, 0x3C};

// Muted but distinct hues, assigned to Tracks and Patterns by index when the
// entity has no authored colour.
inline const QColor kHueCycle[] = {
    QColor{0x5B, 0x8D, 0xD9},  // blue
    QColor{0x5B, 0xAE, 0x7E},  // green
    QColor{0xC8, 0x6B, 0x6B},  // clay
    QColor{0x9A, 0x7B, 0xC8},  // violet
    QColor{0xC8, 0x9A, 0x5B},  // ochre
    QColor{0x5B, 0xAE, 0xC0},  // teal
};
inline constexpr int kHueCount = 6;

inline QColor hue(int index) { return kHueCycle[index % kHueCount]; }

inline QColor from_colour(core::Colour value) {
    return QColor{static_cast<int>((value >> 16) & 0xFF),
                  static_cast<int>((value >> 8) & 0xFF),
                  static_cast<int>(value & 0xFF)};
}

}  // namespace ui::theme
