// UI mock for Waveshare ESP32-S3 Round LCD (GC9A01)
#include <Arduino.h>
#include "display_config.hpp"

// Create display instance
LGFX display;

// ---------- UI State ----------
enum class Screen { MAIN, SETTINGS, METRICS };
Screen currentScreen = Screen::MAIN;

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
  .arcLow        = 0x07E0,  // bright green
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
  .arcLow        = 0x07E0,  // bright green
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
  
  // Battery arc outer and inner borders
  // UI angles: 185° to 240° = drawArc 95° to 150°
  sprite.drawArc(cx, cy, rBatOuter, rBatOuter, 95, 150, borderColor);
  sprite.drawArc(cx, cy, rBatInner, rBatInner, 95, 150, borderColor);
  
  // Satellite arc outer and inner borders
  // UI angles: 120° to 175° = drawArc 30° to 85°
  sprite.drawArc(cx, cy, rSatOuter, rSatOuter, 30, 85, borderColor);
  sprite.drawArc(cx, cy, rSatInner, rSatInner, 30, 85, borderColor);

  // Draw speed number
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(cs.speedText, cs.background);
  sprite.setTextSize(5);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", (int)roundf(ui.speed_kmh));
  sprite.drawString(buf, cx, cy - 18);
  
  // Draw units
  sprite.setTextSize(2);
  sprite.setTextColor(cs.unitsText, cs.background);
  sprite.drawString(ui.units, cx, cy + 32);

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

  // Satellite icon and count
  int satIconX = cx + 40;
  int satIconY = cy + 55;
  
  sprite.drawRect(satIconX - 8, satIconY - 4, 16, 8, cs.iconNormal);
  sprite.drawLine(satIconX + 8, satIconY - 4, satIconX + 12, satIconY - 8, cs.iconNormal);
  sprite.drawLine(satIconX + 12, satIconY - 8, satIconX + 14, satIconY - 8, cs.iconNormal);
  sprite.fillCircle(satIconX + 14, satIconY - 10, 2, cs.iconNormal);
  for (int i = 1; i <= 2; i++) {
    sprite.drawArc(satIconX + 8, satIconY, 4 + i*3, 4 + i*3, 225, 315, cs.iconNormal);
  }
  
  sprite.setTextDatum(TC_DATUM);
  sprite.setTextSize(1);
  sprite.setTextColor(cs.text, cs.background);
  char satTxt[6];
  snprintf(satTxt, sizeof(satTxt), "%d", ui.satellites);
  sprite.drawString(satTxt, satIconX, satIconY + 10);

  // Sun/Moon icon at bottom center
  int sunX = cx;
  int iconY = cy + 60;
  
  if (ui.isDarkMode) {
    sprite.fillCircle(sunX, iconY, 7, cs.iconNormal);
    sprite.fillCircle(sunX + 4, iconY - 2, 6, cs.background);
  } else {
    sprite.fillCircle(sunX, iconY, 6, cs.iconNormal);
    for (int i = 0; i < 8; i++) {
      float angle = i * 45.0f;
      int x1, y1, x2, y2;
      polarPoint(sunX, iconY, 8, angle, x1, y1);
      polarPoint(sunX, iconY, 12, angle, x2, y2);
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
  ColorScheme& cs = getColors();
  
  display.fillScreen(cs.background);
  display.setTextDatum(TL_DATUM);
  display.setTextColor(cs.text, cs.background);
  display.setTextSize(2);
  display.drawString("Settings", 10, 10);

  display.setTextSize(1);
  
  // Display mode section
  display.drawString("Display:", 10, 40);
  display.setTextColor(ui.isDarkMode ? cs.text : cs.arcMid, cs.background);
  display.drawString(ui.isDarkMode ? " > Dark mode" : "   Light mode", 20, 56);
  display.setTextColor(ui.isDarkMode ? cs.arcMid : cs.text, cs.background);
  display.drawString(ui.isDarkMode ? "   Light mode" : " > Dark mode", 20, 70);
  
  // Units section
  display.setTextColor(cs.text, cs.background);
  display.drawString("Units:", 10, 94);
  display.setTextColor(cs.arcMid, cs.background);
  display.drawString(" > km/h (current)", 20, 110);
  display.setTextColor(cs.text, cs.background);
  display.drawString("   mph", 20, 124);
  display.drawString("   m/s", 20, 138);

  // Speed scale section
  display.drawString("Speed scale:", 10, 162);
  display.setTextColor(cs.arcMid, cs.background);
  display.drawString(" > Driving 220 km/h", 20, 178);
  display.setTextColor(cs.text, cs.background);
  display.drawString("   Walking 20 km/h", 20, 192);
  display.drawString("   Cycling 60 km/h", 20, 206);

  display.setTextColor(cs.iconDim, cs.background);
  display.drawString("'a'/'d' navigate, 'm' mode", 10, H-14);
}

void renderMetrics() {
  const int W = display.width();
  const int H = display.height();
  ColorScheme& cs = getColors();
  
  display.fillScreen(cs.background);
  display.setTextDatum(TL_DATUM);
  display.setTextColor(cs.text, cs.background);
  display.setTextSize(2);
  display.drawString("Metrics", 10, 10);

  display.setTextSize(1);
  char line[48];
  snprintf(line, sizeof(line), "Satellites: %d", ui.satellites);
  display.drawString(line, 10, 60);
  snprintf(line, sizeof(line), "Battery: %d%%", ui.battery_pc);
  display.drawString(line, 10, 80);
  snprintf(line, sizeof(line), "Lat: %.5f", ui.lat);
  display.drawString(line, 10, 100);
  snprintf(line, sizeof(line), "Lon: %.5f", ui.lon);
  display.drawString(line, 10, 120);

  display.setTextColor(cs.iconDim, cs.background);
  display.drawString("'a'/'d' navigate", 10, H-14);
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

  // Input via Serial for now (mock swipe): 'a' = left, 'd' = right, 'm' = toggle mode
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
