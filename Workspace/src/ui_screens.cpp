#include "ui_screens.hpp"
#include <cmath>

static uint16_t COL_BG_DARK = 0x0000;
static uint16_t COL_BG_LIGHT = 0xFFFF;
static uint16_t COL_TEXT_DARK = TFT_WHITE;
static uint16_t COL_TEXT_LIGHT = TFT_BLACK;
static uint16_t COL_ACCENT = TFT_GREEN;

static void header(LGFX &g, const AppData &d, const char* title) {
  g.fillRect(0, 0, g.width(), 24, d.darkMode ? 0x18E3 : 0xC618);
  g.setTextColor(d.darkMode ? COL_TEXT_DARK : COL_TEXT_LIGHT);
  g.setTextDatum(ML_DATUM);
  g.drawString(title, 8, 12);
  char r[24]; snprintf(r, sizeof(r), "%d%%", d.batteryPercent);
  g.setTextDatum(MR_DATUM);
  g.drawString(r, g.width()-8, 12);
}

void SpeedScreen::render(LGFX &g, const AppData &d) {
  g.fillScreen(d.darkMode ? COL_BG_DARK : COL_BG_LIGHT);
  header(g, d, "Speed");
  int cx = g.width()/2;
  int cy = g.height()/2 + 10;

  // gauge arc
  float frac = fminf(fmaxf(d.gps.speedKmh / d.maxSpeedKmh, 0.0f), 1.0f);
  int segments = 100;
  int filled = (int)(segments * frac);
  for (int i = 0; i < segments; ++i) {
    float start = (240.0f + (i * 240.0f / segments));
    float ang = start;
    if (ang >= 360.0f) ang -= 360.0f;
    float rad = (ang - 90.0f) * DEG_TO_RAD;
    int x1 = cx + (int)(cosf(rad) * 90.0f);
    int y1 = cy + (int)(sinf(rad) * 90.0f);
    int x2 = cx + (int)(cosf(rad) * 110.0f);
    int y2 = cy + (int)(sinf(rad) * 110.0f);
    uint16_t col = (i < filled) ? COL_ACCENT : TFT_DARKGREY;
    g.drawLine(x1, y1, x2, y2, col);
  }

  g.setTextDatum(MC_DATUM);
  g.setFont(&fonts::FreeSansBold12pt7b);
  g.setTextColor(d.darkMode ? COL_TEXT_DARK : COL_TEXT_LIGHT, d.darkMode ? COL_BG_DARK : COL_BG_LIGHT);
  char spd[24]; snprintf(spd, sizeof(spd), "%.0f km/h", d.gps.speedKmh);
  g.drawString(spd, cx, cy-10);
  g.setFont(nullptr);

  g.setTextDatum(TC_DATUM);
  g.setTextColor(d.darkMode ? TFT_LIGHTGREY : TFT_DARKGREY);
  char line1[48]; snprintf(line1, sizeof(line1), "SAT:%d ALT:%.1fm", d.gps.satsUsed, d.gps.altitude);
  g.drawString(line1, cx, cy + 45);
}

void MetricsScreen::render(LGFX &g, const AppData &d) {
  g.fillScreen(d.darkMode ? COL_BG_DARK : COL_BG_LIGHT);
  header(g, d, "Metrics");
  int x = 16, y = 40, dy = 22;
  g.setTextDatum(ML_DATUM);
  g.setTextColor(d.darkMode ? COL_TEXT_DARK : COL_TEXT_LIGHT);
  char buf[64];
  snprintf(buf, sizeof(buf), "Fix:%s Q:%d", d.gps.validFix?"Y":"N", d.gps.fixQuality); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Sats used:%d inView:%d", d.gps.satsUsed, d.gps.satsInView); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Lat: %.6f", d.gps.lat); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Lon: %.6f", d.gps.lon); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Alt: %.1fm", d.gps.altitude); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Spd: %.1f kn / %.1f km/h", d.gps.speedKnots, d.gps.speedKmh); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Course: %.1f deg", d.gps.courseDeg); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Time: %s UTC", d.gps.timeUTC); g.drawString(buf, x, y); y+=dy;
  snprintf(buf, sizeof(buf), "Date: %s", d.gps.date); g.drawString(buf, x, y); y+=dy;
}

void SettingsScreen::render(LGFX &g, const AppData &d) {
  g.fillScreen(d.darkMode ? COL_BG_DARK : COL_BG_LIGHT);
  header(g, d, "Settings");
  g.setTextColor(d.darkMode ? COL_TEXT_DARK : COL_TEXT_LIGHT);
  g.setTextDatum(MC_DATUM);
  g.drawString("Tap to cycle screens", g.width()/2, g.height()/2 - 10);
  g.drawString("(long-press to toggle theme)", g.width()/2, g.height()/2 + 10);
}
