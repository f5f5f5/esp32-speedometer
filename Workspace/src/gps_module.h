#pragma once
#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal snapshot of GPS data for UI/app integration
typedef struct GPSData {
  bool validFix;      // true if fixQuality > 0 or RMC active
  int  fixQuality;    // 0=no fix, 1=GPS, 2=DGPS, >2 augmentation
  int  satsUsed;      // satellites used in solution (GGA)
  int  satsInView;    // total satellites visible (GSV)
  float lat;          // degrees
  float lon;          // degrees
  float altitude;     // meters (MSL) from GGA
  float speedKnots;   // from RMC
  float speedKmh;     // derived
  float courseDeg;    // from RMC
  char  date[8];      // DDMMYY, 0-terminated if available
  char  timeUTC[10];  // HHMMSS.sss, 0-terminated if available
} GPSData;

// Initialize the GPS on given UART1 pins. Typical: RX=16 (ESP reads), TX=15
void gps_init(int rxPin, int txPin, uint32_t baud);

// Poll the UART, parse incoming NMEA, and update the internal snapshot
void gps_poll(void);

// Copy the latest snapshot into 'out'. Thread-safe for single-core cooperative use.
void gps_get_data(GPSData* out);

#ifdef __cplusplus
}
#endif
