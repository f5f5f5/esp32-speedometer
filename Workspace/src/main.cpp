// UI mock for Waveshare ESP32-S3 Round LCD (GC9A01)
#include <Arduino.h>
#include <Wire.h>
#include "display_config.hpp"

// Create display instance
LGFX display;

// ---------- UI State ----------
enum class Screen { MAIN, SETTINGS, METRICS };
Screen currentScreen = Screen::MAIN;

// ---------- Touch / Gesture State ----------
struct SwipeState {
  bool touching = false;
  int startX = 0;
  int startY = 0;
  int lastX = 0;
  int lastY = 0;
  uint32_t startMs = 0;
};
static SwipeState swipe;
static const int SWIPE_THRESHOLD_PX = 50;     // horizontal distance to trigger swipe
static const int TAP_THRESHOLD_PX   = 10;     // max movement considered a tap
static const uint32_t TAP_TIME_MS   = 300;    // tap max duration

// Optional: I2C scanner to locate CST816S touch on unknown pins.
#if defined(TOUCH_I2C_SCANNER)
static void scanI2CForTouch() {
  struct Pair { int sda; int scl; } candidates[] = {
    // Try a handful of common ESP32-S3 pairs. Avoid pins used by SPI: 2,8,9,10,11,14
    {4,5}, {5,6}, {6,7}, {7,6}, {12,13}, {13,12}, {15,16}, {16,15}, {17,18}, {18,17},
    {19,20}, {20,19}, {21,20}, {38,39}, {39,38}, {40,39}, {41,40}, {42,41}
  };

  Serial.println("[I2C-Scan] Starting scan for CST816S (addr 0x15)...");
  for (auto p : candidates) {
    if (p.sda < 0 || p.scl < 0) continue;
    Wire.end();
    if (!Wire.begin(p.sda, p.scl)) {
      Serial.printf("[I2C-Scan] Wire.begin failed on SDA=%d SCL=%d\n", p.sda, p.scl);
      continue;
    }
    for (int freq : {100000, 400000}) {
      #if ARDUINO >= 10610
      Wire.setClock(freq);
      #endif
      bool foundAny = false;
      for (uint8_t addr = 0x08; addr <= 0x7E; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
          foundAny = true;
          Serial.printf("[I2C-Scan] Found device 0x%02X at SDA=%d SCL=%d freq=%d\n", addr, p.sda, p.scl, freq);
          if (addr == 0x15) {
            Serial.println("[I2C-Scan] Likely CST816S detected! Use these build_flags:");
            Serial.printf("  -DTOUCH_CST816S -DTOUCH_I2C_PORT=0 -DTOUCH_PIN_SDA=%d -DTOUCH_PIN_SCL=%d -DTOUCH_I2C_ADDR=0x15 -DTOUCH_I2C_FREQ=%d\n", p.sda, p.scl, freq);
            Serial.println("[I2C-Scan] Optionally add -DTOUCH_PIN_INT=GPIO -DTOUCH_PIN_RST=GPIO if available.");
          }
        }
      }
      if (!foundAny) {
        Serial.printf("[I2C-Scan] No devices at SDA=%d SCL=%d freq=%d\n", p.sda, p.scl, freq);
      }
    }
  }
  Serial.println("[I2C-Scan] Scan complete.");
}
#endif

struct UIState {
  float speed_kmh = 88.0f;       // demo speed
  float max_kmh   = 220.0f;      // gauge max
  const char* units = "km/h";    // units label
  int satellites = 9;            // demo satellite count
  int battery_pc = 73;           // battery percent
  double lat = 51.5074;          // demo location
  double lon = -0.1278;
  bool isDarkMode = false;       // light mode by default
  
  // Track previous values to avoid unnecessary redraws
  float prev_speed = -1.0f;
  int prev_battery = -1;
  int prev_satellites = -1;
  bool needsFullRedraw = true;
} ui;

// ---------- Color Schemes ----------
struct ColorScheme {
  uint16_t background;
  uint16_t text;
  uint16_t speedText;
  uint16_t unitsText;
  uint16_t arcBackground;
  uint16_t arcLow;      // green segment
  uint16_t arcMid;      // yellow/orange segment
  uint16_t arcHigh;     // red segment
  uint16_t iconNormal;
  uint16_t iconDim;
};

ColorScheme lightMode = {
  .background    = TFT_WHITE,
  .text          = TFT_BLACK,
  .speedText     = TFT_BLACK,
  .unitsText     = TFT_BLACK,
  .arcBackground = TFT_BLACK,
  .arcLow        = 0x2F43,  // #2f7043 green (RGB565: 5-6-5 bits)
  .arcMid        = 0xFD20,  // orange/yellow
  .arcHigh       = 0xF800,  // red
  .iconNormal    = TFT_BLACK,
  .iconDim       = 0x8410   // gray
};

ColorScheme darkMode = {
  .background    = TFT_BLACK,
  .text          = TFT_WHITE,
  .speedText     = TFT_WHITE,
  .unitsText     = 0xCE79,  // light gray
  .arcBackground = 0x2104,  // dark gray
  .arcLow        = 0x2F43,  // #2f7043 green (RGB565: 5-6-5 bits)
  .arcMid        = 0xFD20,  // orange
  .arcHigh       = 0xF800,  // red
  .iconNormal    = TFT_WHITE,
  .iconDim       = 0x8410   // gray
};

ColorScheme& getColors() {
  return ui.isDarkMode ? darkMode : lightMode;
}

// ---------- Math helpers ----------
static inline float deg2rad(float deg) { return (deg - 90.0f) * (PI / 180.0f); }

// Compute point on circle with UI degrees (0 at 12 o'clock, clockwise)
static inline void polarPoint(int cx, int cy, float r, float deg, int &x, int &y) {
  float th = deg2rad(deg);
  x = cx + (int)roundf(cosf(th) * r);
  y = cy + (int)roundf(sinf(th) * r);
}

// Fill an arc sector between startDeg..endDeg with thickness rOuter-rInner
// Handles wrap when endDeg < startDeg by adding 360 to end.
void fillArc(int cx, int cy, float rInner, float rOuter, float startDeg, float endDeg, uint16_t color, float stepDeg = 3.0f) {
  if (endDeg < startDeg) endDeg += 360.0f;
  int px_i, py_i, px_o, py_o, qx_i, qy_i, qx_o, qy_o;
  float prev = startDeg;
  polarPoint(cx, cy, rInner, prev, px_i, py_i);
  polarPoint(cx, cy, rOuter, prev, px_o, py_o);
  for (float a = startDeg + stepDeg; a <= endDeg + 0.001f; a += stepDeg) {
    float cur = a > endDeg ? endDeg : a;
    polarPoint(cx, cy, rInner, cur, qx_i, qy_i);
    polarPoint(cx, cy, rOuter, cur, qx_o, qy_o);
    // Two triangles to make a quad strip
    display.fillTriangle(px_i, py_i, px_o, py_o, qx_o, qy_o, color);
    display.fillTriangle(px_i, py_i, qx_i, qy_i, qx_o, qy_o, color);
    px_i = qx_i; py_i = qy_i; px_o = qx_o; py_o = qy_o; prev = cur;
  }
}

// Draw background and filled portion of an arc gauge.
void drawArcGauge(int cx, int cy, float rInner, float rOuter, float startDeg, float endDeg,
                  float value01, uint16_t backColor, uint16_t fillColor) {
  // background
  fillArc(cx, cy, rInner, rOuter, startDeg, endDeg, backColor);
  // filled portion
  float span = endDeg - startDeg; if (span < 0) span += 360.0f;
  float upto = startDeg + span * constrain(value01, 0.0f, 1.0f);
  fillArc(cx, cy, rInner, rOuter, startDeg, upto, fillColor);
}

// Fill arc segment to sprite for double buffering
void fillArcToSprite(LGFX_Sprite* spr, int cx, int cy, float rInner, float rOuter, float startDeg, float endDeg, uint16_t color) {
  if (startDeg > endDeg) return;
  const float stepDeg = 3.0f;
  int px_i, py_i, px_o, py_o;
  int qx_i, qy_i, qx_o, qy_o;
  float prev = startDeg;
  polarPoint(cx, cy, rInner, prev, px_i, py_i);
  polarPoint(cx, cy, rOuter, prev, px_o, py_o);
  for (float a = startDeg + stepDeg; a <= endDeg + 0.001f; a += stepDeg) {
    float cur = a > endDeg ? endDeg : a;
    polarPoint(cx, cy, rInner, cur, qx_i, qy_i);
    polarPoint(cx, cy, rOuter, cur, qx_o, qy_o);
    // Two triangles to make a quad strip
    spr->fillTriangle(px_i, py_i, px_o, py_o, qx_o, qy_o, color);
    spr->fillTriangle(px_i, py_i, qx_i, qy_i, qx_o, qy_o, color);
    px_i = qx_i; py_i = qy_i; px_o = qx_o; py_o = qy_o; prev = cur;
  }
}

// ---------- Screens ----------
void renderMain() {
  const int W = display.width();
  const int H = display.height();
  const int cx = W/2, cy = H/2;
  ColorScheme& cs = getColors();
  
  // Create sprite buffer for double buffering
  static LGFX_Sprite sprite(&display);
  static bool spriteCreated = false;
  
  if (!spriteCreated) {
    sprite.createSprite(W, H);
    spriteCreated = true;
  }
  
  // Draw everything to the sprite buffer
  sprite.fillSprite(cs.background);

  // Arc dimensions - outer ring for speed, inner rings for battery and satellites
  const float rOuter = 119.0f;
  const float rInner = 108.0f;
  
  const float rBatOuter = 100.0f;
  const float rBatInner = 92.0f;
  
  const float rSatOuter = 100.0f;
  const float rSatInner = 92.0f;
  
  // Speed arc: 8 o'clock (240°) to 4 o'clock (120°) = 240° span
  const float speedStart = 240.0f;  // 8 o'clock
  const float speedEnd = 120.0f;    // 4 o'clock
  const float speedSpan = 240.0f;   // total arc span
  
  // Battery arc: 6 o'clock (180°) to 8 o'clock (240°) with 5° gap = 55° span
  const float batGap = 5.0f;
  const float batStart = 180.0f + batGap;
  const float batEnd = 240.0f;
  const float batSpan = batEnd - batStart;
  
  // Satellite arc: 6 o'clock (180°) to 4 o'clock (120°) with 5° gap = 55° span counter-clockwise
  const float satGap = 5.0f;
  const float satStart = 180.0f - satGap;
  const float satEnd = 120.0f;
  const float satSpan = satStart - satEnd;
  
  // Draw speed arc background (240° to 360°, then 0° to 120°)
  fillArcToSprite(&sprite, cx, cy, rInner, rOuter, speedStart, 360, cs.arcBackground);
  fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, speedEnd, cs.arcBackground);
  
  // Calculate filled portion based on speed
  float fillFraction = constrain(ui.speed_kmh / ui.max_kmh, 0.0f, 1.0f);
  float fillDeg = fillFraction * speedSpan;  // 0 to 240°
  
  // Draw colored segments: green 0-60%, yellow 60-85%, red 85-100%
  if (fillDeg > 0.0f) {
    float greenEnd = min(fillDeg, speedSpan * 0.6f);  // 60% of span = 144°
    float yellowEnd = min(fillDeg, speedSpan * 0.85f); // 85% of span = 204°
    
    // Green segment
    if (greenEnd > 0) {
      float greenEndAngle = speedStart + greenEnd;
      if (greenEndAngle > 360.0f) {
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, speedStart, 360, cs.arcLow);
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, greenEndAngle - 360.0f, cs.arcLow);
      } else {
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, speedStart, greenEndAngle, cs.arcLow);
      }
    }
    
    // Yellow segment
    if (fillDeg > speedSpan * 0.6f) {
      float yellowStartAngle = speedStart + speedSpan * 0.6f;
      float yellowEndAngle = speedStart + yellowEnd;
      
      if (yellowStartAngle >= 360.0f) {
        yellowStartAngle -= 360.0f;
        yellowEndAngle -= 360.0f;
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, yellowStartAngle, yellowEndAngle, cs.arcMid);
      } else if (yellowEndAngle > 360.0f) {
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, yellowStartAngle, 360, cs.arcMid);
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, yellowEndAngle - 360.0f, cs.arcMid);
      } else {
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, yellowStartAngle, yellowEndAngle, cs.arcMid);
      }
    }
    
    // Red segment
    if (fillDeg > speedSpan * 0.85f) {
      float redStartAngle = speedStart + speedSpan * 0.85f;
      float redEndAngle = speedStart + fillDeg;
      
      if (redStartAngle >= 360.0f) {
        redStartAngle -= 360.0f;
        redEndAngle -= 360.0f;
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, redStartAngle, redEndAngle, cs.arcHigh);
      } else if (redEndAngle > 360.0f) {
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, redStartAngle, 360, cs.arcHigh);
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, 0, redEndAngle - 360.0f, cs.arcHigh);
      } else {
        fillArcToSprite(&sprite, cx, cy, rInner, rOuter, redStartAngle, redEndAngle, cs.arcHigh);
      }
    }
  }

  // Battery arc background
  fillArcToSprite(&sprite, cx, cy, rBatInner, rBatOuter, batStart, batEnd, cs.arcBackground);
  
  // Battery arc filled portion
  float currentBatFill = (ui.battery_pc / 100.0f) * batSpan;
  uint16_t batColor = (ui.battery_pc < 20) ? cs.arcHigh : cs.arcLow;
  if (currentBatFill > 0) {
    fillArcToSprite(&sprite, cx, cy, rBatInner, rBatOuter, batStart, batStart + currentBatFill, batColor);
  }
  
  // Satellite arc background
  fillArcToSprite(&sprite, cx, cy, rSatInner, rSatOuter, satEnd, satStart, cs.arcBackground);
  
  // Satellite arc filled portion
  float currentSatFill = (min(ui.satellites, 20) / 20.0f) * satSpan;
  if (currentSatFill > 0) {
    fillArcToSprite(&sprite, cx, cy, rSatInner, rSatOuter, satStart - currentSatFill, satStart, cs.arcLow);
  }
  
  // Draw anti-aliased borders around all arc bars
  // Note: drawArc uses standard angles (0° = 3 o'clock), so subtract 90° from our UI angles
  uint16_t borderColor = ui.isDarkMode ? TFT_WHITE : TFT_BLACK;
  
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

  // Draw speed number with font (24pt bold, moved up for tighter spacing)
  sprite.setTextDatum(MC_DATUM);
  sprite.setFont(&fonts::FreeSansBold24pt7b);
  sprite.setTextColor(cs.speedText, cs.background);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", (int)roundf(ui.speed_kmh));
  sprite.drawString(buf, cx, cy - 18);
  sprite.setFont(nullptr);  // Reset to default font
  
  // Draw units above the speed numbers (larger size with tighter spacing)
  sprite.setTextSize(2);
  sprite.setTextColor(cs.unitsText, cs.background);
  sprite.drawString(ui.units, cx, cy - 52);
  sprite.setTextSize(1);  // Reset text size

  // Battery icon and percentage text
  int batIconX = cx - 40;
  int batIconY = cy + 55;
  
  uint16_t batIconColor = (ui.battery_pc < 20) ? cs.arcHigh : cs.iconNormal;
  sprite.drawRect(batIconX - 12, batIconY - 8, 24, 14, batIconColor);
  sprite.fillRect(batIconX + 12, batIconY - 4, 2, 6, batIconColor);
  int batFill = (ui.battery_pc * 20) / 100;
  if (batFill > 0) {
    sprite.fillRect(batIconX - 10, batIconY - 6, batFill, 10, batIconColor);
  }
  
  sprite.setTextDatum(TC_DATUM);
  sprite.setTextSize(1);
  sprite.setTextColor(cs.text, cs.background);
  char batTxt[6];
  snprintf(batTxt, sizeof(batTxt), "%d%%", ui.battery_pc);
  sprite.drawString(batTxt, batIconX, batIconY + 10);

  // Satellite icon (inspired by ac-illust design, flipped horizontally)
  // Positioned at right, with solar panel on left side
  int satIconX = cx + 40;
  int satIconY = cy + 55;
  
  // Satellite body (main rectangular body)
  sprite.fillRect(satIconX - 2, satIconY - 5, 8, 10, cs.iconNormal);
  sprite.drawRect(satIconX - 2, satIconY - 5, 8, 10, cs.iconNormal);
  
  // Solar panel on the left side (flipped from original)
  sprite.fillRect(satIconX - 10, satIconY - 3, 7, 6, cs.iconNormal);
  sprite.drawLine(satIconX - 10, satIconY - 1, satIconX - 3, satIconY - 1, cs.background);
  sprite.drawLine(satIconX - 10, satIconY + 1, satIconX - 3, satIconY + 1, cs.background);
  
  // Antenna on top
  sprite.drawLine(satIconX + 2, satIconY - 5, satIconX + 2, satIconY - 9, cs.iconNormal);
  sprite.fillCircle(satIconX + 2, satIconY - 10, 2, cs.iconNormal);
  
  // Signal waves (arcs emanating from satellite)
  for (int i = 1; i <= 3; i++) {
    sprite.drawArc(satIconX + 6, satIconY, 4 + i*3, 4 + i*3, 315, 45, cs.iconNormal);
  }
  
  sprite.setTextDatum(TC_DATUM);
  sprite.setTextSize(1);
  sprite.setTextColor(cs.text, cs.background);
  char satTxt[6];
  snprintf(satTxt, sizeof(satTxt), "%d", ui.satellites);
  sprite.drawString(satTxt, satIconX, satIconY + 10);

  // Sun/Moon icon (moved up and made bigger)
  int sunX = cx;
  int iconY = cy + 24;  // Moved up from cy+32 (about half the distance)
  
  if (ui.isDarkMode) {
    // Moon (crescent shape, bigger)
    sprite.fillCircle(sunX, iconY, 11, cs.iconNormal);
    sprite.fillCircle(sunX + 6, iconY - 3, 10, cs.background);
  } else {
    // Sun (circle with rays, bigger)
    sprite.fillCircle(sunX, iconY, 10, cs.iconNormal);
    for (int i = 0; i < 8; i++) {
      float angle = i * 45.0f;
      int x1, y1, x2, y2;
      polarPoint(sunX, iconY, 12, angle, x1, y1);
      polarPoint(sunX, iconY, 18, angle, x2, y2);
      sprite.drawLine(x1, y1, x2, y2, cs.iconNormal);
    }
  }
  
  // Push the entire sprite buffer to display in one operation
  sprite.pushSprite(0, 0);
  
  // Update tracking variables
  ui.prev_speed = ui.speed_kmh;
  ui.prev_battery = ui.battery_pc;
  ui.prev_satellites = ui.satellites;
  ui.needsFullRedraw = false;
}

void renderSettings() {
  const int W = display.width();
  const int H = display.height();
  const int cx = W/2;
  const int cy = H/2;
  ColorScheme& cs = getColors();
  
  display.fillScreen(cs.background);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(cs.text, cs.background);
  display.setTextSize(2);
  display.drawString("Settings", cx, 30);

  display.setTextSize(1);
  display.setTextDatum(MC_DATUM);
  
  // Display mode section
  display.setTextColor(cs.text, cs.background);
  display.drawString("Display Mode:", cx, 60);
  display.setTextColor(ui.isDarkMode ? cs.text : cs.arcMid, cs.background);
  display.drawString(ui.isDarkMode ? "> Dark" : "  Light", cx - 35, 78);
  display.setTextColor(ui.isDarkMode ? cs.arcMid : cs.text, cs.background);
  display.drawString(ui.isDarkMode ? "  Light" : "> Dark", cx + 35, 78);
  
  // Units section
  display.setTextColor(cs.text, cs.background);
  display.drawString("Units:", cx, 105);
  display.setTextColor(cs.arcMid, cs.background);
  display.drawString("> km/h", cx, 123);
  display.setTextColor(cs.iconDim, cs.background);
  display.drawString("mph / m/s", cx, 138);

  // Speed scale section
  display.setTextColor(cs.text, cs.background);
  display.drawString("Speed Scale:", cx, 163);
  display.setTextColor(cs.arcMid, cs.background);
  display.drawString("> Driving (220)", cx, 181);
  display.setTextColor(cs.iconDim, cs.background);
  display.drawString("Walking / Cycling", cx, 196);

  display.setTextColor(cs.iconDim, cs.background);
  display.setTextSize(1);
  display.drawString("Swipe to navigate", cx, 220);
}

void renderMetrics() {
  const int W = display.width();
  const int H = display.height();
  const int cx = W/2;
  const int cy = H/2;
  ColorScheme& cs = getColors();
  
  display.fillScreen(cs.background);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(cs.text, cs.background);
  display.setTextSize(2);
  display.drawString("Metrics", cx, 30);

  display.setTextSize(1);
  display.setTextDatum(MC_DATUM);
  
  char line[48];
  display.setTextColor(cs.text, cs.background);
  
  snprintf(line, sizeof(line), "Satellites: %d", ui.satellites);
  display.drawString(line, cx, 70);
  
  snprintf(line, sizeof(line), "Battery: %d%%", ui.battery_pc);
  display.drawString(line, cx, 95);
  
  snprintf(line, sizeof(line), "Speed: %.1f %s", ui.speed_kmh, ui.units);
  display.drawString(line, cx, 120);
  
  display.setTextColor(cs.iconDim, cs.background);
  snprintf(line, sizeof(line), "Lat: %.5f", ui.lat);
  display.drawString(line, cx, 150);
  
  snprintf(line, sizeof(line), "Lon: %.5f", ui.lon);
  display.drawString(line, cx, 165);

  display.setTextColor(cs.iconDim, cs.background);
  display.drawString("Swipe to navigate", cx, 205);
}

// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.begin(115200);
  #endif
  delay(100);

  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  display.invertDisplay(true);

  Serial.printf("Display %dx%d ready. Controls: 'a' left, 'd' right, 'm' toggle mode.\n", display.width(), display.height());
  #if defined(TOUCH_I2C_SCANNER)
    Serial.println("Touch scanner enabled. Running scan to help determine SDA/SCL pins...");
    scanI2CForTouch();
    Serial.println("Touch scanner finished. Configure pins via build_flags and rebuild.");
  #endif
  
  // Draw initial screen
  renderMain();
}

static void renderActive() {
  switch (currentScreen) {
    case Screen::MAIN:    renderMain(); break;
    case Screen::SETTINGS:renderSettings(); break;
    case Screen::METRICS: renderMetrics(); break;
  }
}

void loop() {
  static uint32_t lastUpdate = 0;
  static uint32_t lastDemo   = 0;

  // Touch input (swipe left/right to change screens, tap center to toggle mode)
  {
    int tx, ty;
    bool pressed = display.getTouch(&tx, &ty);
    if (pressed && !swipe.touching) {
      // touch start
      swipe.touching = true;
      swipe.startX = swipe.lastX = tx;
      swipe.startY = swipe.lastY = ty;
      swipe.startMs = millis();
    } else if (pressed && swipe.touching) {
      // touch move
      swipe.lastX = tx;
      swipe.lastY = ty;
    } else if (!pressed && swipe.touching) {
      // touch end -> evaluate gesture
      int dx = swipe.lastX - swipe.startX;
      int dy = swipe.lastY - swipe.startY;
      uint32_t dt = millis() - swipe.startMs;

      // Horizontal swipe?
      if (abs(dx) >= SWIPE_THRESHOLD_PX && abs(dy) < SWIPE_THRESHOLD_PX) {
        if (dx < 0) {
          // Swipe left -> next screen
          currentScreen = (currentScreen == Screen::MAIN) ? Screen::SETTINGS
                          : (currentScreen == Screen::SETTINGS ? Screen::METRICS : Screen::MAIN);
        } else {
          // Swipe right -> previous screen
          currentScreen = (currentScreen == Screen::MAIN) ? Screen::METRICS
                          : (currentScreen == Screen::SETTINGS ? Screen::MAIN : Screen::SETTINGS);
        }
        ui.needsFullRedraw = true;
        renderActive();
      } else if (abs(dx) < TAP_THRESHOLD_PX && abs(dy) < TAP_THRESHOLD_PX && dt <= TAP_TIME_MS) {
        // Tap near center toggles dark/light mode
        int cx = display.width()/2;
        int cy = display.height()/2;
        if (abs(swipe.startX - cx) < 80 && abs(swipe.startY - cy) < 80) {
          ui.isDarkMode = !ui.isDarkMode;
          ui.needsFullRedraw = true;
          renderActive();
        }
      }
      swipe.touching = false;
    }
  }

  // Input via Serial also supported: 'a' = left, 'd' = right, 'm' = toggle mode
  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'a' || c == 'A') {
      currentScreen = (currentScreen == Screen::MAIN) ? Screen::METRICS : (currentScreen == Screen::SETTINGS ? Screen::MAIN : Screen::SETTINGS);
      ui.needsFullRedraw = true;
      renderActive();
    } else if (c == 'd' || c == 'D') {
      currentScreen = (currentScreen == Screen::MAIN) ? Screen::SETTINGS : (currentScreen == Screen::SETTINGS ? Screen::METRICS : Screen::MAIN);
      ui.needsFullRedraw = true;
      renderActive();
    } else if (c == 'm' || c == 'M') {
      ui.isDarkMode = !ui.isDarkMode;
      ui.needsFullRedraw = true;
      Serial.printf("Switched to %s mode\n", ui.isDarkMode ? "dark" : "light");
      renderActive();
    }
  }

  // Periodic demo updates for MAIN screen (animate values)
  uint32_t now = millis();
  if (now - lastDemo > 120) {
    lastDemo = now;
    // simple oscillation of speed 0..max
    static float t = 0.0f; t += 0.04f; if (t > TWO_PI) t -= TWO_PI;
    ui.speed_kmh = (sinf(t) * 0.5f + 0.5f) * ui.max_kmh;
    ui.battery_pc = 20 + (int)((sinf(t*0.4f)*0.5f + 0.5f) * 80);
    ui.satellites = 5 + (int)((sinf(t*0.7f)*0.5f + 0.5f) * 15);
    if (currentScreen == Screen::MAIN) renderMain();
  }

  // Initial draw and slow refresh for other screens
  if (now - lastUpdate > 2000) {
    lastUpdate = now;
    if (currentScreen != Screen::MAIN) renderActive();
  }
}
