#include "gps_module.h"
#include <HardwareSerial.h>
#include <string.h>

static HardwareSerial GPS(1);

// Internal state mirrors GPSData
static GPSData g_data = {0};

// Buffers
static char lineBuf[128];
static int linePos = 0;

// Tokenizer helper
static int tokenize(char* s, const char* tokens[], int maxTok) {
  int count = 0; char* p = s;
  while (p && *p && count < maxTok) {
    tokens[count++] = p;
    char* comma = strchr(p, ',');
    if (!comma) break;
    *comma = '\0';
    p = comma + 1;
  }
  return count;
}

static float nmeaCoordToDeg(const char* fld) {
  if (!fld || !*fld) return 0.0f;
  float v = atof(fld);
  int deg = (int)(v / 100.0f);
  float minutes = v - deg * 100.0f;
  return deg + minutes / 60.0f;
}

static void parseGGA(char* sentence) {
  // GGA: time,lat,N,lon,E,fix,sats,hdop,alt,M,geoid,...
  const char* t[20] = {0};
  int n = tokenize(sentence, t, 20);
  if (n < 10) return;
  g_data.fixQuality = atoi(t[6]);
  g_data.satsUsed = atoi(t[7]);
  g_data.altitude = atof(t[9]);
  g_data.lat = nmeaCoordToDeg(t[2]);
  if (t[3] && t[3][0] == 'S') g_data.lat = -g_data.lat;
  g_data.lon = nmeaCoordToDeg(t[4]);
  if (t[5] && t[5][0] == 'W') g_data.lon = -g_data.lon;
  g_data.validFix = (g_data.fixQuality > 0);
}

static void parseRMC(char* sentence) {
  // RMC: time,status,lat,N,lon,E,sog,course,date,...
  const char* t[20] = {0};
  int n = tokenize(sentence, t, 20);
  if (n < 10) return;
  if (t[1]) strncpy(g_data.timeUTC, t[1], sizeof(g_data.timeUTC)-1);
  g_data.timeUTC[sizeof(g_data.timeUTC)-1] = '\0';
  bool active = (t[2] && t[2][0] == 'A');
  g_data.speedKnots = t[7] ? atof(t[7]) : 0.0f;
  g_data.courseDeg = t[8] ? atof(t[8]) : 0.0f;
  if (t[9]) strncpy(g_data.date, t[9], sizeof(g_data.date)-1);
  g_data.date[sizeof(g_data.date)-1] = '\0';
  if (!g_data.validFix && active) {
    g_data.lat = nmeaCoordToDeg(t[3]);
    if (t[4] && t[4][0] == 'S') g_data.lat = -g_data.lat;
    g_data.lon = nmeaCoordToDeg(t[5]);
    if (t[6] && t[6][0] == 'W') g_data.lon = -g_data.lon;
  }
  if (active) g_data.validFix = true; // active RMC implies usable solution
}

static void parseGSV(char* sentence) {
  // GSV: totalMsgs,msgIndex,svInView,...
  const char* t[30] = {0};
  int n = tokenize(sentence, t, 30);
  if (n < 4) return;
  g_data.satsInView = atoi(t[3]);
}

void gps_init(int rxPin, int txPin, uint32_t baud) {
  memset(&g_data, 0, sizeof(g_data));
  GPS.begin(baud, SERIAL_8N1, rxPin, txPin);
}

void gps_poll(void) {
  while (GPS.available()) {
    char c = GPS.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[linePos] = '\0';
      if (linePos > 6 && lineBuf[0] == '$') {
        char* s = lineBuf + 1; // skip '$'
        // match by type after talker ID (chars 2-4 of full)
        if (strncmp(s+2, "GGA", 3) == 0) {
          parseGGA(s);
        } else if (strncmp(s+2, "RMC", 3) == 0) {
          parseRMC(s);
        } else if (strncmp(s+2, "GSV", 3) == 0) {
          parseGSV(s);
        }
      }
      linePos = 0;
    } else if (linePos < (int)sizeof(lineBuf)-1) {
      lineBuf[linePos++] = c;
    } else {
      linePos = 0; // overflow reset
    }
  }
  
  // Convert knots to km/h
  g_data.speedKmh = g_data.speedKnots * 1.852f;
  
  // Apply deadband filter to suppress GPS drift when stationary
  // GPS modules typically have ~0.1-0.5 knots of noise when stationary
  // This translates to ~0.2-0.9 km/h, but can reach 1.5 km/h in some conditions
  // Set threshold at 1.8 km/h (just under walking pace ~2-3 km/h)
  const float SPEED_DEADBAND_KMH = 1.8f;
  if (g_data.speedKmh < SPEED_DEADBAND_KMH) {
    g_data.speedKmh = 0.0f;
    g_data.speedKnots = 0.0f;
  }
}

void gps_get_data(GPSData* out) {
  if (!out) return;
  *out = g_data; // shallow copy
}
