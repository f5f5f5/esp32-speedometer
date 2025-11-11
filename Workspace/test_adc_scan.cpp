// Temporary ADC scanner - paste this into setup() to find battery pin
// ESP32-S3 ADC1 pins: 1-10
// ESP32-S3 ADC2 pins: 11-20 (but avoid ADC2, it conflicts with WiFi)

void scanADCPins() {
  Serial.println("\n=== Scanning ADC Pins for Battery ===");
  Serial.println("Looking for ~2050mV (4.1V battery ÷ 2.0 divider)");
  Serial.println("Pin | ADC Raw | Millivolts | Voltage");
  Serial.println("----|---------|------------|--------");
  
  // ADC1 channels on ESP32-S3
  const int adc_pins[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  
  for (int pin : adc_pins) {
    pinMode(pin, INPUT);
    analogSetPinAttenuation(pin, ADC_11db);
    delay(10);
    
    // Take average of 10 readings
    uint32_t sum_mv = 0;
    uint32_t sum_raw = 0;
    for (int i = 0; i < 10; i++) {
      sum_mv += analogReadMilliVolts(pin);
      sum_raw += analogRead(pin);
      delay(5);
    }
    uint32_t avg_mv = sum_mv / 10;
    uint32_t avg_raw = sum_raw / 10;
    float voltage = (avg_mv / 1000.0f) * 2.0f; // Assume 2x divider
    
    Serial.printf(" %2d | %4d    | %4d       | %.3fV", 
                  pin, avg_raw, avg_mv, voltage);
    
    // Highlight likely battery pins (expecting ~2.05V at ADC for 4.1V battery)
    if (avg_mv >= 1900 && avg_mv <= 2200) {
      Serial.printf(" <-- LIKELY BATTERY! (%.2fV actual)", voltage);
    }
    Serial.println();
  }
  Serial.println("=====================================\n");
}
