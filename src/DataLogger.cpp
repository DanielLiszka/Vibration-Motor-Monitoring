#include "DataLogger.h"
#include "StorageManager.h"

DataLogger::DataLogger()
    : logCount(0)
    , alertCount(0)
    , lastLogTime(0)
    , lastAlertTime(0)
    , logInterval(LOG_INTERVAL_MS)
    , trainingSampleCount(0)
    , trainingSampleHead(0)
{
    memset(trainingSamples, 0, sizeof(trainingSamples));
}

DataLogger::~DataLogger() {
}

bool DataLogger::begin() {
    DEBUG_PRINTLN("Initializing Data Logger...");
    logCount = 0;
    alertCount = 0;
    lastLogTime = millis();
    lastAlertTime = 0;
    loadTrainingSamplesFromFlash();
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

bool DataLogger::logTrainingSample(const float* features, uint8_t numFeatures,
                                   uint8_t label, float confidence, uint8_t labelSource) {
    if (!features || numFeatures == 0) {
        return false;
    }

    TrainingSample sample{};
    sample.timestamp = millis();
    sample.numFeatures = (numFeatures > 16) ? 16 : numFeatures;
    sample.label = label;
    sample.confidence = confidence;
    sample.labelSource = labelSource;
    sample.uploaded = false;

    for (uint8_t i = 0; i < sample.numFeatures; i++) {
        sample.features[i] = features[i];
    }
    for (uint8_t i = sample.numFeatures; i < 16; i++) {
        sample.features[i] = 0.0f;
    }

    if (trainingSampleCount < MAX_TRAINING_SAMPLES) {
        trainingSamples[trainingSampleCount] = sample;
        trainingSampleCount++;
    } else {
        for (size_t i = 1; i < MAX_TRAINING_SAMPLES; i++) {
            trainingSamples[i - 1] = trainingSamples[i];
        }
        trainingSamples[MAX_TRAINING_SAMPLES - 1] = sample;
    }

    return true;
}

TrainingSample* DataLogger::getTrainingSample(size_t index) {
    if (index >= trainingSampleCount) return nullptr;
    return &trainingSamples[index];
}

void DataLogger::markSampleUploaded(size_t index) {
    if (index >= trainingSampleCount) return;
    trainingSamples[index].uploaded = true;
}

void DataLogger::clearUploadedSamples() {
    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < trainingSampleCount; readIndex++) {
        if (!trainingSamples[readIndex].uploaded) {
            if (writeIndex != readIndex) {
                trainingSamples[writeIndex] = trainingSamples[readIndex];
            }
            writeIndex++;
        }
    }
    trainingSampleCount = writeIndex;
}

String DataLogger::exportTrainingSamples(size_t maxSamples) {
    String output;
    output.reserve(1024);

    output += "timestamp,label,confidence,labelSource,uploaded,numFeatures";
    for (int i = 0; i < 16; i++) {
        output += ",f";
        output += String(i);
    }
    output += "\n";

    size_t count = trainingSampleCount;
    if (maxSamples > 0 && maxSamples < count) {
        count = maxSamples;
    }

    size_t startIndex = trainingSampleCount - count;
    for (size_t i = startIndex; i < trainingSampleCount; i++) {
        const TrainingSample& s = trainingSamples[i];
        output += String(s.timestamp);
        output += ",";
        output += String(s.label);
        output += ",";
        output += String(s.confidence, 4);
        output += ",";
        output += String(s.labelSource);
        output += ",";
        output += String(s.uploaded ? 1 : 0);
        output += ",";
        output += String(s.numFeatures);

        for (int j = 0; j < 16; j++) {
            output += ",";
            output += String(s.features[j], 6);
        }
        output += "\n";
    }

    return output;
}

bool DataLogger::saveTrainingSamplesToFlash() {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    static const char* path = "/config/training_samples.bin";
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        return false;
    }

    const uint8_t magic[4] = {'T', 'S', 'M', 'P'};
    const uint16_t version = 1;
    const uint16_t count = (uint16_t)min((size_t)MAX_TRAINING_SAMPLES, trainingSampleCount);

    if (file.write(magic, sizeof(magic)) != sizeof(magic)) {
        file.close();
        return false;
    }
    if (file.write((const uint8_t*)&version, sizeof(version)) != sizeof(version)) {
        file.close();
        return false;
    }
    if (file.write((const uint8_t*)&count, sizeof(count)) != sizeof(count)) {
        file.close();
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        const TrainingSample& s = trainingSamples[i];

        uint8_t uploaded = s.uploaded ? 1 : 0;
        uint8_t numFeatures = (s.numFeatures > 16) ? 16 : s.numFeatures;

        if (file.write((const uint8_t*)&s.timestamp, sizeof(s.timestamp)) != sizeof(s.timestamp) ||
            file.write(&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures) ||
            file.write((const uint8_t*)&s.label, sizeof(s.label)) != sizeof(s.label) ||
            file.write((const uint8_t*)&s.labelSource, sizeof(s.labelSource)) != sizeof(s.labelSource) ||
            file.write(&uploaded, sizeof(uploaded)) != sizeof(uploaded) ||
            file.write((const uint8_t*)&s.confidence, sizeof(s.confidence)) != sizeof(s.confidence) ||
            file.write((const uint8_t*)s.features, sizeof(float) * 16) != (sizeof(float) * 16)) {
            file.close();
            return false;
        }
    }

    file.close();
    return true;
}

bool DataLogger::loadTrainingSamplesFromFlash() {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    static const char* path = "/config/training_samples.bin";
    if (!SPIFFS.exists(path)) {
        return false;
    }

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    uint8_t magic[4] = {0, 0, 0, 0};
    if (file.read(magic, sizeof(magic)) != sizeof(magic)) {
        file.close();
        return false;
    }
    if (memcmp(magic, "TSMP", 4) != 0) {
        file.close();
        return false;
    }

    uint16_t version = 0;
    uint16_t count = 0;
    if (file.read((uint8_t*)&version, sizeof(version)) != sizeof(version) ||
        file.read((uint8_t*)&count, sizeof(count)) != sizeof(count)) {
        file.close();
        return false;
    }
    if (version != 1) {
        file.close();
        return false;
    }

    if (count > MAX_TRAINING_SAMPLES) {
        count = MAX_TRAINING_SAMPLES;
    }

    trainingSampleCount = 0;
    for (uint16_t i = 0; i < count; i++) {
        TrainingSample s{};

        uint8_t uploaded = 0;
        uint8_t numFeatures = 0;

        if (file.read((uint8_t*)&s.timestamp, sizeof(s.timestamp)) != sizeof(s.timestamp) ||
            file.read(&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures) ||
            file.read((uint8_t*)&s.label, sizeof(s.label)) != sizeof(s.label) ||
            file.read((uint8_t*)&s.labelSource, sizeof(s.labelSource)) != sizeof(s.labelSource) ||
            file.read(&uploaded, sizeof(uploaded)) != sizeof(uploaded) ||
            file.read((uint8_t*)&s.confidence, sizeof(s.confidence)) != sizeof(s.confidence) ||
            file.read((uint8_t*)s.features, sizeof(float) * 16) != (sizeof(float) * 16)) {
            file.close();
            trainingSampleCount = 0;
            return false;
        }

        s.numFeatures = (numFeatures > 16) ? 16 : numFeatures;
        s.uploaded = (uploaded != 0);

        trainingSamples[trainingSampleCount] = s;
        trainingSampleCount++;
    }

    file.close();
    return true;
}
