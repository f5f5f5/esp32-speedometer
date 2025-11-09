// Hello World for Waveshare ESP32-S3 Round LCD (GC9A01)
#include <Arduino.h>
#include "display_config.hpp"

// Create display instance
LGFX display;

void setup() {
  Serial.begin(115200);
  // Also log on UART0 in case Serial is mapped to USB CDC
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.begin(115200);
    Serial0.println("UART0 fallback active");
  #endif
  delay(100);
  Serial.println("Hello demo starting - ESP32-S3 Round LCD (GC9A01)");
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.println("Hello demo starting - ESP32-S3 Round LCD (GC9A01)");
  #endif

  // initialize display
  display.init();
  display.setRotation(0);
  display.setBrightness(255); // max backlight for visibility
  display.invertDisplay(true);

  Serial.printf("Display size: %dx%d\n", display.width(), display.height());
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.printf("Display size: %dx%d\n", display.width(), display.height());
  #endif

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.setTextDatum(MC_DATUM);

  int cx = display.width() / 2;
  int cy = display.height() / 2;
  display.drawString("Hello World!", cx, cy);
  delay(1000);

  // Draw a white border to ensure pixels appear
  display.drawRect(0, 0, display.width(), display.height(), TFT_WHITE);
  Serial.println("Drew white border");
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
    Serial0.println("Drew white border");
  #endif
  delay(500);

  // Palette verification: show raw RGB565 primaries and mixes in a grid
  const uint16_t swatches[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_MAGENTA, TFT_CYAN, TFT_WHITE, TFT_BLACK};
  const char*   labels[]   = {"RED","GREEN","BLUE","YEL","MAG","CYN","WHT","BLK"};
  int cols = 4;
  int cellW = display.width() / cols;
  int cellH = 40;
  for (size_t i = 0; i < sizeof(swatches) / sizeof(swatches[0]); ++i) {
    int col = i % cols;
    int row = i / cols;
    int x = col * cellW; int y = 40 + row * cellH;
    display.fillRect(x, y, cellW, cellH, swatches[i]);
    display.setTextColor((swatches[i] == 0x0000 ? TFT_WHITE : TFT_BLACK), swatches[i]);
    display.drawString(labels[i], x + cellW / 2, y + cellH / 2);
  }
} // <-- Add this closing brace to end setup()

void loop() {
  static uint32_t t = 0;
  static int phase = 0;
  static bool inverted = false;
  uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK};
  const char* names[] = {"RED","GREEN","BLUE","YELLOW","CYAN","MAGENTA","WHITE","BLACK"};

  uint32_t now = millis();
  if (now - t > 1000) {
    t = now;
    uint16_t c = colors[phase % (sizeof(colors)/sizeof(colors[0]))];
    display.fillScreen(c);
    display.setTextColor((c == TFT_BLACK) ? TFT_WHITE : TFT_BLACK, c);
    display.drawString(names[phase % (sizeof(colors)/sizeof(colors[0]))], display.width()/2, display.height()/2);
    Serial.printf("Filled screen with %s\n", names[phase % (sizeof(colors)/sizeof(colors[0]))]);
    #if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT==1)
      Serial0.printf("Filled screen with %s\n", names[phase % (sizeof(colors)/sizeof(colors[0]))]);
    #endif

    // Move a small circle around to verify motion
    int r = 8;
    int x = (display.width()/2) + (display.width()/3) * cosf(phase * 0.7f);
    int y = (display.height()/2) + (display.height()/3) * sinf(phase * 0.7f);
    display.fillCircle(x, y, r, (c == TFT_BLACK) ? TFT_WHITE : TFT_BLACK);
    phase++;
  }
}
