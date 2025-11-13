#pragma once
// Reusable UI icon and label drawing utilities
// Works with LGFX_Sprite for double-buffered rendering

#include <Arduino.h>
#include "display_config.hpp"
#include "arc_utils.hpp"  // for deg2rad / polarPoint

namespace ui_icon {

using ui_arc::deg2rad;
using ui_arc::polarPoint;

// Draw the speed needle with a small shadow, given the final angle in UI degrees
inline void drawSpeedNeedle(LGFX_Sprite &spr, int cx, int cy, float rInner, float angleDeg, bool darkMode) {
    float needleRad = deg2rad(angleDeg);
    const float gapFromArc = 2.0f;
    const float visibleLength = 30.0f;
    float needleTip = rInner - gapFromArc;
    float needleStart = needleTip - visibleLength;
    float perpRad = needleRad + PI / 2.0f;

    float baseWidth = 3.0f;
    int bx1 = cx + (int)(cosf(needleRad) * needleStart + cosf(perpRad) * baseWidth);
    int by1 = cy + (int)(sinf(needleRad) * needleStart + sinf(perpRad) * baseWidth);
    int bx2 = cx + (int)(cosf(needleRad) * needleStart - cosf(perpRad) * baseWidth);
    int by2 = cy + (int)(sinf(needleRad) * needleStart - sinf(perpRad) * baseWidth);

    float tipWidth = 1.5f;
    int tx1 = cx + (int)(cosf(needleRad) * needleTip + cosf(perpRad) * tipWidth);
    int ty1 = cy + (int)(sinf(needleRad) * needleTip + sinf(perpRad) * tipWidth);
    int tx2 = cx + (int)(cosf(needleRad) * needleTip - cosf(perpRad) * tipWidth);
    int ty2 = cy + (int)(sinf(needleRad) * needleTip - sinf(perpRad) * tipWidth);

    uint16_t shadowColor = darkMode ? 0x0841 : 0x8C92;
    spr.fillTriangle(bx1 + 2, by1 + 2, bx2 + 2, by2 + 2, tx1 + 2, ty1 + 2, shadowColor);
    spr.fillTriangle(bx2 + 2, by2 + 2, tx1 + 2, ty1 + 2, tx2 + 2, ty2 + 2, shadowColor);

    spr.fillTriangle(bx1, by1, bx2, by2, tx1, ty1, TFT_RED);
    spr.fillTriangle(bx2, by2, tx1, ty1, tx2, ty2, TFT_RED);
}

// Small USB plug icon to denote USB power at bottom area
inline void drawUSBPlugIcon(LGFX_Sprite &spr, int x, int y, uint16_t color, uint16_t bg) {
    int o = -9;
    spr.fillRect(x + o - 8, y - 1, 8, 2, color);
    spr.fillRoundRect(x + o, y - 5, 12, 10, 2, color);
    spr.fillRoundRect(x + o + 12, y - 3, 6, 6, 1, color);
    spr.drawLine(x + o + 14, y - 1, x + o + 14, y + 1, bg);
    spr.drawLine(x + o + 16, y - 1, x + o + 16, y + 1, bg);
}

// Battery outline with fill proportional to percent (0..100)
inline void drawBatteryIcon(LGFX_Sprite &spr, int x, int y, int percent, bool low, uint16_t color) {
    spr.drawRect(x - 12, y - 8, 24, 14, color);
    spr.fillRect(x + 12, y - 4, 2, 6, color);
    int fillPix = (constrain(percent, 0, 100) * 20) / 100;
    if (fillPix > 0) spr.fillRect(x - 10, y - 6, fillPix, 10, color);
}

// Simple satellite dish icon with 3 arc lines and LNB
inline void drawSatelliteIcon(LGFX_Sprite &spr, int x, int y, uint16_t color, uint16_t bg) {
    spr.fillRect(x - 2, y - 5, 8, 10, color);
    spr.drawRect(x - 2, y - 5, 8, 10, color);
    spr.fillRect(x - 10, y - 3, 7, 6, color);
    spr.drawLine(x - 10, y - 1, x - 3, y - 1, bg);
    spr.drawLine(x - 10, y + 1, x - 3, y + 1, bg);
    spr.drawLine(x + 2, y - 5, x + 2, y - 9, color);
    spr.fillCircle(x + 2, y - 10, 2, color);
    for (int i = 1; i <= 3; i++) {
        spr.drawArc(x + 6, y, 4 + i * 3, 4 + i * 3, 315, 45, color);
    }
}

// Flashing low battery label near battery icon anchor (same placement as legacy)
inline void drawLowBatteryLabel(LGFX_Sprite &spr, int batIconX, int batIconY, uint16_t textColor, uint16_t bg) {
    spr.setTextDatum(TL_DATUM);
    spr.setFont(&fonts::FreeSansBold12pt7b);
    spr.setTextColor(textColor, bg);
    spr.drawString("LOW", batIconX + 18 - 74 + 17, batIconY - 16 - 66 + 37 - 5);
    spr.drawString("BAT", batIconX + 18 - 52,      batIconY - 16 - 66 + 37 - 5 + 19);
    spr.setFont(nullptr);
}

// Flashing NO FIX label near satellite icon anchor, mirrored from LOW BAT
inline void drawNoFixLabel(LGFX_Sprite &spr, int satIconX, int satIconY, uint16_t textColor, uint16_t bg) {
    spr.setTextDatum(TL_DATUM);
    spr.setFont(&fonts::FreeSansBold12pt7b);
    spr.setTextColor(textColor, bg);
    int noX  = satIconX - 18 + 74 - 17 - 15 - 5;
    int noY  = satIconY - 16 - 66 + 37 - 5 - 2;
    int fixX = satIconX - 18 + 74 - 17 - 22;
    int fixY = noY + 19;
    spr.drawString("NO",  noX,  noY);
    spr.drawString("FIX", fixX, fixY);
    spr.setFont(nullptr);
}

// Sun icon (light mode) or Moon icon (dark mode) at given position
inline void drawSunMoonIcon(LGFX_Sprite &spr, int x, int y, bool darkMode, uint16_t color, uint16_t bg) {
    if (darkMode) {
        spr.fillCircle(x, y, 11, color);
        spr.fillCircle(x + 6, y - 3, 10, bg);
    } else {
        spr.fillCircle(x, y, 10, color);
        for (int i = 0; i < 8; i++) {
            float ang = i * 45.0f;
            int rx1, ry1, rx2, ry2;
            polarPoint(x, y, 12, ang, rx1, ry1);
            polarPoint(x, y, 18, ang, rx2, ry2);
            spr.drawLine(rx1, ry1, rx2, ry2, color);
        }
    }
}

} // namespace ui_icon
