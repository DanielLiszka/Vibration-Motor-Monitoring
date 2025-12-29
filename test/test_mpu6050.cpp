#include <Arduino.h>
#include "MPU6050Driver.h"
#include "Config.h"

MPU6050Driver sensor;

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║     MPU6050 Sensor Test Utility       ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    Serial.println("1. Testing sensor connection...");
    if (!sensor.begin()) {
        Serial.println("   FAIL: " + sensor.getLastError());
        Serial.println("\nTroubleshooting:");
        Serial.println("  - Check wiring (SDA->21, SCL->22)");
        Serial.println("  - Verify 3.3V power");
        Serial.println("  - Check I2C address (default 0x68)");
        while(1) { delay(1000); }
    }
    Serial.println("   OK: Sensor connected\n");

    Serial.println("2. Sensor Information:");
    Serial.printf("   Temperature: %.2f °C\n", sensor.getTemperature());
    Serial.println();

    Serial.println("3. Starting continuous data stream...");
    Serial.println("   Press RESET to stop\n");
    Serial.println("Time(ms)\tAccel_X\tAccel_Y\tAccel_Z\tMagnitude\tTemp(°C)");
    Serial.println("─────────────────────────────────────────────────────────────────");
}

void loop() {
    static uint32_t lastPrint = 0;
    static uint32_t sampleCount = 0;

    if (millis() - lastPrint < 100) {
        return;
    }
    lastPrint = millis();

    AccelData accel;
    if (sensor.readAcceleration(accel)) {
        float magnitude = sqrt(accel.x * accel.x +
                              accel.y * accel.y +
                              accel.z * accel.z);
        float temp = sensor.getTemperature();

        Serial.printf("%lu\t%.3f\t%.3f\t%.3f\t%.3f\t\t%.1f\n",
                     accel.timestamp,
                     accel.x, accel.y, accel.z,
                     magnitude, temp);

        sampleCount++;

        if (sampleCount % 100 == 0) {
            Serial.println("\n─────────────────────────────────────────────────────────────────");
            Serial.printf("Samples collected: %lu\n", sampleCount);
            Serial.printf("Sampling rate: ~10 Hz (display), 100 Hz (actual)\n");
            Serial.println("─────────────────────────────────────────────────────────────────\n");
        }
    } else {
        Serial.println("Read error!");
    }
}
