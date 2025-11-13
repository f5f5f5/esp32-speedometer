// Multi-screen UI with arc gauge + GPS integration
#include <Arduino.h>
#include <Wire.h>
#include "display_config.hpp"
#include "battery.hpp"
#include "gps_module.h"
#include "arc_utils.hpp"
#include "icon_utils.hpp"

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

// ---------- Colour Schemes ----------
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

// ========================================
// GEOMETRY & MATH HELPERS
// ========================================

// Arc rendering resolution: smaller values = smoother arcs, larger = faster rendering
// Geometry helpers now provided by arc_utils.hpp (ui_arc namespace)
using ui_arc::deg2rad;      // re-export for existing code
using ui_arc::polarPoint;   // re-export for existing code

// ========================================
// SCREEN RENDERING
// ========================================

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

  // Speed gauge (background + segmented fill) via utility
  float needleAngle = ui_arc::drawSpeedGauge(sprite, cx, cy,
                                             rInner, rOuter,
                                             speedStart, speedSpan,
                                             ui.speed_kmh, ui.max_kmh,
                                             cs.arcBackground, cs.arcLow, cs.arcMid, cs.arcHigh);

  // Battery arc
  uint16_t batColor = battery.isUSBPowered() ? 0x0318 : (ui.battery_pc < 20 ? cs.arcHigh : cs.arcLow);  // Darker blue for USB
  ui_arc::drawBatteryArc(sprite, cx, cy, rBatInner, rBatOuter, batStart, batSpan, ui.battery_pc, cs.arcBackground, batColor);

  // Satellite arc (used satellites scaled to max)
  ui_arc::drawSatelliteArc(sprite, cx, cy, rSatInner, rSatOuter, satStart, satSpan, ui.satellites, 6, cs.arcBackground, cs.arcLow, cs.arcMid, cs.arcHigh);

  // Draw anti-aliased borders around all arc bars
  // Use opposite mode's background colour for borders (light mode uses dark bg, dark mode uses light bg)
  uint16_t borderColor = ui.isDarkMode ? 0xADB5 : 0x1082;

  // Borders + end caps using helper
  ui_arc::drawArcBordersWithCaps(sprite, cx, cy, rInner, rOuter, 240, 120, borderColor);
  ui_arc::drawArcBordersWithCaps(sprite, cx, cy, rBatInner, rBatOuter, batStart, batEnd, borderColor);
  ui_arc::drawArcBordersWithCaps(sprite, cx, cy, rSatInner, rSatOuter, satEnd, satStart, borderColor);

  // Speed needle
  ui_icon::drawSpeedNeedle(sprite, cx, cy, rInner, needleAngle, ui.isDarkMode);

  // Battery icon / USB indicator
  int batIconX = cx - 40; int batIconY = cy + 55; bool isUSB = battery.isUSBPowered(); bool isLow = battery.isLowBattery();
  if (isUSB) { ui_icon::drawUSBPlugIcon(sprite, batIconX, batIconY, cs.iconNormal, cs.background); }
  else { ui_icon::drawBatteryIcon(sprite, batIconX, batIconY, ui.battery_pc, isLow, isLow ? cs.arcHigh : cs.iconNormal); }
  sprite.setTextDatum(TC_DATUM); sprite.setTextSize(1); sprite.setTextColor(cs.text, cs.background); char batTxt[8]; snprintf(batTxt, sizeof(batTxt), isUSB ? "USB" : "%d%%", ui.battery_pc); sprite.drawString(batTxt, batIconX, batIconY + 10);

  // Low battery flashing label
  if (isLow && !isUSB && ui.lowBatFlashState) { ui_icon::drawLowBatteryLabel(sprite, batIconX, batIconY, cs.arcHigh, cs.background); }

  // Satellite icon & count
  int satIconX = cx + 40; int satIconY = cy + 55; ui_icon::drawSatelliteIcon(sprite, satIconX, satIconY, cs.iconNormal, cs.background); sprite.setTextDatum(TC_DATUM); sprite.setTextSize(1); sprite.setTextColor(cs.text, cs.background); char satTxt[6]; snprintf(satTxt, sizeof(satTxt), "%d", ui.satellites); sprite.drawString(satTxt, satIconX, satIconY + 10);

  // No fix warning label (symmetrical to LOW BAT, above satellite icon)
  bool showNoFixWarning = !ui.fixValid;
  if (showNoFixWarning && ui.lowBatFlashState) { ui_icon::drawNoFixLabel(sprite, satIconX, satIconY, cs.arcHigh, cs.background); }

  // Sun/Moon
  int sunX = cx; int iconY = cy + 24; ui_icon::drawSunMoonIcon(sprite, sunX, iconY, ui.isDarkMode, cs.iconNormal, cs.background);

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
