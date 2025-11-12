// Define GPS_TEST to build this standalone UART/NMEA reader without touching your main app.
// Add to platformio.ini under your env: build_flags = -DGPS_TEST

#ifdef GPS_TEST

#include <Arduino.h>

// HardwareSerial port 1 on ESP32-S3; pins are remappable.
// Your wiring (colors from your note):
//  - GND  = black  -> GND
//  - 3V3  = red    -> 3V3
//  - GPIO15 = yellow (ESP TX)
//  - GPIO16 = green  (ESP RX)
// Therefore:
//  - GPS TX -> ESP RX GPIO16 (green)
//  - GPS RX -> ESP TX GPIO15 (yellow) [optional if you need to send config]
// Assumes a typical 9600 baud NMEA GPS module. Adjust baud if your module differs.

static constexpr int GPS_RX_PIN = 16; // ESP32 receives data here (connect to GPS TX)
static constexpr int GPS_TX_PIN = 15; // ESP32 transmits (connect to GPS RX) -- optional
// We'll auto-cycle common baud rates until we see data.
static const uint32_t BAUDS[] = {9600, 38400, 115200};
static constexpr size_t BAUDS_LEN = sizeof(BAUDS) / sizeof(BAUDS[0]);
static size_t baudIndex = 0;

HardwareSerial GPS(1); // Use UART1 (not the default Serial0 used by USB)

// Simple line buffer for incoming NMEA sentences
String nmeaLine;

// Parse a GGA sentence minimally to extract fix quality and satellite count.
// Returns true if parsed.
bool parseGGA(const String &line, int &fixQuality, int &satCount) {
  if (!(line.startsWith("$GPGGA") || line.startsWith("$GNGGA"))) return false;
  int fieldIndex = 0;
  int lastPos = 0;
  fixQuality = -1;
  satCount = -1;
  for (int i = 0; i <= line.length(); ++i) {
    if (i == line.length() || line[i] == ',') {
      String field = line.substring(lastPos, i);
      lastPos = i + 1;
      fieldIndex++;
      // Field order (0-based conceptually after the '$GPGGA'): time, lat, N/S, lon, E/W, fix quality, satellites ...
      // Our loop counts starting at 1 for first field after sentence ID.
      if (fieldIndex == 6) { // fix quality
        fixQuality = field.toInt();
      } else if (fieldIndex == 7) { // satellites in use
        satCount = field.toInt();
        break; // we can stop early
      }
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(400); // Allow USB Serial to come up
  Serial.println(F("[GPS TEST] Initializing GPS UART..."));
  GPS.begin(BAUDS[baudIndex], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("[GPS TEST] UART1 started @ %lu baud on RX=%d TX=%d\n", (unsigned long)BAUDS[baudIndex], GPS_RX_PIN, GPS_TX_PIN);
  Serial.println(F("[GPS TEST] Waiting for NMEA sentences (lines starting with '$')..."));
  Serial.println(F("[GPS TEST] If nothing appears in 10s, check wiring, power, and baud."));
}

void loop() {
  static uint32_t lastRx = millis();
  while (GPS.available()) {
    char c = GPS.read();
    lastRx = millis();
    if (c == '\n') {
      nmeaLine.trim();
      if (nmeaLine.startsWith("$")) {
        Serial.println(nmeaLine); // Echo raw sentence
        int fixQ, sats;
        if (parseGGA(nmeaLine, fixQ, sats)) {
          Serial.printf("[GGA] Satellites=%d FixQuality=%d\n", sats, fixQ);
          // Fix quality meanings (common): 0=Invalid, 1=GPS, 2=DGPS, 4=RTK Fixed, 5=RTK Float
        }
      }
      nmeaLine = ""; // reset for next sentence
    } else if (c != '\r') {
      nmeaLine += c;
      if (nmeaLine.length() > 120) { // Avoid runaway lines if baud mismatch
        nmeaLine = ""; // reset overly long line
      }
    }
  }

  // If we haven't received anything for a while, try the next baud.
  if (millis() - lastRx > 8000) {
    baudIndex = (baudIndex + 1) % BAUDS_LEN;
    GPS.updateBaudRate(BAUDS[baudIndex]);
    Serial.printf("[GPS TEST] No data, switching to %lu baud...\n", (unsigned long)BAUDS[baudIndex]);
    lastRx = millis();
  }

  // Optional: every 5s print a heartbeat so we know firmware is alive
  static uint32_t lastBeat = 0;
  uint32_t now = millis();
  if (now - lastBeat > 5000) {
    lastBeat = now;
    Serial.println(F("[GPS TEST] Heartbeat - still reading..."));
  }
}

#endif // GPS_TEST
