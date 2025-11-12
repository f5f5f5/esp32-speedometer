// Reusable lightweight NMEA GPS service for ESP32 (Arduino)
// Parses GGA (fix/sats/alt/pos), RMC (speed/course/time/date), GSV (sats in view)

#pragma once
#include <Arduino.h>

class GPSService {
public:
  struct GGAInfo {
    bool validFix = false;
    int fixQuality = 0;   // 0=no fix, 1=GPS, 2=DGPS, >2 augmented
    int satsUsed = 0;     // satellites used in solution
    float lat = 0;        // degrees
    float lon = 0;        // degrees
    float altitude = 0;   // meters MSL
  };

  struct RMCInfo {
    bool active = false;   // A=active, V=void
    float speedKnots = 0;  // speed over ground
    float courseDeg = 0;   // course over ground
    char date[8] = {0};    // DDMMYY
    char timeUTC[10] = {0}; // HHMMSS.sss
  };

  struct GSVInfo {
    int inView = 0;        // total satellites in view
    int msgCount = 0;      // message count in cycle
    int currentMsg = 0;    // current message index
    uint32_t lastUpdate = 0;
  };

  // Initialize a HardwareSerial port for GPS (e.g., port 1)
  void begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud = 9600);

  // Read bytes from the serial stream, parse any completed NMEA sentences
  void updateFromStream(Stream &s);

  // Accessors
  const GGAInfo& gga() const { return _gga; }
  const RMCInfo& rmc() const { return _rmc; }
  const GSVInfo& gsv() const { return _gsv; }

  // Convenience getters
  bool hasFix() const { return _gga.validFix || _rmc.active; }
  float latitude() const { return _gga.lat; }
  float longitude() const { return _gga.lon; }
  float altitude() const { return _gga.altitude; }
  int satsUsed() const { return _gga.satsUsed; }
  int satsInView() const { return _gsv.inView; }
  float speedKnots() const { return _rmc.speedKnots; }
  float speedKmh() const { return _rmc.speedKnots * 1.852f; }
  float courseDeg() const { return _rmc.courseDeg; }

private:
  // Helpers
  static float nmeaCoordToDeg(const char* fld, bool isLat);
  static int tokenize(char* s, const char* tokens[], int maxTok);

  void parseSentence(char *sentenceNoDollar);
  void parseGGA(char* s);
  void parseRMC(char* s);
  void parseGSV(char* s);

  HardwareSerial* _serial = nullptr;
  char _lineBuf[128];
  int _linePos = 0;

  GGAInfo _gga;
  RMCInfo _rmc;
  GSVInfo _gsv;
};
