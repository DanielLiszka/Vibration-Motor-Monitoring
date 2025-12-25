#include "DataLogger.h"
#include "StorageManager.h"

DataLogger::DataLogger()
    : logCount(0)
    , alertCount(0)
    , lastLogTime(0)
    , lastAlertTime(0)
    , logInterval(LOG_INTERVAL_MS)
{
}

DataLogger::~DataLogger() {
}

bool DataLogger::begin() {
    DEBUG_PRINTLN("Initializing Data Logger...");
    logCount = 0;
    alertCount = 0;
    lastLogTime = millis();
    lastAlertTime = 0;
    DEBUG_PRINTLN("Data Logger initialized");
    return true;
}

void DataLogger::log(const FeatureVector& features, const FaultResult& fault, float temperature) {
    if (!shouldLog()) {
        return;
    }

    LogEntry entry;
    entry.timestamp = millis();
    entry.features = features;
    entry.fault = fault;
    entry.temperature = temperature;

    lastEntry = entry;
    lastLogTime = entry.timestamp;
    logCount++;

    if (LOG_TO_SERIAL) {
        logToSerial(entry);
    }

    if (LOG_TO_FLASH) {
        logToFlash(entry);
    }
}

void DataLogger::logAlert(const FaultResult& fault) {
    if (!canSendAlert()) {
        return;
    }

    alertCount++;
    lastAlertTime = millis();

    Serial.println("\n========================================");
    Serial.println("       FAULT ALERT!");
    Serial.println("========================================");
    fault.print();
    Serial.println("========================================\n");
}

void DataLogger::logToSerial(const LogEntry& entry) {
    Serial.println("\n--- Motor Vibration Log ---");
    Serial.printf("Timestamp: %s\n", formatTimestamp(entry.timestamp).c_str());
    Serial.printf("Temperature: %.1f °C\n", entry.temperature);

    Serial.println("\nFeatures:");
    entry.features.print();

    if (entry.fault.type != FAULT_NONE) {
        Serial.println("\nFault:");
        entry.fault.print();
    }

    Serial.println("---------------------------\n");
}

bool DataLogger::logToFlash(const LogEntry& entry) {
    static StorageManager storage;
    static bool storageInit = false;

    if (!storageInit) {
        if (!storage.begin()) {
            return false;
        }
        storageInit = true;
    }

    uint32_t dayNumber = entry.timestamp / 86400000UL;
    String filename = "log_" + String(dayNumber) + ".csv";

    String csvLine = entry.toCSV() + "\n";
    return storage.appendLog(filename, csvLine);
}

bool DataLogger::shouldLog() {
    uint32_t currentTime = millis();
    return (currentTime - lastLogTime) >= logInterval;
}

bool DataLogger::canSendAlert() {
    uint32_t currentTime = millis();
    return (currentTime - lastAlertTime) >= ALERT_COOLDOWN_MS;
}

void DataLogger::clearLogs() {
    logCount = 0;
    alertCount = 0;
    DEBUG_PRINTLN("Logs cleared");
}

String DataLogger::exportLogs(uint8_t format, uint32_t maxEntries) {
    StorageManager storage;
    if (!storage.begin()) {
        return "Storage initialization failed";
    }

    String output;
    if (storage.exportAllLogs(output)) {
        return output;
    }

    return "Export failed";
}

String DataLogger::formatTimestamp(uint32_t timestamp) const {
    uint32_t seconds = timestamp / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    seconds = seconds % 60;
    minutes = minutes % 60;
    hours = hours % 24;

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
             (int)hours, (int)minutes, (int)seconds, (int)(timestamp % 1000));

    return String(buffer);
}

String LogEntry::toJSON() const {
    String json = "{";
    json += "\"timestamp\":" + String(timestamp) + ",";
    json += "\"temperature\":" + String(temperature, 2) + ",";

    json += "\"features\":{";
    json += "\"rms\":" + String(features.rms, 4) + ",";
    json += "\"peakToPeak\":" + String(features.peakToPeak, 4) + ",";
    json += "\"kurtosis\":" + String(features.kurtosis, 4) + ",";
    json += "\"skewness\":" + String(features.skewness, 4) + ",";
    json += "\"crestFactor\":" + String(features.crestFactor, 4) + ",";
    json += "\"variance\":" + String(features.variance, 4) + ",";
    json += "\"spectralCentroid\":" + String(features.spectralCentroid, 2) + ",";
    json += "\"spectralSpread\":" + String(features.spectralSpread, 2) + ",";
    json += "\"bandPowerRatio\":" + String(features.bandPowerRatio, 4) + ",";
    json += "\"dominantFreq\":" + String(features.dominantFrequency, 2);
    json += "},";

    json += "\"fault\":{";
    json += "\"type\":\"" + String(fault.getFaultTypeName()) + "\",";
    json += "\"severity\":\"" + String(fault.getSeverityName()) + "\",";
    json += "\"confidence\":" + String(fault.confidence, 2) + ",";
    json += "\"anomalyScore\":" + String(fault.anomalyScore, 4) + ",";
    json += "\"description\":\"" + fault.description + "\"";
    json += "}";

    json += "}";
    return json;
}

String LogEntry::toCSV() const {
    String csv = String(timestamp) + ",";
    csv += String(temperature, 2) + ",";
    csv += String(features.rms, 4) + ",";
    csv += String(features.peakToPeak, 4) + ",";
    csv += String(features.kurtosis, 4) + ",";
    csv += String(features.skewness, 4) + ",";
    csv += String(features.crestFactor, 4) + ",";
    csv += String(features.variance, 4) + ",";
    csv += String(features.spectralCentroid, 2) + ",";
    csv += String(features.spectralSpread, 2) + ",";
    csv += String(features.bandPowerRatio, 4) + ",";
    csv += String(features.dominantFrequency, 2) + ",";
    csv += String(fault.getFaultTypeName()) + ",";
    csv += String(fault.getSeverityName()) + ",";
    csv += String(fault.confidence, 2) + ",";
    csv += String(fault.anomalyScore, 4);

    return csv;
}
