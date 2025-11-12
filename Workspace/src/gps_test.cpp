// Simple GPS test sketch for NEO-7M / u-blox on ESP32-S3
// UART1: RX=GPIO16 (connect to GPS TX), TX=GPIO15 (optional: to GPS RX for config)
// Streams raw NMEA and parses GGA (fix/alt), RMC (speed/time), GSV (satellites in view).
// Adds WiFi telnet tether (port 2323) and HTTP status page (port 80) for remote monitoring.
// Provide STA credentials via build_flags (e.g. -DWIFI_STA_SSID=\"YourSSID\" -DWIFI_STA_PASS=\"YourPass\")
// or it falls back to a SoftAP named ESP32-GPS (password gps12345). Indoor usage: expect no fix.
// Cold start can take >30s, full almanac up to ~12 min.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

HardwareSerial GPS(1);

static const int GPS_RX_PIN = 16; // ESP32 reads from GPS TX
static const int GPS_TX_PIN = 15; // ESP32 writes to GPS RX (optional)
static uint32_t lastStatus = 0;
static uint32_t lastSummary = 0;
static uint32_t startMillis = 0;
static char lineBuf[128];
static int linePos = 0;

struct GGAInfo {
  bool validFix = false;
  int fixQuality = 0; // 0=no fix,1=GPS,2=DGPS, >2 other augmentations
  int sats = 0;        // satellites used in solution
  float lat = 0;
  float lon = 0;
  float altitude = 0;  // meters above mean sea level
} gga;

struct RMCInfo {
  bool active = false;   // status A=active, V=void
  float speedKnots = 0;  // ground speed in knots
  float courseDeg = 0;   // track/course over ground in degrees
  char date[8] = {0};    // DDMMYY if available
  char timeUTC[10] = {0}; // HHMMSS.sss raw
} rmc;

struct GSVInfo {
  int inView = 0;        // total satellites in view (from first field)
  int msgCount = 0;      // total messages for this cycle
  int currentMsg = 0;    // current message number
  uint32_t lastUpdate = 0;
} gsv;

#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID ""
#endif
#ifndef WIFI_STA_PASS
#define WIFI_STA_PASS ""
#endif

static WiFiServer telnetServer(2323);
static WiFiClient telnetClient;
static WebServer httpServer(80);

static float nmeaCoordToDeg(const char* fld, bool isLat) {
  // Format: ddmm.mmmm (lat) / dddmm.mmmm (lon)
  if (!fld || !*fld) return 0;
  float val = atof(fld);
  int deg = (int)(val / 100);
  float minutes = val - deg * 100;
  float degVal = deg + minutes / 60.0f;
  return degVal;
}

static int tokenize(char* s, const char* tokens[], int maxTok) {
  int count = 0;
  char* p = s;
  while (p && *p && count < maxTok) {
    tokens[count++] = p;
    char* comma = strchr(p, ',');
    if (!comma) break;
    *comma = '\0';
    p = comma + 1;
  }
  return count;
}

static void parseGGA(char* s) {
  // $GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,geoid,M,,*CS
  const char* tokens[20] = {0};
  int field = tokenize(s, tokens, 20);
  if (field < 10) return;
  gga.fixQuality = atoi(tokens[6]);
  gga.sats = atoi(tokens[7]);
  gga.altitude = atof(tokens[9]);
  gga.validFix = gga.fixQuality > 0;
  gga.lat = nmeaCoordToDeg(tokens[2], true);
  if (tokens[3] && tokens[3][0] == 'S') gga.lat = -gga.lat;
  gga.lon = nmeaCoordToDeg(tokens[4], false);
  if (tokens[5] && tokens[5][0] == 'W') gga.lon = -gga.lon;
}

static void parseRMC(char* s) {
  // $GPRMC,time,status,lat,N,lon,E,sog,course,date,magVar,E*CS
  const char* tokens[20] = {0};
  int field = tokenize(s, tokens, 20);
  if (field < 10) return;
  if (tokens[1]) strncpy(rmc.timeUTC, tokens[1], sizeof(rmc.timeUTC)-1);
  rmc.active = (tokens[2] && tokens[2][0] == 'A');
  rmc.speedKnots = tokens[7] ? atof(tokens[7]) : 0;
  rmc.courseDeg = tokens[8] ? atof(tokens[8]) : 0;
  if (tokens[9]) strncpy(rmc.date, tokens[9], sizeof(rmc.date)-1);
  float lat = nmeaCoordToDeg(tokens[3], true);
  if (tokens[4] && tokens[4][0] == 'S') lat = -lat;
  float lon = nmeaCoordToDeg(tokens[5], false);
  if (tokens[6] && tokens[6][0] == 'W') lon = -lon;
  if (!gga.validFix && rmc.active) {
    gga.lat = lat;
    gga.lon = lon;
  }
}

static void parseGSV(char* s) {
  // $GPGSV,totalMsgs,msgNum,svInView, ... up to 4 satellites per sentence
  const char* tokens[30] = {0};
  int field = tokenize(s, tokens, 30);
  if (field < 4) return;
  gsv.msgCount = atoi(tokens[1]);
  gsv.currentMsg = atoi(tokens[2]);
  gsv.inView = atoi(tokens[3]);
  gsv.lastUpdate = millis();
}

static void wifiStart() {
  if (strlen(WIFI_STA_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_STA_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      delay(200);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] Connected: %s RSSI=%d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.println("[WiFi] STA connect failed, falling back to SoftAP");
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    const char* apSsid = "ESP32-GPS";
    const char* apPass = "gps12345"; // change for security
    bool ok = WiFi.softAP(apSsid, apPass);
    Serial.printf("[WiFi] SoftAP %s %s at %s\n", apSsid, ok?"started":"FAILED", WiFi.softAPIP().toString().c_str());
  }
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("[WiFi] Telnet server listening on port 2323");
  
  // HTTP routes
  httpServer.on("/", []() {
    uint32_t uptime = (millis() - startMillis) / 1000;
    float speedKmh = rmc.speedKnots * 1.852f;
    float speedMph = rmc.speedKnots * 1.15078f;
    
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='2'>";
    html += "<title>ESP32 GPS Monitor</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
    html += ".card{background:white;padding:20px;margin:10px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;margin-top:0;}h2{color:#555;border-bottom:2px solid #4CAF50;padding-bottom:5px;}";
    html += ".metric{display:inline-block;margin:10px 20px 10px 0;}.label{color:#777;font-size:0.9em;}";
    html += ".value{font-size:1.5em;font-weight:bold;color:#333;}.good{color:#4CAF50;}.warn{color:#FF9800;}.bad{color:#f44336;}";
    html += ".mono{font-family:monospace;}</style></head><body>";
    
    html += "<h1>&#x1F6F0; ESP32 GPS Monitor</h1>";
    
    // Status card
    html += "<div class='card'><h2>Status</h2>";
    html += "<div class='metric'><div class='label'>Uptime</div><div class='value'>";
    html += String(uptime) + "s</div></div>";
    html += "<div class='metric'><div class='label'>Fix Quality</div><div class='value ";
    html += gga.validFix ? "good'>GPS" : "bad'>NO FIX";
    html += " (" + String(gga.fixQuality) + ")</div></div>";
    html += "<div class='metric'><div class='label'>RMC Status</div><div class='value ";
    html += rmc.active ? "good'>ACTIVE" : "warn'>VOID";
    html += "</div></div></div>";
    
    // Satellite card
    html += "<div class='card'><h2>Satellites</h2>";
    html += "<div class='metric'><div class='label'>In Use</div><div class='value'>";
    html += String(gga.sats) + "</div></div>";
    html += "<div class='metric'><div class='label'>In View</div><div class='value'>";
    html += String(gsv.inView) + "</div></div></div>";
    
    // Position card
    html += "<div class='card'><h2>Position</h2>";
    html += "<div class='metric'><div class='label'>Latitude</div><div class='value mono'>";
    html += String(gga.lat, 6) + "&deg;</div></div>";
    html += "<div class='metric'><div class='label'>Longitude</div><div class='value mono'>";
    html += String(gga.lon, 6) + "&deg;</div></div>";
    html += "<div class='metric'><div class='label'>Altitude</div><div class='value'>";
    html += String(gga.altitude, 1) + " m</div></div>";
    if (gga.validFix && gga.lat != 0 && gga.lon != 0) {
      html += "<div style='margin-top:10px;'><a href='https://www.google.com/maps?q=";
      html += String(gga.lat, 6) + "," + String(gga.lon, 6);
      html += "' target='_blank' style='color:#4CAF50;'>&#x1F5FA; View on Google Maps</a></div>";
    }
    html += "</div>";
    
    // Speed card
    html += "<div class='card'><h2>Speed &amp; Course</h2>";
    html += "<div class='metric'><div class='label'>Speed (knots)</div><div class='value'>";
    html += String(rmc.speedKnots, 1) + " kn</div></div>";
    html += "<div class='metric'><div class='label'>Speed (km/h)</div><div class='value'>";
    html += String(speedKmh, 1) + " km/h</div></div>";
    html += "<div class='metric'><div class='label'>Speed (mph)</div><div class='value'>";
    html += String(speedMph, 1) + " mph</div></div>";
    html += "<div class='metric'><div class='label'>Course</div><div class='value'>";
    html += String(rmc.courseDeg, 1) + "&deg;</div></div></div>";
    
    // Time card
    if (strlen(rmc.timeUTC) > 0 || strlen(rmc.date) > 0) {
      html += "<div class='card'><h2>Time (UTC)</h2>";
      html += "<div class='metric'><div class='label'>Time</div><div class='value mono'>";
      html += String(rmc.timeUTC) + "</div></div>";
      html += "<div class='metric'><div class='label'>Date</div><div class='value mono'>";
      html += String(rmc.date) + "</div></div></div>";
    }
    
    // Connection info
    html += "<div class='card'><h2>Network</h2>";
    html += "<div class='metric'><div class='label'>Mode</div><div class='value'>";
    html += (WiFi.getMode() == WIFI_AP) ? "SoftAP" : "Station";
    html += "</div></div>";
    html += "<div class='metric'><div class='label'>IP Address</div><div class='value mono'>";
    html += (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    html += "</div></div>";
    if (WiFi.getMode() == WIFI_STA) {
      html += "<div class='metric'><div class='label'>RSSI</div><div class='value'>";
      html += String(WiFi.RSSI()) + " dBm</div></div>";
    }
    html += "<div style='margin-top:10px;color:#777;font-size:0.9em;'>Telnet: port 2323</div>";
    html += "</div>";
    
    html += "<div style='text-align:center;margin-top:20px;color:#999;font-size:0.85em;'>";
    html += "Auto-refresh every 2s | NEO-7M on ESP32-S3</div>";
    html += "</body></html>";
    
    httpServer.send(200, "text/html", html);
  });
  
  httpServer.on("/json", []() {
    uint32_t uptime = (millis() - startMillis) / 1000;
    String json = "{";
    json += "\"uptime\":" + String(uptime) + ",";
    json += "\"fix\":" + String(gga.validFix ? "true" : "false") + ",";
    json += "\"fixQuality\":" + String(gga.fixQuality) + ",";
    json += "\"satsUsed\":" + String(gga.sats) + ",";
    json += "\"satsInView\":" + String(gsv.inView) + ",";
    json += "\"lat\":" + String(gga.lat, 6) + ",";
    json += "\"lon\":" + String(gga.lon, 6) + ",";
    json += "\"altitude\":" + String(gga.altitude, 1) + ",";
    json += "\"speedKnots\":" + String(rmc.speedKnots, 2) + ",";
    json += "\"speedKmh\":" + String(rmc.speedKnots * 1.852f, 2) + ",";
    json += "\"course\":" + String(rmc.courseDeg, 1) + ",";
    json += "\"rmcActive\":" + String(rmc.active ? "true" : "false") + ",";
    json += "\"timeUTC\":\"" + String(rmc.timeUTC) + "\",";
    json += "\"date\":\"" + String(rmc.date) + "\"";
    json += "}";
    httpServer.send(200, "application/json", json);
  });
  
  httpServer.begin();
  Serial.println("[WiFi] HTTP server listening on port 80");
}

static void wifiPoll() {
  if (!telnetClient || !telnetClient.connected()) {
    WiFiClient newClient = telnetServer.available();
    if (newClient) {
      if (telnetClient) telnetClient.stop();
      telnetClient = newClient;
      telnetClient.print("ESP32 GPS telnet ready\r\n");
    }
  }
}

static inline void logWrite(char c) {
  Serial.write(c);
  if (telnetClient && telnetClient.connected()) telnetClient.write((uint8_t)c);
}

static void logPrint(const char* s) {
  Serial.print(s);
  if (telnetClient && telnetClient.connected()) telnetClient.print(s);
}

static void logPrintf(const char* fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  logPrint(buf);
}

extern "C" void gps_test_setup();
extern "C" void gps_test_loop();

void gps_test_setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[GPS] Starting...");
  GPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[GPS] UART1 started at 9600 baud");
  startMillis = millis();
  wifiStart();
}

void gps_test_loop() {
  while (GPS.available()) {
    char c = GPS.read();
    logWrite(c);
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[linePos] = '\0';
      if (linePos > 6 && lineBuf[0] == '$') {
        char* sentence = lineBuf + 1;
        if (strncmp(sentence+2, "GGA", 3) == 0) {
          parseGGA(sentence);
        } else if (strncmp(sentence+2, "RMC", 3) == 0) {
          parseRMC(sentence);
        } else if (strncmp(sentence+2, "GSV", 3) == 0) {
          parseGSV(sentence);
        }
      }
      linePos = 0;
    } else if (linePos < (int)sizeof(lineBuf) - 1) {
      lineBuf[linePos++] = c;
    } else {
      linePos = 0; // overflow reset
    }
  }
  uint32_t now = millis();
  if (now - lastStatus > 3000) {
    lastStatus = now;
    logPrint("[GPS] Waiting for sentences...\n");
  }
  if (now - lastSummary > 5000) {
    lastSummary = now;
    float speedKmh = rmc.speedKnots * 1.852f;
    logPrintf("[SUM] t=%lus fixQ=%d satsUsed=%d satsView=%d RMC=%s spd=%.1fkn(%.1fkm/h) alt=%.1fm lat=%.5f lon=%.5f\n",
              (unsigned long)((now - startMillis)/1000UL),
              gga.fixQuality,
              gga.sats,
              gsv.inView,
              rmc.active?"A":"V",
              rmc.speedKnots,
              speedKmh,
              gga.altitude,
              gga.lat,
              gga.lon);
  }
  wifiPoll();
  httpServer.handleClient();
}

void setup() { gps_test_setup(); }
void loop() { gps_test_loop(); }
