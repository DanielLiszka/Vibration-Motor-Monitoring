#include "DataBuffer.h"
#include "DataExporter.h"
#include "StorageManager.h"
#include <ArduinoJson.h>

static FaultType faultTypeFromString(const char* type) {
    if (!type) return FAULT_NONE;
    if (strcmp(type, "IMBALANCE") == 0) return FAULT_IMBALANCE;
    if (strcmp(type, "MISALIGNMENT") == 0) return FAULT_MISALIGNMENT;
    if (strcmp(type, "BEARING") == 0) return FAULT_BEARING;
    if (strcmp(type, "LOOSENESS") == 0) return FAULT_LOOSENESS;
    if (strcmp(type, "UNKNOWN") == 0) return FAULT_UNKNOWN;
    return FAULT_NONE;
}

static SeverityLevel severityFromString(const char* severity) {
    if (!severity) return SEVERITY_NORMAL;
    if (strcmp(severity, "WARNING") == 0) return SEVERITY_WARNING;
    if (strcmp(severity, "CRITICAL") == 0) return SEVERITY_CRITICAL;
    return SEVERITY_NORMAL;
}

DataBuffer::DataBuffer(size_t maxSize)
    : maxBufferSize(maxSize)
    , autoExportEnabled(false)
    , autoExportInterval(BUFFER_AUTO_EXPORT_INTERVAL)
    , lastExportTime(0)
{
    records.reserve(maxSize);
}

DataBuffer::~DataBuffer() {
    records.clear();
}

void DataBuffer::addRecord(const FeatureVector& features, const FaultResult* fault) {
    if (isFull()) {
        removeOldest(1);
    }

    DataRecord record;
    record.features = features;
    record.timestamp = millis();
    record.hasFault = (fault != nullptr);

    if (fault != nullptr) {
        record.fault = *fault;
    }

    records.push_back(record);
}

const DataRecord& DataBuffer::getRecord(size_t index) const {
    if (index >= records.size()) {
        static DataRecord emptyRecord;
        return emptyRecord;
    }
    return records[index];
}

std::vector<DataRecord> DataBuffer::getRecords(size_t start, size_t count) const {
    std::vector<DataRecord> result;

    if (start >= records.size()) {
        return result;
    }

    size_t end = start + count;
    if (end > records.size() || count == 0) {
        end = records.size();
    }

    for (size_t i = start; i < end; i++) {
        result.push_back(records[i]);
    }

    return result;
}

std::vector<DataRecord> DataBuffer::getLatestRecords(size_t count) const {
    if (count == 0 || records.empty()) {
        return std::vector<DataRecord>();
    }

    size_t start = 0;
    if (records.size() > count) {
        start = records.size() - count;
    }

    return getRecords(start, count);
}

void DataBuffer::clear() {
    records.clear();
}

void DataBuffer::removeOldest(size_t count) {
    if (count >= records.size()) {
        clear();
        return;
    }

    records.erase(records.begin(), records.begin() + count);
}

String DataBuffer::exportToJSON(size_t start, size_t count) {
    std::vector<DataRecord> exportRecords = getRecords(start, count);

    String json = "{\"records\":[";

    for (size_t i = 0; i < exportRecords.size(); i++) {
        json += recordToJSON(exportRecords[i]);
        if (i < exportRecords.size() - 1) {
            json += ",";
        }
    }

    json += "],\"count\":" + String(exportRecords.size());
    json += ",\"total\":" + String(records.size());
    json += "}";

    return json;
}

String DataBuffer::exportToCSV(size_t start, size_t count) {
    std::vector<DataRecord> exportRecords = getRecords(start, count);

    String csv = "timestamp,rms,peakToPeak,kurtosis,skewness,crestFactor,variance,";
    csv += "spectralCentroid,spectralSpread,bandPowerRatio,dominantFreq,";
    csv += "hasFault,faultType,faultSeverity,faultConfidence,faultAnomalyScore\n";

    for (const auto& record : exportRecords) {
        csv += recordToCSV(record);
    }

    return csv;
}

bool DataBuffer::saveToFile(const String& filename) {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    const bool isCsv = filename.endsWith(".csv") || filename.endsWith(".CSV");
    String data = isCsv ? exportToCSV(0, 0) : exportToJSON(0, 0);
    return storage.saveLog(filename, data);
}

bool DataBuffer::loadFromFile(const String& filename) {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    String content = storage.readLog(filename);
    content.trim();

    if (content.length() == 0) {
        return false;
    }

    const bool looksLikeCsv = filename.endsWith(".csv") || filename.endsWith(".CSV") ||
                              content.startsWith("timestamp,");
    if (looksLikeCsv) {
        clear();

        size_t lineCount = 0;
        for (size_t i = 0; i < content.length(); i++) {
            if (content[i] == '\n') lineCount++;
        }
        if (content.length() > 0 && content[content.length() - 1] != '\n') {
            lineCount++;
        }

        const size_t dataLines = (lineCount > 1) ? (lineCount - 1) : 0;
        const size_t skipLines = (dataLines > maxBufferSize) ? (dataLines - maxBufferSize) : 0;

        char* buffer = new char[content.length() + 1];
        content.toCharArray(buffer, content.length() + 1);

        char* saveptrLine = nullptr;
        char* line = strtok_r(buffer, "\n", &saveptrLine);
        bool headerSkipped = false;
        size_t dataLineIndex = 0;

        while (line) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\r') {
                line[len - 1] = '\0';
            }

            if (!headerSkipped) {
                headerSkipped = true;
                line = strtok_r(nullptr, "\n", &saveptrLine);
                continue;
            }

            if (line[0] == '\0') {
                line = strtok_r(nullptr, "\n", &saveptrLine);
                continue;
            }

            if (dataLineIndex++ < skipLines) {
                line = strtok_r(nullptr, "\n", &saveptrLine);
                continue;
            }

            DataRecord record;
            record.timestamp = 0;
            record.hasFault = false;
            record.features.reset();
            record.fault.reset();

            char* saveptrTok = nullptr;
            char* token = strtok_r(line, ",", &saveptrTok);
            int col = 0;

            while (token) {
                switch (col) {
                    case 0:
                        record.timestamp = (uint32_t)strtoul(token, nullptr, 10);
                        record.features.timestamp = record.timestamp;
                        record.fault.timestamp = record.timestamp;
                        break;
                    case 1: record.features.rms = strtof(token, nullptr); break;
                    case 2: record.features.peakToPeak = strtof(token, nullptr); break;
                    case 3: record.features.kurtosis = strtof(token, nullptr); break;
                    case 4: record.features.skewness = strtof(token, nullptr); break;
                    case 5: record.features.crestFactor = strtof(token, nullptr); break;
                    case 6: record.features.variance = strtof(token, nullptr); break;
                    case 7: record.features.spectralCentroid = strtof(token, nullptr); break;
                    case 8: record.features.spectralSpread = strtof(token, nullptr); break;
                    case 9: record.features.bandPowerRatio = strtof(token, nullptr); break;
                    case 10: record.features.dominantFrequency = strtof(token, nullptr); break;
                    case 11: record.hasFault = (atoi(token) != 0); break;
                    case 12: record.fault.type = faultTypeFromString(token); break;
                    case 13: record.fault.severity = severityFromString(token); break;
                    case 14: record.fault.confidence = strtof(token, nullptr); break;
                    case 15: record.fault.anomalyScore = strtof(token, nullptr); break;
                    default: break;
                }

                col++;
                token = strtok_r(nullptr, ",", &saveptrTok);
            }

            if (!record.hasFault || record.fault.type == FAULT_NONE) {
                record.hasFault = false;
                record.fault.type = FAULT_NONE;
                record.fault.severity = SEVERITY_NORMAL;
            }

            records.push_back(record);
            if (records.size() > maxBufferSize) {
                records.erase(records.begin());
            }

            line = strtok_r(nullptr, "\n", &saveptrLine);
        }

        delete[] buffer;
        return !records.empty();
    }

    if (!content.startsWith("{")) {
        return false;
    }

    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        return false;
    }

    JsonArray recordsArray = doc["records"].as<JsonArray>();
    if (recordsArray.isNull()) {
        return false;
    }

    clear();

    const size_t total = recordsArray.size();
    const size_t start = (total > maxBufferSize) ? (total - maxBufferSize) : 0;

    for (size_t i = start; i < total; i++) {
        JsonObject recordObj = recordsArray[i];
        if (recordObj.isNull()) continue;

        DataRecord record;
        record.timestamp = recordObj["timestamp"] | 0;
        record.hasFault = recordObj.containsKey("fault");
        record.features.reset();
        record.fault.reset();

        JsonObject featuresObj = recordObj["features"].as<JsonObject>();
        if (!featuresObj.isNull()) {
            record.features.timestamp = featuresObj["timestamp"] | record.timestamp;
            record.features.rms = featuresObj["rms"] | 0.0f;
            record.features.peakToPeak = featuresObj["peakToPeak"] | 0.0f;
            record.features.kurtosis = featuresObj["kurtosis"] | 0.0f;
            record.features.skewness = featuresObj["skewness"] | 0.0f;
            record.features.crestFactor = featuresObj["crestFactor"] | 0.0f;
            record.features.variance = featuresObj["variance"] | 0.0f;
            record.features.spectralCentroid = featuresObj["spectralCentroid"] | 0.0f;
            record.features.spectralSpread = featuresObj["spectralSpread"] | 0.0f;
            record.features.bandPowerRatio = featuresObj["bandPowerRatio"] | 0.0f;
            record.features.dominantFrequency = featuresObj["dominantFreq"] | 0.0f;
        } else {
            record.features.timestamp = record.timestamp;
        }

        if (record.hasFault) {
            JsonObject faultObj = recordObj["fault"].as<JsonObject>();
            if (!faultObj.isNull()) {
                record.fault.timestamp = faultObj["timestamp"] | record.timestamp;
                record.fault.type = faultTypeFromString(faultObj["type"] | "NONE");
                record.fault.severity = severityFromString(faultObj["severity"] | "NORMAL");
                record.fault.confidence = faultObj["confidence"] | 0.0f;
                record.fault.anomalyScore = faultObj["anomalyScore"] | 0.0f;
                record.fault.description = String(faultObj["description"] | "");
            }
        }

        if (!record.hasFault || record.fault.type == FAULT_NONE) {
            record.hasFault = false;
            record.fault.type = FAULT_NONE;
            record.fault.severity = SEVERITY_NORMAL;
        }

        records.push_back(record);
    }

    return !records.empty();
}

bool DataBuffer::shouldAutoExport() const {
    if (!autoExportEnabled) {
        return false;
    }

    return (millis() - lastExportTime >= autoExportInterval) || isFull();
}

size_t DataBuffer::getFaultCount() const {
    size_t count = 0;
    for (const auto& record : records) {
        if (record.hasFault && record.fault.type != FAULT_NONE) {
            count++;
        }
    }
    return count;
}

float DataBuffer::getAverageFaultRate() const {
    if (records.empty()) {
        return 0.0f;
    }

    return (float)getFaultCount() / records.size() * 100.0f;
}

String DataBuffer::recordToJSON(const DataRecord& record) {
    DataExporter exporter;
    String json = "{";

    json += "\"timestamp\":" + String(record.timestamp) + ",";
    json += "\"features\":" + exporter.exportFeatures(record.features, FORMAT_JSON);

    if (record.hasFault) {
        json += ",\"fault\":" + exporter.exportFault(record.fault, FORMAT_JSON);
    }

    json += "}";

    return json;
}

String DataBuffer::recordToCSV(const DataRecord& record) {
    String csv = String(record.timestamp) + ",";

    csv += String(record.features.rms, 4) + ",";
    csv += String(record.features.peakToPeak, 4) + ",";
    csv += String(record.features.kurtosis, 4) + ",";
    csv += String(record.features.skewness, 4) + ",";
    csv += String(record.features.crestFactor, 4) + ",";
    csv += String(record.features.variance, 4) + ",";
    csv += String(record.features.spectralCentroid, 2) + ",";
    csv += String(record.features.spectralSpread, 2) + ",";
    csv += String(record.features.bandPowerRatio, 4) + ",";
    csv += String(record.features.dominantFrequency, 2) + ",";

    if (record.hasFault) {
        csv += "1,";
        csv += String(record.fault.getFaultTypeName()) + ",";
        csv += String(record.fault.getSeverityName()) + ",";
        csv += String(record.fault.confidence, 2) + ",";
        csv += String(record.fault.anomalyScore, 4);
    } else {
        csv += "0,NONE,NORMAL,0,0";
    }

    csv += "\n";

    return csv;
}
