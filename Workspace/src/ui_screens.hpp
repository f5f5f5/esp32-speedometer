#pragma once
#include "display_config.hpp" // defines LGFX
#include "gps_module.h"
#include "battery.hpp"

struct AppData {
  GPSData gps;
  int batteryPercent = 0;
  bool darkMode = true;
  float maxSpeedKmh = 220.0f;
};

class BaseScreen {
public:
  virtual ~BaseScreen() {}
  virtual const char* name() const = 0;
  virtual void render(LGFX &gfx, const AppData &data) = 0;
};

class SpeedScreen : public BaseScreen {
public:
  const char* name() const override { return "Speed"; }
  void render(LGFX &gfx, const AppData &d) override;
};

class MetricsScreen : public BaseScreen {
public:
  const char* name() const override { return "Metrics"; }
  void render(LGFX &gfx, const AppData &d) override;
};

class SettingsScreen : public BaseScreen {
public:
  const char* name() const override { return "Settings"; }
  void render(LGFX &gfx, const AppData &d) override;
};

struct ScreenManager {
  enum ScreenID { SPEED, METRICS, SETTINGS };
  ScreenID current = SPEED;
  SpeedScreen speed;
  MetricsScreen metrics;
  SettingsScreen settings;

  BaseScreen* active() {
    switch(current) {
      case SPEED: return &speed;
      case METRICS: return &metrics;
      case SETTINGS: return &settings;
    }
    return &speed;
  }
  void next() {
    current = (ScreenID)(((int)current + 1) % 3);
  }
  void prev() {
    current = (ScreenID)(((int)current + 2) % 3);
  }
};
