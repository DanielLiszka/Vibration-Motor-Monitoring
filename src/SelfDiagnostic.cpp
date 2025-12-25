#include "SelfDiagnostic.h"
#include <WiFi.h>
#include <SPIFFS.h>

SelfDiagnostic::SelfDiagnostic()
    : lastCheckTime(0)
    , checkInterval(DIAG_CHECK_INTERVAL_MS)
{
    lastResult.reset();
}

SelfDiagnostic::~SelfDiagnostic() {
}

bool SelfDiagnostic::begin() {
    DEBUG_PRINTLN("Initializing Self Diagnostic...");
    lastCheckTime = millis();
    return true;
}

DiagnosticResult SelfDiagnostic::runDiagnostics(MPU6050Driver* sensor) {
    DiagnosticResult result;
    result.reset();
    result.uptime = millis();

    checkMemory(result);
    checkStorage(result);

    if (sensor) {
        checkSensor(sensor, result);
        checkTemperature(sensor, result);
    }

    if (WiFi.status() == WL_CONNECTED) {
        checkWiFi(result);
    }

    updateOverallStatus(result);

    lastResult = result;
    lastCheckTime = millis();

    return result;
}

bool SelfDiagnostic::shouldRunDiagnostics() {
    return (millis() - lastCheckTime) >= checkInterval;
}

bool SelfDiagnostic::checkSensor(MPU6050Driver* sensor, DiagnosticResult& result) {
    result.sensorOK = sensor->isConnected();

    if (!result.sensorOK) {
        result.sensorMessage = "Sensor not responding";
        return false;
    }

    AccelData data;
    if (!sensor->readAcceleration(data)) {
        result.sensorOK = false;
        result.sensorMessage = "Failed to read sensor data";
        return false;
    }

    result.sensorMessage = "Sensor OK";
    return true;
}

bool SelfDiagnostic::checkMemory(DiagnosticResult& result) {
    result.freeHeap = ESP.getFreeHeap();
    result.memoryOK = (result.freeHeap >= DIAG_MIN_HEAP_BYTES);

    if (!result.memoryOK) {
        result.memoryMessage = "Low memory warning";
    } else {
        result.memoryMessage = "Memory OK";
    }

    return result.memoryOK;
}

bool SelfDiagnostic::checkWiFi(DiagnosticResult& result) {
    result.wifiRSSI = WiFi.RSSI();
    result.wifiOK = (result.wifiRSSI >= DIAG_MIN_RSSI_DBM);

    if (!result.wifiOK) {
        result.wifiMessage = "Weak signal";
    } else {
        result.wifiMessage = "WiFi OK";
    }

    return result.wifiOK;
}

bool SelfDiagnostic::checkStorage(DiagnosticResult& result) {
    if (!SPIFFS.begin()) {
        result.storageOK = false;
        result.storageMessage = "Storage not mounted";
        return false;
    }

    size_t total = SPIFFS.totalBytes();
    size_t used = SPIFFS.usedBytes();
    float usage = (used * 100.0) / total;

    result.storageOK = (usage < 90.0);

    if (!result.storageOK) {
        result.storageMessage = "Storage nearly full";
    } else {
        result.storageMessage = "Storage OK";
    }

    return result.storageOK;
}

bool SelfDiagnostic::checkTemperature(MPU6050Driver* sensor, DiagnosticResult& result) {
    result.sensorTemp = sensor->getTemperature();
    result.temperatureOK = (result.sensorTemp < DIAG_MAX_TEMP_C);

    if (!result.temperatureOK) {
        result.temperatureMessage = "Temperature high";
    } else {
        result.temperatureMessage = "Temperature OK";
    }

    return result.temperatureOK;
}

void SelfDiagnostic::updateOverallStatus(DiagnosticResult& result) {
    int errorCount = 0;
    int warningCount = 0;

    if (!result.sensorOK) errorCount++;
    if (!result.memoryOK) warningCount++;
    if (!result.wifiOK) warningCount++;
    if (!result.storageOK) warningCount++;
    if (!result.temperatureOK) errorCount++;

    if (errorCount > 0) {
        result.overall = DIAG_ERROR;
    } else if (warningCount > 1) {
        result.overall = DIAG_WARNING;
    } else {
        result.overall = DIAG_OK;
    }
}
