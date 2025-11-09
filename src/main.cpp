#include <Arduino.h>
#include <TinyGPS++.h>
#include <Adafruit_GFX.h>

// GPS module configuration
TinyGPSPlus gps;
#define GPS_RX_PIN 18  // Adjust according to your wiring
#define GPS_TX_PIN 17  // Adjust according to your wiring

// LCD Display configuration
// TODO: Add display initialization

void setup() {
    // Initialize Serial for debugging
    Serial.begin(115200);
    
    // Initialize GPS Serial
    Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    // Initialize Display
    // TODO: Add display initialization code
    
    Serial.println("ESP32 Speedometer initialized");
}

void loop() {
    // Read GPS data
    while (Serial2.available() > 0) {
        if (gps.encode(Serial2.read())) {
            if (gps.speed.isUpdated()) {
                float speed = gps.speed.kmph();
                // TODO: Update display with speed
                Serial.print("Speed: ");
                Serial.print(speed);
                Serial.println(" km/h");
            }
        }
    }
    
    // Check GPS module connection
    if (millis() > 5000 && gps.charsProcessed() < 10) {
        Serial.println("No GPS data received. Check wiring.");
        delay(5000);
    }
}