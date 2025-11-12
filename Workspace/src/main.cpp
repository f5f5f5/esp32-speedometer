// Stripped main.cpp (not used in gps_test environment due to build_src_filter)
// Left intentionally minimal to avoid accidental linkage conflicts.
// If building the full app, replace with application entry forwarding.
void setup() {}
void loop() {}

// Fill arc segment to sprite for double buffering
void fillArcToSprite(LGFX_Sprite* spr, int cx, int cy, float rInner, float rOuter, float startDeg, float endDeg, uint16_t color) {
  if (startDeg > endDeg) return;
  const float stepDeg = ARC_STEP_DEGREES;
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
  
  // Create sprite buffer for double buffering (use global shared sprite)
  if (!spriteInit) { sprite.createSprite(W, H); spriteInit = true; }
  
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
    if (greenEnd > 0.0f) {
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
  // Blue when USB powered, red when low battery, green otherwise
  uint16_t batColor;
  if (battery.isUSBPowered()) {
    batColor = 0x051D;  // Blue (RGB565: 0,160,232)
  } else if (ui.battery_pc < 20) {
    batColor = cs.arcHigh;  // Red for low battery
  } else {
    batColor = cs.arcLow;  // Green for normal battery
  }
  if (currentBatFill > 0.0f) {
    fillArcToSprite(&sprite, cx, cy, rBatInner, rBatOuter, batStart, batStart + currentBatFill, batColor);
  }
  
  // Satellite arc background
  fillArcToSprite(&sprite, cx, cy, rSatInner, rSatOuter, satEnd, satStart, cs.arcBackground);
  
  // Satellite arc filled portion
  float currentSatFill = (min(ui.satellites, 20) / 20.0f) * satSpan;
  if (currentSatFill > 0.0f) {
    fillArcToSprite(&sprite, cx, cy, rSatInner, rSatOuter, satStart - currentSatFill, satStart, cs.arcLow);
  }
  
  // Draw anti-aliased borders around all arc bars
  // Note: drawArc uses standard angles (0° = 3 o'clock), so subtract 90° from our UI angles
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
  // Purged file for GPS test isolation
  #ifdef GPS_TEST
  void setup() {}
  void loop() {}
  #else
  void setup() {}
  void loop() {}
  #endif

  // Satellite icon (inspired by ac-illust design, flipped horizontally)
  // Positioned at right, with solar panel on left side
  int satIconX = cx + 40;
  int satIconY = cy + 55;
  
  // Satellite body (main rectangular body)
  // Clean minimal main.cpp for gps_test environment forwarding to gps_test.cpp
  #include <Arduino.h>

  void gps_test_setup();
  void gps_test_loop();

  void setup() { gps_test_setup(); }
  void loop() { gps_test_loop(); }
    int x1 = cx + (int)(cos(rad) * r1);
    int y1 = cy + (int)(sin(rad) * r1);
    int x2 = cx + (int)(cos(rad) * r2);
    int y2 = cy + (int)(sin(rad) * r2);
    display.drawLine(x1, y1, x2, y2, arcColor);
  }
  
  // Draw needle indicator pointing to ~45 degrees (mid-range)
  // Only the last ~15 pixels visible, with 2-pixel gap from arc
  float needleAngle = 250;  // pointing position on the arc
  float rad = (needleAngle - 90) * PI / 180.0f;
  
  const float gapFromArc = 2.0f;         // gap between needle tip and arc inner edge
  const float visibleLength = 15.0f;    // visible portion of needle
  
  float needleTip = r1 - gapFromArc;         // just before arc inner edge
  float needleStart = needleTip - visibleLength;  // 15 pixels back toward center
  
  int nx1 = cx + (int)(cos(rad) * needleStart);
  int ny1 = cy + (int)(sin(rad) * needleStart);
  int nx2 = cx + (int)(cos(rad) * needleTip);
  int ny2 = cy + (int)(sin(rad) * needleTip);
  
  // Draw red needle (visible portion only)
  display.drawLine(nx1, ny1, nx2, ny2, TFT_RED);
  display.drawLine(nx1-1, ny1, nx2-1, ny2, TFT_RED);  // thicker
  display.drawLine(nx1+1, ny1, nx2+1, ny2, TFT_RED);
  
  // Center hub
  display.fillCircle(cx, cy, 6, TFT_WHITE);
  
  // App title
  display.setTextDatum(MC_DATUM);
  display.setFont(&fonts::FreeSansBold12pt7b);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("SPEEDOMETER", cx, cy + 50);
  display.setFont(nullptr);
  
  // Subtext
  display.setTextSize(1);
  display.setTextColor(0x8410, TFT_BLACK); // gray
  display.drawString("Initializing...", cx, cy + 75);
  
  // Version/credit (optional)
  display.setTextColor(0x4208, TFT_BLACK); // darker gray
  display.drawString("v1.0", cx, H - 20);
}

// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.begin(115200);
  #endif
  
  // Brief startup delay
  delay(100);

  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  display.invertDisplay(true);
  
  // Show splash screen while initializing
  renderSplash();
  
  // Initialize battery management
  battery.begin();
  
  // Give battery time to update and settle (update() runs once per second)
  delay(1500);
  battery.update();  // Force an update to get initial percentage
  ui.battery_pc = battery.getPercentage();
  
  // Hold splash screen a bit longer for visual effect
  delay(500);

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
  static uint32_t lastLowBatFlash = 0;
  
  // Update battery reading
  battery.update();
  ui.battery_pc = battery.getPercentage();

  // Update LOW BAT flash state (toggle every second, independent of redraws)
  uint32_t now = millis();
  if (now - lastLowBatFlash > 1000) {
    ui.lowBatFlashState = !ui.lowBatFlashState;
    lastLowBatFlash = now;
    // Trigger redraw on main screen if low battery warning is active
    if (battery.isLowBattery() && !battery.isUSBPowered() && currentScreen == Screen::MAIN) {
      renderMain();
    }
  }

  // Check if battery state changed (USB plugged/unplugged, charging status)
  BatteryState currentBatteryState = battery.getState();
  if (currentBatteryState != ui.prev_battery_state) {
    ui.prev_battery_state = currentBatteryState;
    // Redraw current screen to reflect new state
    renderActive();
  }

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

  // Periodic demo updates for MAIN screen (animate values) - disabled unless DEMO_MODE is defined
  #ifdef DEMO_MODE
  if (now - lastDemo > 120) {
    lastDemo = now;
    // simple oscillation of speed and satellites (battery uses real values)
    static float t = 0.0f; t += 0.04f; if (t > TWO_PI) t -= TWO_PI;
    // Demo target peak at 230 km/h; dial max remains 220, so needle/arc will peg at max
    const float demoTopKmh = 230.0f;
    ui.speed_kmh = (sinf(t) * 0.5f + 0.5f) * demoTopKmh;
    ui.satellites = 5 + (int)((sinf(t*0.7f)*0.5f + 0.5f) * 15);
    if (currentScreen == Screen::MAIN) renderMain();
  }
  #endif
  
  // Periodic screen updates (every 1 second for Metrics and Settings to show live data)
  if (now - lastUpdate > 1000) {
    lastUpdate = now;
    if (currentScreen == Screen::METRICS || currentScreen == Screen::SETTINGS) {
      renderActive();
    }
  }
}
