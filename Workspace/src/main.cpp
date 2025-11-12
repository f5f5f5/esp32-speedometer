// Restored legacy multi-screen UI with arc gauge + GPS integration
#include <Arduino.h>
#include <Wire.h>
#include "display_config.hpp"
#include "battery.hpp"
#include "gps_module.h"

// Create display and battery instances
LGFX display;
Battery battery;

// ---------- UI State ----------
enum class Screen { MAIN, SETTINGS, METRICS };
static Screen currentScreen = Screen::MAIN;

struct UIState {
  float speed_kmh = 0.0f;      // live speed
  float max_kmh   = 220.0f;    // gauge scale limit
  const char* units = "km/h";  // unit label
  int satellites = 0;          // sats used in solution
  int satsInView = 0;          // sats visible
  int battery_pc = 0;          // battery percent
  double lat = 0.0;            // latitude
  double lon = 0.0;            // longitude
  float altitude_m = 0.0f;     // altitude meters
  bool fixValid = false;       // GPS fix validity
  bool isDarkMode = false;     // light by default
  bool lowBatFlashState = false;

  // Previous values for selective redraw
  float prev_speed = -1.0f;
  int prev_battery = -1;
  int prev_satellites = -1;
  BatteryState prev_battery_state = BatteryState::UNKNOWN;
  bool needsFullRedraw = true;
} ui;

// Global sprite buffer for double-buffered rendering
static LGFX_Sprite sprite(&display);
static bool spriteInit = false;

// ---------- Gesture State ----------
struct SwipeState {
  bool touching = false;
  int startX = 0;
  int startY = 0;
  int lastX = 0;
  int lastY = 0;
  uint32_t startMs = 0;
};
static SwipeState swipe;

#ifndef SWIPE_THRESHOLD_PX
#define SWIPE_THRESHOLD_PX 50
#endif
#ifndef TAP_THRESHOLD_PX
#define TAP_THRESHOLD_PX 10
#endif
#ifndef TAP_TIME_MS
#define TAP_TIME_MS 300
#endif

// ---------- Color Schemes (same as legacy) ----------
struct ColorScheme {
  uint16_t background;
  uint16_t text;
  uint16_t speedText;
  uint16_t unitsText;
  uint16_t arcBackground;
  uint16_t arcLow;
  uint16_t arcMid;
  uint16_t arcHigh;
  uint16_t iconNormal;
  uint16_t iconDim;
  uint16_t settingSelected;
};

static ColorScheme lightMode = {
  .background    = 0xADB5,
  .text          = TFT_BLACK,
  .speedText     = TFT_BLACK,
  .unitsText     = TFT_BLACK,
  .arcBackground = 0x1082,
  .arcLow        = 0x2F43,
  .arcMid        = 0xFD20,
  .arcHigh       = 0xF800,
  .iconNormal    = TFT_BLACK,
  .iconDim       = 0x8410,
  .settingSelected = 0x1082
};

static ColorScheme darkMode = {
  .background    = 0x1082,
  .text          = TFT_WHITE,
  .speedText     = TFT_WHITE,
  .unitsText     = 0xCE79,
  .arcBackground = 0xADB5,
  .arcLow        = 0x2F43,  // Same green as light mode
  .arcMid        = 0x8420,
  .arcHigh       = 0x9000,
  .iconNormal    = TFT_WHITE,
  .iconDim       = 0x8410,
  .settingSelected = 0x8420
};

static ColorScheme& getColors() { return ui.isDarkMode ? darkMode : lightMode; }

// ---------- Geometry Helpers ----------
static inline float deg2rad(float deg) { return (deg - 90.0f) * (PI / 180.0f); }
static inline void polarPoint(int cx, int cy, float r, float deg, int &x, int &y) {
  float th = deg2rad(deg);
  x = cx + (int)roundf(cosf(th) * r);
  y = cy + (int)roundf(sinf(th) * r);
}

static constexpr float ARC_STEP_DEGREES = 3.0f;
static void fillArcToSprite(LGFX_Sprite* spr, int cx, int cy, float rInner, float rOuter, float startDeg, float endDeg, uint16_t color) {
  if (startDeg > endDeg) return;
  int px_i, py_i, px_o, py_o, qx_i, qy_i, qx_o, qy_o; float prev = startDeg;
  polarPoint(cx, cy, rInner, prev, px_i, py_i); polarPoint(cx, cy, rOuter, prev, px_o, py_o);
  for (float a = startDeg + ARC_STEP_DEGREES; a <= endDeg + 0.001f; a += ARC_STEP_DEGREES) {
    float cur = a > endDeg ? endDeg : a;
    polarPoint(cx, cy, rInner, cur, qx_i, qy_i); polarPoint(cx, cy, rOuter, cur, qx_o, qy_o);
    spr->fillTriangle(px_i, py_i, px_o, py_o, qx_o, qy_o, color);
    spr->fillTriangle(px_i, py_i, qx_i, qy_i, qx_o, qy_o, color);
    px_i = qx_i; py_i = qy_i; px_o = qx_o; py_o = qy_o; prev = cur;
  }
}

// ---------- Rendering: Main Gauge ----------
static void renderMain() {
  const int W = display.width(); const int H = display.height(); const int cx = W/2, cy = H/2;
  ColorScheme& cs = getColors();
  if (!spriteInit) { sprite.createSprite(W, H); spriteInit = true; }
  sprite.fillSprite(cs.background);

  // Arc radii
  const float rOuter = 119.0f; const float rInner = 108.0f;
  const float rBatOuter = 100.0f, rBatInner = 92.0f;
  const float rSatOuter = 100.0f, rSatInner = 92.0f;

  // Angle definitions
  const float speedStart = 240.0f, speedEnd = 120.0f, speedSpan = 240.0f;
  const float batGap = 5.0f; const float batStart = 180.0f + batGap; const float batEnd = 240.0f; const float batSpan = batEnd - batStart;
  const float satGap = 5.0f; const float satStart = 180.0f - satGap; const float satEnd = 120.0f; const float satSpan = satStart - satEnd;

  // Background for speed arc (split wrap)
  fillArcToSprite(&sprite, cx, cy, rInner, rOuter, speedStart, 360, cs.arcBackground);
  fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, speedEnd, cs.arcBackground);

  float fillFraction = constrain(ui.speed_kmh / ui.max_kmh, 0.0f, 1.0f);
  float fillDeg = fillFraction * speedSpan; // degrees of fill
  if (fillDeg > 0.0f) {
    float greenEnd = min(fillDeg, speedSpan * 0.6f);
    float yellowEnd = min(fillDeg, speedSpan * 0.85f);
    // Green
    if (greenEnd > 0.0f) {
      float ge = speedStart + greenEnd; if (ge > 360) { fillArcToSprite(&sprite, cx, cy, rInner, rOuter, speedStart, 360, cs.arcLow); fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, ge - 360, cs.arcLow); }
      else fillArcToSprite(&sprite, cx, cy, rInner, rOuter, speedStart, ge, cs.arcLow);
    }
    // Yellow
    if (fillDeg > speedSpan * 0.6f) {
      float ys = speedStart + speedSpan * 0.6f; float ye = speedStart + yellowEnd;
      if (ys >= 360) { ys -= 360; ye -= 360; fillArcToSprite(&sprite, cx, cy, rInner, rOuter, ys, ye, cs.arcMid); }
      else if (ye > 360) { fillArcToSprite(&sprite, cx, cy, rInner, rOuter, ys, 360, cs.arcMid); fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, ye - 360, cs.arcMid); }
      else fillArcToSprite(&sprite, cx, cy, rInner, rOuter, ys, ye, cs.arcMid);
    }
    // Red
    if (fillDeg > speedSpan * 0.85f) {
      float rs = speedStart + speedSpan * 0.85f; float re = speedStart + fillDeg;
      if (rs >= 360) { rs -= 360; re -= 360; fillArcToSprite(&sprite, cx, cy, rInner, rOuter, rs, re, cs.arcHigh); }
      else if (re > 360) { fillArcToSprite(&sprite, cx, cy, rInner, rOuter, rs, 360, cs.arcHigh); fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, re - 360, cs.arcHigh); }
      else fillArcToSprite(&sprite, cx, cy, rInner, rOuter, rs, re, cs.arcHigh);
    }
  }

  // Battery arc background + fill
  fillArcToSprite(&sprite, cx, cy, rBatInner, rBatOuter, batStart, batEnd, cs.arcBackground);
  float batFillDeg = (ui.battery_pc / 100.0f) * batSpan;
  uint16_t batColor = battery.isUSBPowered() ? 0x0318 : (ui.battery_pc < 20 ? cs.arcHigh : cs.arcLow);  // Darker blue for USB
  if (batFillDeg > 0) fillArcToSprite(&sprite, cx, cy, rBatInner, rBatOuter, batStart, batStart + batFillDeg, batColor);

  // Satellite arc (using satellites used, fallback to in-view count if zero)
  fillArcToSprite(&sprite, cx, cy, rSatInner, rSatOuter, satEnd, satStart, cs.arcBackground);
  // Satellite arc: max at 6 sats, red <=2, amber =3, green >3
  const int maxSatsForArc = 6;
  float satCountNorm = (min(ui.satellites, maxSatsForArc) / (float)maxSatsForArc) * satSpan;
  if (satCountNorm > 0) {
    // Determine color: red if <=2, amber if 3, green if >3
    uint16_t satColor;
    if (ui.satellites <= 2) {
      satColor = cs.arcHigh;  // red
    } else if (ui.satellites == 3) {
      satColor = cs.arcMid;   // amber
    } else {
      satColor = cs.arcLow;   // green
    }
    fillArcToSprite(&sprite, cx, cy, rSatInner, rSatOuter, satStart - satCountNorm, satStart, satColor);
  }

  // Draw anti-aliased borders around all arc bars (legacy approach)
  // Use opposite mode's background color for borders (light mode uses dark bg, dark mode uses light bg)
  uint16_t borderColor = ui.isDarkMode ? 0xADB5 : 0x1082;

  // Speed arc outer and inner borders (240° to 360°, then 0° to 120°)
  // UI angles: 240° to 360° = drawArc 150° to 270°
  sprite.drawArc(cx, cy, rOuter, rOuter, 150, 270, borderColor);
  // UI angles: 0° to 120° = drawArc 270° to 30° (wraps, so draw as -90° to 30°)
  sprite.drawArc(cx, cy, rOuter, rOuter, -90, 30, borderColor);
  sprite.drawArc(cx, cy, rInner, rInner, 150, 270, borderColor);
  sprite.drawArc(cx, cy, rInner, rInner, -90, 30, borderColor);

  // Speed arc end caps (radial lines at 240° and 120°)
  int x1, y1, x2, y2;
  // Start cap at 240° (8 o'clock)
  polarPoint(cx, cy, rInner, 240, x1, y1);
  polarPoint(cx, cy, rOuter, 240, x2, y2);
  sprite.drawLine(x1, y1, x2, y2, borderColor);
  // End cap at 120° (4 o'clock)
  polarPoint(cx, cy, rInner, 120, x1, y1);
  polarPoint(cx, cy, rOuter, 120, x2, y2);
  sprite.drawLine(x1, y1, x2, y2, borderColor);

  // Battery arc outer and inner borders
  // UI angles: 185° to 240° = drawArc 95° to 150°
  sprite.drawArc(cx, cy, rBatOuter, rBatOuter, 95, 150, borderColor);
  sprite.drawArc(cx, cy, rBatInner, rBatInner, 95, 150, borderColor);

  // Battery arc end caps
  polarPoint(cx, cy, rBatInner, batStart, x1, y1);
  polarPoint(cx, cy, rBatOuter, batStart, x2, y2);
  sprite.drawLine(x1, y1, x2, y2, borderColor);
  polarPoint(cx, cy, rBatInner, batEnd, x1, y1);
  polarPoint(cx, cy, rBatOuter, batEnd, x2, y2);
  sprite.drawLine(x1, y1, x2, y2, borderColor);

  // Satellite arc outer and inner borders
  // UI angles: 120° to 175° = drawArc 30° to 85°
  sprite.drawArc(cx, cy, rSatOuter, rSatOuter, 30, 85, borderColor);
  sprite.drawArc(cx, cy, rSatInner, rSatInner, 30, 85, borderColor);

  // Satellite arc end caps
  polarPoint(cx, cy, rSatInner, satEnd, x1, y1);
  polarPoint(cx, cy, rSatOuter, satEnd, x2, y2);
  sprite.drawLine(x1, y1, x2, y2, borderColor);
  polarPoint(cx, cy, rSatInner, satStart, x1, y1);
  polarPoint(cx, cy, rSatOuter, satStart, x2, y2);
  sprite.drawLine(x1, y1, x2, y2, borderColor);

  // Speed needle
  float currentSpeedAngle = speedStart + fillDeg; if (currentSpeedAngle >= 360) currentSpeedAngle -= 360;
  float needleRad = deg2rad(currentSpeedAngle); const float gapFromArc = 2.0f; const float visibleLength = 30.0f; float needleTip = rInner - gapFromArc; float needleStart = needleTip - visibleLength; float perpRad = needleRad + PI/2.0f; float baseWidth=3.0f; int bx1 = cx + (int)(cosf(needleRad)*needleStart + cosf(perpRad)*baseWidth); int by1 = cy + (int)(sinf(needleRad)*needleStart + sinf(perpRad)*baseWidth); int bx2 = cx + (int)(cosf(needleRad)*needleStart - cosf(perpRad)*baseWidth); int by2 = cy + (int)(sinf(needleRad)*needleStart - sinf(perpRad)*baseWidth); float tipWidth=1.5f; int tx1 = cx + (int)(cosf(needleRad)*needleTip + cosf(perpRad)*tipWidth); int ty1 = cy + (int)(sinf(needleRad)*needleTip + sinf(perpRad)*tipWidth); int tx2 = cx + (int)(cosf(needleRad)*needleTip - cosf(perpRad)*tipWidth); int ty2 = cy + (int)(sinf(needleRad)*needleTip - sinf(perpRad)*tipWidth); uint16_t shadowColor = ui.isDarkMode ? 0x0841 : 0x8C92; sprite.fillTriangle(bx1+2,by1+2,bx2+2,by2+2,tx1+2,ty1+2,shadowColor); sprite.fillTriangle(bx2+2,by2+2,tx1+2,ty1+2,tx2+2,ty2+2,shadowColor); sprite.fillTriangle(bx1,by1,bx2,by2,tx1,ty1,TFT_RED); sprite.fillTriangle(bx2,by2,tx1,ty1,tx2,ty2,TFT_RED);

  // Battery icon / USB indicator
  int batIconX = cx - 40; int batIconY = cy + 55; bool isUSB = battery.isUSBPowered(); bool isLow = battery.isLowBattery();
  if (isUSB) {
    uint16_t c = getColors().iconNormal; int o = -9; sprite.fillRect(batIconX + o - 8, batIconY - 1, 8, 2, c); sprite.fillRoundRect(batIconX + o, batIconY - 5, 12, 10, 2, c); sprite.fillRoundRect(batIconX + o + 12, batIconY - 3, 6, 6, 1, c); sprite.drawLine(batIconX + o + 14, batIconY - 1, batIconX + o + 14, batIconY + 1, getColors().background); sprite.drawLine(batIconX + o + 16, batIconY - 1, batIconX + o + 16, batIconY + 1, getColors().background);
  } else {
    uint16_t c = isLow ? cs.arcHigh : cs.iconNormal; sprite.drawRect(batIconX - 12, batIconY - 8, 24, 14, c); sprite.fillRect(batIconX + 12, batIconY - 4, 2, 6, c); int fillPix = (ui.battery_pc * 20) / 100; if (fillPix > 0) sprite.fillRect(batIconX -10, batIconY -6, fillPix, 10, c);
  }
  sprite.setTextDatum(TC_DATUM); sprite.setTextSize(1); sprite.setTextColor(cs.text, cs.background); char batTxt[8]; snprintf(batTxt, sizeof(batTxt), isUSB ? "USB" : "%d%%", ui.battery_pc); sprite.drawString(batTxt, batIconX, batIconY + 10);

  // Low battery flashing label
  if (isLow && !isUSB && ui.lowBatFlashState) {
    sprite.setTextDatum(TL_DATUM); sprite.setFont(&fonts::FreeSansBold12pt7b); sprite.setTextColor(cs.arcHigh, cs.background); sprite.drawString("LOW", batIconX + 18 - 74 + 17, batIconY - 16 - 66 + 37 - 5); sprite.drawString("BAT", batIconX + 18 - 52, batIconY - 16 - 66 + 37 - 5 + 19); sprite.setFont(nullptr);
  }

  // Satellite icon & count
  int satIconX = cx + 40; int satIconY = cy + 55; sprite.fillRect(satIconX - 2, satIconY - 5, 8, 10, cs.iconNormal); sprite.drawRect(satIconX -2, satIconY -5, 8, 10, cs.iconNormal); sprite.fillRect(satIconX -10, satIconY -3, 7, 6, cs.iconNormal); sprite.drawLine(satIconX -10, satIconY -1, satIconX -3, satIconY -1, cs.background); sprite.drawLine(satIconX -10, satIconY +1, satIconX -3, satIconY +1, cs.background); sprite.drawLine(satIconX +2, satIconY -5, satIconX +2, satIconY -9, cs.iconNormal); sprite.fillCircle(satIconX +2, satIconY -10, 2, cs.iconNormal); for (int i=1;i<=3;i++) sprite.drawArc(satIconX +6, satIconY, 4 + i*3, 4 + i*3, 315, 45, cs.iconNormal); sprite.setTextDatum(TC_DATUM); sprite.setTextSize(1); sprite.setTextColor(cs.text, cs.background); char satTxt[6]; snprintf(satTxt, sizeof(satTxt), "%d", ui.satellites); sprite.drawString(satTxt, satIconX, satIconY + 10);

  // No fix warning label (symmetrical to LOW BAT, above satellite icon)
  bool showNoFixWarning = !ui.fixValid;
  if (showNoFixWarning && ui.lowBatFlashState) {  // Use same flash state for consistency
    sprite.setTextDatum(TL_DATUM); sprite.setFont(&fonts::FreeSansBold12pt7b); sprite.setTextColor(cs.arcHigh, cs.background);
    // Mirror the LOW BAT positioning: satIconX is +40 from center, batIconX is -40
    // LOW BAT offsets: batIconX + 18 - 74 + 17, batIconY - 16 - 66 + 37 - 5
    // For symmetry, flip horizontal offset around satIconX
    int noX = satIconX - 18 + 74 - 17 - 15 - 5;  // Adjusted for "NO" width, moved left 5px
    int noY = satIconY - 16 - 66 + 37 - 5 - 2;   // Moved up 2px
    int fixX = satIconX - 18 + 74 - 17 - 22; // Adjusted for "FIX" width
    int fixY = noY + 19;  // Same vertical spacing as LOW/BAT
    sprite.drawString("NO", noX, noY);
    sprite.drawString("FIX", fixX, fixY);
    sprite.setFont(nullptr);
  }

  // Sun/Moon
  int sunX = cx; int iconY = cy + 24; if (ui.isDarkMode) { sprite.fillCircle(sunX, iconY, 11, cs.iconNormal); sprite.fillCircle(sunX + 6, iconY - 3, 10, cs.background); } else { sprite.fillCircle(sunX, iconY, 10, cs.iconNormal); for (int i=0;i<8;i++){ float ang = i*45.0f; int rx1,ry1,rx2,ry2; polarPoint(sunX, iconY, 12, ang, rx1, ry1); polarPoint(sunX, iconY, 18, ang, rx2, ry2); sprite.drawLine(rx1, ry1, rx2, ry2, cs.iconNormal);} }

  // Speed value
  sprite.setTextDatum(MC_DATUM); sprite.setFont(&fonts::FreeSansBold24pt7b); sprite.setTextColor(cs.speedText, cs.background); char spBuf[12]; if (ui.speed_kmh < 10.0f) snprintf(spBuf, sizeof(spBuf), "%.1f", ui.speed_kmh); else snprintf(spBuf, sizeof(spBuf), "%d", (int)roundf(ui.speed_kmh)); sprite.drawString(spBuf, cx, cy - 18); sprite.setFont(nullptr); sprite.setTextSize(2); sprite.setTextColor(cs.unitsText, cs.background); sprite.drawString(ui.units, cx, cy - 52); sprite.setTextSize(1);

  sprite.pushSprite(0,0);
  ui.prev_speed = ui.speed_kmh; ui.prev_battery = ui.battery_pc; ui.prev_satellites = ui.satellites; ui.needsFullRedraw = false;
}

static void renderSettings() {
  const int W = display.width(); const int H = display.height(); const int cx = W/2; ColorScheme& cs = getColors(); if (!spriteInit){ sprite.createSprite(W,H); spriteInit = true; }
  sprite.fillSprite(cs.background); sprite.setTextDatum(MC_DATUM); sprite.setFont(&fonts::FreeSansBold12pt7b); sprite.setTextColor(cs.text, cs.background); sprite.drawString("Settings", cx, 35); sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.setTextColor(cs.text, cs.background); sprite.drawString("Display Mode", cx, 65); sprite.setFont(nullptr); sprite.setTextSize(1); sprite.setTextColor(ui.isDarkMode ? cs.text : cs.settingSelected, cs.background); sprite.drawString(ui.isDarkMode ? "> Dark" : "  Light", cx - 35, 82); sprite.setTextColor(ui.isDarkMode ? cs.settingSelected : cs.text, cs.background); sprite.drawString(ui.isDarkMode ? "  Light" : "> Dark", cx + 35, 82);
  sprite.setFont(&fonts::FreeSans9pt7b); sprite.setTextColor(cs.text, cs.background); sprite.drawString("Units", cx, 110); sprite.setFont(nullptr); sprite.setTextColor(cs.settingSelected, cs.background); sprite.drawString("> km/h", cx, 127); sprite.setTextColor(cs.iconDim, cs.background); sprite.drawString("mph / m/s", cx, 142);
  sprite.setFont(&fonts::FreeSans9pt7b); sprite.setTextColor(cs.text, cs.background); sprite.drawString("Speed Scale", cx, 168); sprite.setFont(nullptr); sprite.setTextColor(cs.settingSelected, cs.background); sprite.drawString("> Driving (220)", cx, 185); sprite.setTextColor(cs.iconDim, cs.background); sprite.drawString("Walking / Cycling", cx, 200);
  sprite.setTextColor(cs.iconDim, cs.background); sprite.drawString("Swipe to navigate", cx, 220); sprite.pushSprite(0,0);
}

static void renderMetrics() {
  const int W = display.width(); const int H = display.height(); const int cx = W/2; ColorScheme& cs = getColors(); if (!spriteInit){ sprite.createSprite(W,H); spriteInit = true; }
  sprite.fillSprite(cs.background);
  sprite.setTextDatum(MC_DATUM);
  sprite.setFont(&fonts::FreeSansBold12pt7b);
  sprite.setTextColor(cs.text, cs.background);
  sprite.drawString("Metrics", cx, 35);
  sprite.setFont(&fonts::FreeSans9pt7b);

  char line[64];
  // Satellites (used / in view) with fix status
  sprite.setTextColor(cs.text, cs.background);
  if (ui.fixValid) {
    snprintf(line, sizeof(line), "Satellites: %d / %d", ui.satellites, ui.satsInView);
  } else {
    snprintf(line, sizeof(line), "Satellites: %d / %d (NO FIX)", ui.satellites, ui.satsInView);
  }
  sprite.drawString(line, cx, 70);

  // Coordinates
  sprite.setFont(nullptr);
  sprite.setTextColor(cs.iconDim, cs.background);
  snprintf(line, sizeof(line), "Lat: %.5f", ui.lat);
  sprite.drawString(line, cx, 95);
  snprintf(line, sizeof(line), "Lon: %.5f", ui.lon);
  sprite.drawString(line, cx, 110);

  // Altitude
  if (ui.fixValid) {
    char altVal[16]; dtostrf(ui.altitude_m, 0, 1, altVal);
    snprintf(line, sizeof(line), "Alt: %sm", altVal);
  } else {
    snprintf(line, sizeof(line), "Alt: ---");
  }
  sprite.drawString(line, cx, 125);

  // Speed line (shows ~ prefix if no fix yet)
  sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.setTextColor(cs.text, cs.background);
  snprintf(line, sizeof(line), "Speed: %s%.1f %s", ui.fixValid ? "" : "~", ui.speed_kmh, ui.units);
  sprite.drawString(line, cx, 140);

  // Power / Battery
  if (battery.isUSBPowered()) {
    snprintf(line, sizeof(line), "Power: USB (%.2fV)", battery.getVoltage());
  } else {
    snprintf(line, sizeof(line), "Battery: %d%% (%.2fV)", ui.battery_pc, battery.getVoltage());
  }
  sprite.drawString(line, cx, 170);

  // Footer hint
  sprite.setFont(nullptr);
  sprite.setTextColor(cs.iconDim, cs.background);
  sprite.drawString("Swipe to navigate", cx, 205);

  sprite.pushSprite(0,0);
}

static void renderActive() { switch (currentScreen) { case Screen::MAIN: renderMain(); break; case Screen::SETTINGS: renderSettings(); break; case Screen::METRICS: renderMetrics(); break; } }

// ---------- Splash Screen ----------
static void renderSplash() {
  const int W = display.width(); const int H = display.height(); const int cx = W/2; const int cy = H/2;
  display.fillScreen(TFT_BLACK);
  const float r1 = 70.0f, r2 = 85.0f; uint16_t arcColor = 0x2F43;
  for (float angle = 200; angle <= 340; angle += 5) { float rad = (angle - 90) * PI / 180.0f; int x1 = cx + (int)(cos(rad) * r1); int y1 = cy + (int)(sin(rad) * r1); int x2 = cx + (int)(cos(rad) * r2); int y2 = cy + (int)(sin(rad) * r2); display.drawLine(x1,y1,x2,y2,arcColor); }
  float needleAngle = 250; float rad = (needleAngle - 90) * PI / 180.0f; const float gap = 2.0f; const float vis = 15.0f; float needleTip = r1 - gap; float needleStart = needleTip - vis; int nx1 = cx + (int)(cos(rad) * needleStart); int ny1 = cy + (int)(sin(rad) * needleStart); int nx2 = cx + (int)(cos(rad) * needleTip); int ny2 = cy + (int)(sin(rad) * needleTip); display.drawLine(nx1,ny1,nx2,ny2,TFT_RED); display.drawLine(nx1-1,ny1,nx2-1,ny2,TFT_RED); display.drawLine(nx1+1,ny1,nx2+1,ny2,TFT_RED); display.fillCircle(cx, cy, 6, TFT_WHITE); display.setTextDatum(MC_DATUM); display.setFont(&fonts::FreeSansBold12pt7b); display.setTextColor(TFT_WHITE, TFT_BLACK); display.drawString("SPEEDOMETER", cx, cy + 50); display.setFont(nullptr); display.setTextSize(1); display.setTextColor(0x8410, TFT_BLACK); display.drawString("Initializing...", cx, cy + 75); display.setTextColor(0x4208, TFT_BLACK); display.drawString("v1.0", cx, H - 20);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.begin(115200);
  #endif
  delay(100);
  display.init(); display.setRotation(0); display.setBrightness(255); display.invertDisplay(true);
  renderSplash();
  battery.begin(); delay(1500); battery.update(); ui.battery_pc = battery.getPercentage(); delay(500);
  // Initialize GPS (UART1 RX=16 TX=15)
  gps_init(16, 15, 9600);
  Serial.println("[GPS] Init complete. Awaiting fix...");
  renderMain();
}

// ---------- Loop ----------
void loop() {
  static uint32_t lastLowBatFlash = 0; static uint32_t lastMetricsRefresh = 0; static uint32_t lastGPSUpdatePrint = 0; static uint32_t lastBatteryUpdate = 0; static uint32_t lastMainCheck = 0;
  uint32_t now = millis();

  // Battery periodic update (~1Hz)
  if (now - lastBatteryUpdate > 1000) { lastBatteryUpdate = now; battery.update(); ui.battery_pc = battery.getPercentage(); }

  // Low battery flash toggle (also triggers NO FIX warning flash)
  if (now - lastLowBatFlash > 1000) { lastLowBatFlash = now; ui.lowBatFlashState = !ui.lowBatFlashState; if ((battery.isLowBattery() && !battery.isUSBPowered()) || !ui.fixValid) { if (currentScreen == Screen::MAIN) renderMain(); } }

  // GPS polling (fast) + data snapshot (every 250ms)
  gps_poll();
  static uint32_t lastGPSData = 0;
  if (now - lastGPSData > 250) {
    lastGPSData = now; GPSData gd; gps_get_data(&gd);
    ui.speed_kmh = gd.speedKmh; ui.satellites = gd.satsUsed; ui.satsInView = gd.satsInView; ui.lat = gd.lat; ui.lon = gd.lon; ui.altitude_m = gd.altitude; ui.fixValid = gd.validFix; 
    if (now - lastGPSUpdatePrint > 2000) { lastGPSUpdatePrint = now; Serial.printf("[GPS] fix=%d satsUsed=%d inView=%d speed=%.1fkm/h alt=%.1fm lat=%.5f lon=%.5f\n", gd.validFix, gd.satsUsed, gd.satsInView, gd.speedKmh, gd.altitude, gd.lat, gd.lon); }
  }

  // Redraw metrics/settings every second
  if (now - lastMetricsRefresh > 1000) { lastMetricsRefresh = now; if (currentScreen == Screen::METRICS || currentScreen == Screen::SETTINGS) renderActive(); }

  // Redraw main screen when speed or satellite count changes notably
  if (currentScreen == Screen::MAIN && (now - lastMainCheck > 200)) {
    lastMainCheck = now; if (fabs(ui.speed_kmh - ui.prev_speed) > 0.2f || ui.satellites != ui.prev_satellites || ui.needsFullRedraw) renderMain(); }

  // Battery state change triggers redraw
  BatteryState st = battery.getState(); if (st != ui.prev_battery_state) { ui.prev_battery_state = st; renderActive(); }

  // Touch gesture handling (swipe / tap)
  int tx, ty; bool pressed = display.getTouch(&tx, &ty);
  if (pressed && !swipe.touching) { swipe.touching = true; swipe.startX = swipe.lastX = tx; swipe.startY = swipe.lastY = ty; swipe.startMs = now; }
  else if (pressed && swipe.touching) { swipe.lastX = tx; swipe.lastY = ty; }
  else if (!pressed && swipe.touching) {
    int dx = swipe.lastX - swipe.startX; int dy = swipe.lastY - swipe.startY; uint32_t dt = now - swipe.startMs;
    if (abs(dx) >= SWIPE_THRESHOLD_PX && abs(dy) < SWIPE_THRESHOLD_PX) {
      if (dx < 0) currentScreen = (currentScreen == Screen::MAIN) ? Screen::SETTINGS : (currentScreen == Screen::SETTINGS ? Screen::METRICS : Screen::MAIN);
      else currentScreen = (currentScreen == Screen::MAIN) ? Screen::METRICS : (currentScreen == Screen::SETTINGS ? Screen::MAIN : Screen::SETTINGS);
      ui.needsFullRedraw = true; renderActive();
    } else if (abs(dx) < TAP_THRESHOLD_PX && abs(dy) < TAP_THRESHOLD_PX && dt <= TAP_TIME_MS) {
      int cx = display.width()/2; int cy = display.height()/2; if (abs(swipe.startX - cx) < 80 && abs(swipe.startY - cy) < 80) { ui.isDarkMode = !ui.isDarkMode; ui.needsFullRedraw = true; renderActive(); }
    }
    swipe.touching = false;
  }

  // Serial key input (a/d/m)
  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'a' || c == 'A') { currentScreen = (currentScreen == Screen::MAIN) ? Screen::METRICS : (currentScreen == Screen::SETTINGS ? Screen::MAIN : Screen::SETTINGS); ui.needsFullRedraw = true; renderActive(); }
    else if (c == 'd' || c == 'D') { currentScreen = (currentScreen == Screen::MAIN) ? Screen::SETTINGS : (currentScreen == Screen::SETTINGS ? Screen::METRICS : Screen::MAIN); ui.needsFullRedraw = true; renderActive(); }
    else if (c == 'm' || c == 'M') { ui.isDarkMode = !ui.isDarkMode; ui.needsFullRedraw = true; Serial.printf("[MODE] %s\n", ui.isDarkMode ? "dark" : "light"); renderActive(); }
  }
}
