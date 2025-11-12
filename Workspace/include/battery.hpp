// Battery management for ESP32-S3-Touch-LCD-1.28
#pragma once
#include <Arduino.h>

// Hardware configuration (can be overridden via build_flags)
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN 1        // GPIO for battery voltage ADC (override with -DBATTERY_ADC_PIN=<pin>)
#endif

// Optional USB power detect pin (define with -DUSB_POWER_PIN=<pin> if wired)
// Optional charge-status pin from charger IC (active level depends on chip)
// Define -DCHG_STATUS_PIN=<pin> and optionally -DCHG_ACTIVE_LOW=1 if active-low.

#ifndef BATTERY_SAMPLES
#define BATTERY_SAMPLES 10       // Number of samples for averaging
#endif

// Battery voltage thresholds (for 3.7V Li-Po)
#define VBAT_FULL 4.20f          // Fully charged
#define VBAT_NOMINAL 3.70f       // Nominal voltage
#define VBAT_LOW 3.40f           // Low battery warning
#define VBAT_CRITICAL 3.20f      // Critical - shutdown soon
#define VBAT_EMPTY 3.15f         // Empty (calibrated from actual discharge test)

// ADC configuration
#define ADC_VREF 3.3f            // ESP32-S3 ADC reference voltage
#define ADC_RESOLUTION 4095.0f   // 12-bit ADC
// Voltage divider ratio: Vbat = Vadc * VOLTAGE_DIVIDER
#ifndef VOLTAGE_DIVIDER
#define VOLTAGE_DIVIDER 2.0f     // Override with -DVOLTAGE_DIVIDER=<float>
#endif

// Optional global ADC calibration scale (to correct analogReadMilliVolts bias)
// Final battery voltage = Vadc * VOLTAGE_DIVIDER * ADC_SCALE
#ifndef ADC_SCALE
#define ADC_SCALE 1.0f            // Override with -DADC_SCALE=<float> (e.g., 0.915)
#endif

// Low-battery threshold by percentage
#ifndef LOW_BAT_PERCENT
#define LOW_BAT_PERCENT 5
#endif

// Fallback USB detection voltage threshold (when no USB/CHG pins are defined)
#ifndef USB_VOLT_THRESHOLD
#define USB_VOLT_THRESHOLD 4.02f   // consider USB present when filtered Vbat >= ~4.02V
#endif

enum class BatteryState {
  UNKNOWN,
  CHARGING,
  DISCHARGING,
  FULL,
  USB_POWERED
};

class Battery {
private:
  float voltage;
  float voltageFiltered;
  int percentage;
  BatteryState state;
  unsigned long lastUpdate;
  bool lowBatteryWarning;
  uint32_t warningFlashTime;
  bool lowWarnLatched;
  int usbTrendScore;  // hysteresis for USB detect without pin
  float lastVoltageFiltered;
  // Last raw millivolt sample from ADC (pre-divider), for absence heuristics
  uint32_t mvLastSample;
  int lastRawADC; // last raw ADC reading (0-4095)
  // Heuristic to determine if a battery is likely absent
  int absentScore;
  bool batteryAbsent;
  // Stability tracking for high-voltage (USB-only) absence heuristic
  static const int RECENT_SAMPLES = 30; // ~30s window at 1Hz
  float recentFiltered[RECENT_SAMPLES];
  int recentIndex;
  int recentCount;
  
  // Moving average for stable readings
  float voltageBuffer[BATTERY_SAMPLES];
  int bufferIndex;
  
  float readBatteryVoltage() {
    // Prefer calibrated millivolt read when available (Arduino ESP32 core)
    #if defined(ARDUINO_ARCH_ESP32)
      uint32_t mv = analogReadMilliVolts(BATTERY_ADC_PIN);
      int rawADC = analogRead(BATTERY_ADC_PIN);
      lastRawADC = rawADC;
      float adcVoltage = mv / 1000.0f;
      float vBat = adcVoltage * VOLTAGE_DIVIDER * ADC_SCALE;
      mvLastSample = mv;
      
      #ifdef DEBUG_BATTERY
      static uint32_t lastRawPrint = 0;
      if (millis() - lastRawPrint > 5000) {
        lastRawPrint = millis();
        Serial.printf("[BAT-RAW] Pin=%d, ADC=%d, mV=%u, Vadc=%.3f, Vbat=%.3f (div=%.1fx, cal=%.3f)\n", 
                      BATTERY_ADC_PIN, rawADC, mv, adcVoltage, vBat, VOLTAGE_DIVIDER, ADC_SCALE);
      }
      #endif
      
      // Reject only clearly invalid readings (true zeros); allow low voltages for absence detection
      if (mv == 0 || rawADC == 0) {
        // Return last known good voltage instead of corrupting the filter
        return (voltageFiltered > 0) ? voltageFiltered : 3.7f;
      }
      return vBat;
    #else
      int rawValue = analogRead(BATTERY_ADC_PIN);
      float adcVoltage = (rawValue / ADC_RESOLUTION) * ADC_VREF;
      float vBat = adcVoltage * VOLTAGE_DIVIDER * ADC_SCALE;
      
      // Reject only clearly invalid readings
      if (rawValue == 0) {
        return (voltageFiltered > 0) ? voltageFiltered : 3.7f;
      }
      return vBat;
    #endif
  }
  
  float getAverageVoltage() {
    // Oversample quickly to reduce instantaneous noise
    const int oversample = 8;
    float acc = 0;
    for (int i = 0; i < oversample; ++i) {
      acc += readBatteryVoltage();
      delayMicroseconds(500);
    }
    float sample = acc / oversample;

    voltageBuffer[bufferIndex] = sample;
    bufferIndex = (bufferIndex + 1) % BATTERY_SAMPLES;
    
    float sum = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      sum += voltageBuffer[i];
    }
    return sum / BATTERY_SAMPLES;
  }
  
  int voltageToPercentage(float v) {
    // Piecewise OCV curve for typical LiPo at rest (approximate)
    // Voltage values in volts; percentages in %
    // 0% calibrated from actual discharge test (3.15V actual = device shutdown point)
    static const float ocvV[] = {
      3.15f, 3.50f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f,
      3.82f, 3.84f, 3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f,
      4.11f, 4.15f, 4.20f
    };
    static const int ocvP[] = {
      0,   0,   5,   10,  15,  20,  25,  30,  35,  40,
      45,  50,  55,  60,  65,  70,  75,  80,  85,
      90,  95,  100
    };
    const int N = sizeof(ocvV)/sizeof(ocvV[0]);
    if (v <= ocvV[0]) return 0;
    if (v >= ocvV[N-1]) return 100;
    for (int i = 1; i < N; ++i) {
      if (v <= ocvV[i]) {
        float t = (v - ocvV[i-1]) / (ocvV[i] - ocvV[i-1]);
        int p = (int)roundf(ocvP[i-1] + t * (ocvP[i] - ocvP[i-1]));
        int result = constrain(p, 0, 100);
        #ifdef DEBUG_BATTERY
        Serial.printf("[BAT-OCV] V=%.3f matched segment %d: %.3f-%.3f -> %d%%-%d%% = %d%%\n", 
                      v, i, ocvV[i-1], ocvV[i], ocvP[i-1], ocvP[i], result);
        #endif
        return result;
      }
    }
    return 0;
  }
  
public:
  Battery() : voltage(0), voltageFiltered(0), percentage(0), state(BatteryState::UNKNOWN), 
              lastUpdate(0), lowBatteryWarning(false), warningFlashTime(0), lowWarnLatched(false),
              usbTrendScore(0), lastVoltageFiltered(0), mvLastSample(0), lastRawADC(0), absentScore(0), batteryAbsent(false), bufferIndex(0) {
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      voltageBuffer[i] = 0;
    }
    recentIndex = 0;
    recentCount = 0;
    for (int i = 0; i < RECENT_SAMPLES; ++i) recentFiltered[i] = 0;
  }
  
  void begin() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    #ifdef USB_POWER_PIN
      pinMode(USB_POWER_PIN, INPUT);
    #endif
    #ifdef CHG_STATUS_PIN
      pinMode(CHG_STATUS_PIN, INPUT);
    #endif
    #if defined(ARDUINO_ARCH_ESP32)
      // Set attenuation once (11dB gives range ~3.3V); measured through divider.
      analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    #endif
    
    // Initialize buffer and filter with current readings
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      voltageBuffer[i] = readBatteryVoltage();
      delay(10);
    }
    voltage = getAverageVoltage();
    voltageFiltered = voltage;
    lastVoltageFiltered = voltageFiltered;
    mvLastSample = 0;
    absentScore = 0;
    batteryAbsent = false;
    recentIndex = 0;
    recentCount = 0;
    for (int i = 0; i < RECENT_SAMPLES; ++i) recentFiltered[i] = voltageFiltered;
    update();
  }
  
  void update() {
    unsigned long now = millis();
    // Update every 100ms: fast cadence primarily improves USB detect responsiveness; 
    // percentage itself is smoothed to ~1Hz via averaging/EMA
    if (now - lastUpdate < 100) return;
    
    lastUpdate = now;
    float oldVoltage = voltageFiltered;
    float vAvg = getAverageVoltage();
    // Exponential moving average to stabilize
    const float alpha = 0.2f; // higher = more responsive
    voltageFiltered = (alpha * vAvg) + ((1.0f - alpha) * voltageFiltered);
    if ((vAvg - voltageFiltered) > 1.0f) { // fast catch-up if way off
      #ifdef DEBUG_BATTERY
      Serial.printf("[BAT-FILTER] Catch-up (vAvg=%.3f prevFilt=%.3f)\n", vAvg, oldVoltage);
      #endif
      voltageFiltered = vAvg;
    }
    voltage = voltageFiltered; // expose filtered as public voltage
    percentage = voltageToPercentage(voltageFiltered);
    
    #ifdef DEBUG_BATTERY
    Serial.printf("[BAT] Raw avg: %.3fV, Filtered: %.3fV, Percent: %d%%\n", vAvg, voltageFiltered, percentage);
    #endif
    
    // Legacy low-voltage absence heuristic (keep for fallback)
    // Adjusted threshold to 3.10V (below new VBAT_EMPTY of 3.15V)
    if (voltageFiltered < 3.10f && mvLastSample > 0 && mvLastSample < 1200) {
      absentScore = min(absentScore + 1, 5);
    } else {
      absentScore = max(absentScore - 1, -5);
    }
    bool legacyAbsent = (absentScore >= 3);

    // High-voltage stability absence heuristic for this board:
    // When running ONLY from USB with battery physically removed, the divider pin is held at a
    // relatively fixed voltage by the charging IC producing a very stable high reading (~4.1-4.2V scaled).
    // We detect: (a) high filtered voltage (>= USB_VOLT_THRESHOLD), (b) high percentage (>=90),
    // (c) not actively charging, (d) extremely low variance over recent window (<2mV spread).
    // Maintain a rolling window of recent filtered voltages to check stability.
    recentFiltered[recentIndex] = voltageFiltered;
    recentIndex = (recentIndex + 1) % RECENT_SAMPLES;
    if (recentCount < RECENT_SAMPLES) recentCount++;
    float vMin = 10.0f, vMax = 0.0f;
    for (int i = 0; i < recentCount; ++i) {
      float v = recentFiltered[i];
      if (v < vMin) vMin = v;
      if (v > vMax) vMax = v;
    }
    float spread = (recentCount > 0) ? (vMax - vMin) : 0.0f;

  // Choose absence if stability criteria met for full window duration
  // Magic numbers factored into named constants for clarity and tuning
  static constexpr int   USB_DETECT_MIN_PERCENT = 90;      // >= 90% implies near-full
  static constexpr float USB_STABILITY_SPREAD_V = 0.002f;  // < 2mV spread across window
  static constexpr int   STABILITY_MIN_SAMPLES  = (RECENT_SAMPLES * 2) / 3; // ~20s at 1Hz
  bool stabilityReady = (recentCount >= STABILITY_MIN_SAMPLES);
  bool highVoltageStable = (voltageFiltered >= USB_VOLT_THRESHOLD && percentage >= USB_DETECT_MIN_PERCENT && spread < USB_STABILITY_SPREAD_V && stabilityReady);

    // Decide batteryAbsent: prefer stability heuristic when it triggers, else legacy low-voltage heuristic
    if (highVoltageStable) {
      batteryAbsent = true;
    } else {
      batteryAbsent = legacyAbsent;
    }

    // Detect charging state
    #ifdef USB_POWER_PIN
      bool usbPowered = digitalRead(USB_POWER_PIN) == HIGH;
    #else
      // Fallback: detect USB by voltage/trend with hysteresis
      // USB power keeps voltage high and stable (>= 4.02V for extended period)
      // Battery-only will trend downward over time
      float dv = voltageFiltered - lastVoltageFiltered;
      // Adjust trend score: +1 on modest rise, -1 on stronger fall
      if (dv > 0.005f) usbTrendScore = min(usbTrendScore + 1, 5);   // rising ~5mV+ per sec
      if (dv < -0.010f) usbTrendScore = max(usbTrendScore - 1, -5); // falling ~10mV+ per sec
      
      // USB powered if: high voltage AND (stable/rising trend OR very high voltage OR battery absent)
      bool usbPowered = (voltageFiltered >= USB_VOLT_THRESHOLD) && 
                        (usbTrendScore >= 0 || voltageFiltered >= 4.15f || batteryAbsent);
      #ifdef DEBUG_BATTERY
      Serial.printf("[BAT-USB] V=%.3f dV=%.3f thr=%.3f trend=%d mv=%umV absent=%d(legacy=%d spread=%.4f cnt=%d) -> usb=%d\n",
                    voltageFiltered, dv, USB_VOLT_THRESHOLD, usbTrendScore, mvLastSample, (int)batteryAbsent, legacyAbsent, spread, recentCount, usbPowered);
      #endif
    #endif

    bool chgPinCharging = false;
    #ifdef CHG_STATUS_PIN
      int chgLevel = digitalRead(CHG_STATUS_PIN);
      #ifdef CHG_ACTIVE_LOW
        chgPinCharging = (chgLevel == LOW);
      #else
        chgPinCharging = (chgLevel == HIGH);
      #endif
    #endif
    
    if (usbPowered) {
      // When USB is connected, voltage reads high regardless of actual battery charge
      // We cannot reliably detect charging vs full from voltage alone on this hardware
      // Only show CHARGING if we have a hardware charge status pin
      
      if (batteryAbsent) {
        state = BatteryState::USB_POWERED;  // No battery connected
      } else if (chgPinCharging) {
        state = BatteryState::CHARGING;  // Hardware pin indicates active charging
      } else {
        // Default to USB_POWERED (could be charging, full, or maintaining)
        // Without hardware pin, we can't distinguish
        state = BatteryState::USB_POWERED;
      }
    } else {
      state = BatteryState::DISCHARGING;
    }

#ifdef DIAG_ADC
    static int diagCount = 0;
    if (diagCount < 15) {
      // Expected full-battery mv ~ (voltageFiltered / (VOLTAGE_DIVIDER * ADC_SCALE))*1000
      float expectedMvFromFiltered = voltageFiltered / (VOLTAGE_DIVIDER * ADC_SCALE) * 1000.0f;
      Serial.printf("[ADC-DIAG] rawADC=%d mv=%u filt=%.3fV pct=%d%% expMv=%.1f spread=%.4f usb=%d absent=%d state=%d\n",
                    lastRawADC, mvLastSample, voltageFiltered, percentage, expectedMvFromFiltered, spread, (int)usbPowered, (int)batteryAbsent, (int)state);
      diagCount++;
    }
#endif
    
    // Low battery warning with hysteresis (enter <=LOW_BAT_PERCENT, clear >=LOW_BAT_PERCENT+2)
    const int LOW_BAT_HYST = 2;
    if (state == BatteryState::DISCHARGING) {
      if (!lowWarnLatched && percentage <= LOW_BAT_PERCENT) lowWarnLatched = true;
      if (lowWarnLatched && percentage >= (LOW_BAT_PERCENT + LOW_BAT_HYST)) lowWarnLatched = false;
    } else {
      // No low-bat while on USB/charging
      lowWarnLatched = false;
    }
    lowBatteryWarning = lowWarnLatched;

    lastVoltageFiltered = voltageFiltered;
  }
  
  float getVoltage() const { return voltage; }
  int getPercentage() const { return percentage; }
  BatteryState getState() const { return state; }
  bool isLowBattery() const { return lowBatteryWarning; }
  bool isCharging() const { return state == BatteryState::CHARGING; }
  bool isUSBPowered() const { return state == BatteryState::USB_POWERED || state == BatteryState::CHARGING || state == BatteryState::FULL; }
  bool isBatteryAbsent() const { return batteryAbsent; }
  
  // Diagnostic accessors (for on-screen display when serial unavailable)
  uint32_t getRawMillivolts() const { return mvLastSample; }
  int getRawADC() const { return lastRawADC; }
  
  // Flash warning every 2 seconds
  bool shouldFlashWarning() {
    if (!lowBatteryWarning) return false;
    uint32_t now = millis();
    if (now - warningFlashTime > 2000) {
      warningFlashTime = now;
      return true;
    }
    return false;
  }
  
  const char* getStateString() const {
    switch (state) {
      case BatteryState::CHARGING: return "Charging";
      case BatteryState::DISCHARGING: return "Discharging";
      case BatteryState::FULL: return "Full";
      case BatteryState::USB_POWERED: return "USB Power";
      default: return "Unknown";
    }
  }
};
