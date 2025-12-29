#ifndef SELF_DIAGNOSTIC_H
#define SELF_DIAGNOSTIC_H

#include <Arduino.h>
#include "Config.h"
#include "MPU6050Driver.h"

#define DIAG_CHECK_INTERVAL_MS 60000
#define DIAG_SENSOR_TIMEOUT_MS 1000
#define DIAG_MIN_HEAP_BYTES 10000
#define DIAG_MAX_TEMP_C 85.0
#define DIAG_MIN_RSSI_DBM -80

enum DiagnosticStatus {
    DIAG_OK = 0,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_CRITICAL
};

struct DiagnosticResult {
    DiagnosticStatus overall;

    bool sensorOK;
    bool memoryOK;
    bool wifiOK;
    bool storageOK;
    bool temperatureOK;

    String sensorMessage;
    String memoryMessage;
    String wifiMessage;
    String storageMessage;
    String temperatureMessage;

    uint32_t freeHeap;
    int32_t wifiRSSI;
    float sensorTemp;
    uint32_t uptime;

    uint32_t timestamp;

    void reset() {
        overall = DIAG_OK;
        sensorOK = memoryOK = wifiOK = storageOK = temperatureOK = true;
        sensorMessage = memoryMessage = wifiMessage = "";
        storageMessage = temperatureMessage = "";
        freeHeap = 0;
        wifiRSSI = 0;
        sensorTemp = 0;
        uptime = 0;
        timestamp = millis();
    }

    void print() const {
        Serial.println("\n==============================");
        Serial.println("       SYSTEM DIAGNOSTICS");
        Serial.println("==============================");

        Serial.printf("Overall Status: %s\n", getStatusName(overall));
        Serial.printf("Uptime: %lu seconds\n\n", uptime / 1000);

        Serial.printf("Sensor:      %s  %s\n", sensorOK ? "OK" : "FAIL", sensorMessage.c_str());
        Serial.printf("Memory:      %s  %s (%lu bytes free)\n",
                     memoryOK ? "OK" : "FAIL", memoryMessage.c_str(), freeHeap);
        Serial.printf("WiFi:        %s  %s (%d dBm)\n",
                     wifiOK ? "OK" : "FAIL", wifiMessage.c_str(), wifiRSSI);
        Serial.printf("Storage:     %s  %s\n", storageOK ? "OK" : "FAIL", storageMessage.c_str());
        Serial.printf("Temperature: %s  %.1f C\n", temperatureOK ? "OK" : "FAIL", sensorTemp);

        Serial.println("==============================\n");
    }

    const char* getStatusName(DiagnosticStatus status) const {
        switch(status) {
            case DIAG_OK: return "OK";
            case DIAG_WARNING: return "WARNING";
            case DIAG_ERROR: return "ERROR";
            case DIAG_CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
};

class SelfDiagnostic {
public:
    SelfDiagnostic();
    ~SelfDiagnostic();

    bool begin();

    DiagnosticResult runDiagnostics(MPU6050Driver* sensor = nullptr);

    bool shouldRunDiagnostics();

    DiagnosticResult getLastResult() const { return lastResult; }

    void setCheckInterval(uint32_t intervalMs) { checkInterval = intervalMs; }

private:
    DiagnosticResult lastResult;
    uint32_t lastCheckTime;
    uint32_t checkInterval;

    bool checkSensor(MPU6050Driver* sensor, DiagnosticResult& result);
    bool checkMemory(DiagnosticResult& result);
    bool checkWiFi(DiagnosticResult& result);
    bool checkStorage(DiagnosticResult& result);
    bool checkTemperature(MPU6050Driver* sensor, DiagnosticResult& result);

    void updateOverallStatus(DiagnosticResult& result);
};

#endif
