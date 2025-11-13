#pragma once
// Reusable arc rendering utilities for circular gauge UI
// Angles use UI convention: 0° at 12 o'clock, increasing clockwise (0→90→180→270).
// Functions here operate on an LGFX_Sprite to allow double-buffered rendering.

#include <Arduino.h>
#include "display_config.hpp"   // Provides LGFX / LGFX_Sprite types

namespace ui_arc {

static constexpr float ARC_STEP_DEGREES = 3.0f; // same resolution as legacy
// Avoid conflict with Arduino's macro DEG_TO_RAD
static constexpr float kDegToRad = PI / 180.0f;

// Convert UI degrees (0° at 12 o'clock, clockwise) to radians (standard math orientation)
static inline float deg2rad(float deg) { return (deg - 90.0f) * kDegToRad; }

// Compute cartesian point from center, radius and UI degrees
static inline void polarPoint(int cx, int cy, float r, float deg, int &x, int &y) {
    float th = deg2rad(deg);
    x = cx + (int)roundf(cosf(th) * r);
    y = cy + (int)roundf(sinf(th) * r);
}

// Normalize angle into [0,360)
static inline float norm360(float a) { while (a < 0) a += 360.0f; while (a >= 360.0f) a -= 360.0f; return a; }

// Low-level filled arc sector (no wrap handling). Assumes startDeg <= endDeg within same 0..360 range.
static inline void fillArcRaw(LGFX_Sprite &spr, int cx, int cy, float rInner, float rOuter,
                              float startDeg, float endDeg, uint16_t color) {
    if (endDeg < startDeg) return; // caller ensures ordering
    int px_i, py_i, px_o, py_o, qx_i, qy_i, qx_o, qy_o;
    polarPoint(cx, cy, rInner, startDeg, px_i, py_i);
    polarPoint(cx, cy, rOuter, startDeg, px_o, py_o);
    for (float a = startDeg + ARC_STEP_DEGREES; a <= endDeg + 0.001f; a += ARC_STEP_DEGREES) {
        float cur = (a > endDeg) ? endDeg : a;
        polarPoint(cx, cy, rInner, cur, qx_i, qy_i);
        polarPoint(cx, cy, rOuter, cur, qx_o, qy_o);
        spr.fillTriangle(px_i, py_i, px_o, py_o, qx_o, qy_o, color);
        spr.fillTriangle(px_i, py_i, qx_i, qy_i, qx_o, qy_o, color);
        px_i = qx_i; py_i = qy_i; px_o = qx_o; py_o = qy_o;
    }
}

// Public filled arc that supports wrap across 360° (e.g. start=300 end=60).
static inline void fillArc(LGFX_Sprite &spr, int cx, int cy, float rInner, float rOuter,
                           float startDeg, float endDeg, uint16_t color) {
    startDeg = norm360(startDeg); endDeg = norm360(endDeg);
    if (startDeg == endDeg) return; // zero length
    // If arc does not wrap
    if ((startDeg < endDeg && (endDeg - startDeg) <= 360.0f)) {
        fillArcRaw(spr, cx, cy, rInner, rOuter, startDeg, endDeg, color);
        return;
    }
    // Wrapped arc (start after end) -> split into two segments
    // Example start=240 end=120 -> segment 240..360 and 0..120
    fillArcRaw(spr, cx, cy, rInner, rOuter, startDeg, 360.0f, color);
    fillArcRaw(spr, cx, cy, rInner, rOuter, 0.0f, endDeg, color);
}

// Draw speed gauge with 3 colored zones (green/yellow/red) and background.
// Returns the final needle angle (normalized 0..360)
static inline float drawSpeedGauge(LGFX_Sprite &spr, int cx, int cy,
                                   float rInner, float rOuter,
                                   float startDeg, float spanDeg,
                                   float speedValue, float maxValue,
                                   uint16_t colBg, uint16_t colLow, uint16_t colMid, uint16_t colHigh) {
    // Background arc (wrapped 240 -> 120, for example)
    float endDeg = norm360(startDeg - spanDeg);
    fillArc(spr, cx, cy, rInner, rOuter, startDeg, endDeg, colBg);

    float fillFraction = constrain(speedValue / maxValue, 0.0f, 1.0f);
    float fillDeg = fillFraction * spanDeg;
    if (fillDeg <= 0.0f) return norm360(startDeg); // no fill -> needle at start

    // Zone thresholds (same proportions as legacy)
    float greenLimit = spanDeg * 0.60f;
    float yellowLimit = spanDeg * 0.85f;

    auto drawZone = [&](float zoneStartOffset, float zoneEndOffset, uint16_t color) {
        float zs = startDeg + zoneStartOffset; float ze = startDeg + zoneEndOffset;
        // Convert to wrapped logic using fillArc
        if (zoneStartOffset >= spanDeg) return; // nothing
        if (zoneEndOffset > fillDeg) ze = startDeg + fillDeg;
        // Normalize and draw accounting for wrap
        zs = norm360(zs); ze = norm360(ze);
        fillArc(spr, cx, cy, rInner, rOuter, zs, ze, color);
    };

    // Green zone
    drawZone(0.0f, min(fillDeg, greenLimit), colLow);
    // Yellow zone (only if beyond green)
    if (fillDeg > greenLimit) drawZone(greenLimit, min(fillDeg, yellowLimit), colMid);
    // Red zone
    if (fillDeg > yellowLimit) drawZone(yellowLimit, fillDeg, colHigh);

    // Needle angle = startDeg + fillDeg (wrapped)
    float needleAngle = norm360(startDeg + fillDeg);
    return needleAngle;
}

// Draw simple battery fill arc (no color zones). Percentage in [0,100]
static inline void drawBatteryArc(LGFX_Sprite &spr, int cx, int cy,
                                  float rInner, float rOuter,
                                  float startDeg, float spanDeg,
                                  int percent, uint16_t colBg, uint16_t colFill) {
    float endDeg = norm360(startDeg + spanDeg);
    fillArc(spr, cx, cy, rInner, rOuter, startDeg, endDeg, colBg);
    if (percent <= 0) return;
    float fillDeg = (constrain(percent,0,100)/100.0f) * spanDeg;
    fillArc(spr, cx, cy, rInner, rOuter, startDeg, norm360(startDeg + fillDeg), colFill);
}

// Draw satellite strength arc using satellite count with max scaling
static inline void drawSatelliteArc(LGFX_Sprite &spr, int cx, int cy,
                                    float rInner, float rOuter,
                                    float startDeg, float spanDeg,
                                    int satsUsed, int maxSatsForArc,
                                    uint16_t colBg, uint16_t colLow, uint16_t colMid, uint16_t colHigh) {
    float endDeg = norm360(startDeg - spanDeg + 360.0f); // direction similar to legacy (start decreasing)
    // Background
    fillArc(spr, cx, cy, rInner, rOuter, endDeg, startDeg, colBg);
    if (satsUsed <= 0) return;
    int clamped = (satsUsed > maxSatsForArc) ? maxSatsForArc : satsUsed;
    float fillSpan = (clamped / (float)maxSatsForArc) * spanDeg;
    uint16_t color = (satsUsed <= 2) ? colHigh : (satsUsed == 3 ? colMid : colLow);
    // Fill from (start - fillSpan) .. start
    float fillStart = norm360(startDeg - fillSpan);
    fillArc(spr, cx, cy, rInner, rOuter, fillStart, startDeg, color);
}

// Draw a thin border arc on a single radius using LGFX drawArc.
// Converts UI degrees (0=12 o'clock clockwise) to LGFX drawArc angles (0=3 o'clock, increasing counter-clockwise?).
// Mapping used in legacy code: drawArcAngle = uiAngle - 90.
inline void drawArcBorder(LGFX_Sprite &spr, int cx, int cy, float radius,
                          float uiStartDeg, float uiEndDeg, uint16_t color) {
    // Convert to LGFX angle space
    float a1 = uiStartDeg - 90.0f;
    float a2 = uiEndDeg   - 90.0f;
    // Handle wrap across 360 in UI space
    if (uiStartDeg > uiEndDeg) {
        // e.g., 240..120 -> segments 240..360 and 0..120 -> map to 150..270 and -90..30
        spr.drawArc(cx, cy, (int)radius, (int)radius, (int)a1, 270, color);
        spr.drawArc(cx, cy, (int)radius, (int)radius, -90, (int)a2, color);
    } else {
        spr.drawArc(cx, cy, (int)radius, (int)radius, (int)a1, (int)a2, color);
    }
}

// Draw inner+outer arc borders with end caps at the UI start/end angles
inline void drawArcBordersWithCaps(LGFX_Sprite &spr, int cx, int cy,
                                   float rInner, float rOuter,
                                   float uiStartDeg, float uiEndDeg,
                                   uint16_t color) {
    // Thin borders
    drawArcBorder(spr, cx, cy, rOuter, uiStartDeg, uiEndDeg, color);
    drawArcBorder(spr, cx, cy, rInner, uiStartDeg, uiEndDeg, color);
    // End caps (radial lines)
    int x1, y1, x2, y2;
    polarPoint(cx, cy, rInner, uiStartDeg, x1, y1);
    polarPoint(cx, cy, rOuter, uiStartDeg, x2, y2);
    spr.drawLine(x1, y1, x2, y2, color);
    polarPoint(cx, cy, rInner, uiEndDeg, x1, y1);
    polarPoint(cx, cy, rOuter, uiEndDeg, x2, y2);
    spr.drawLine(x1, y1, x2, y2, color);
}

} // namespace ui_arc
