#ifndef DATA_BUFFER_H
#define DATA_BUFFER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"

#define BUFFER_MAX_SIZE 1000
#define BUFFER_BATCH_SIZE 100
#define BUFFER_AUTO_EXPORT_INTERVAL 300000

struct DataRecord {
    FeatureVector features;
    FaultResult fault;
    uint32_t timestamp;
    bool hasFault;
};

class DataBuffer {
public:
    DataBuffer(size_t maxSize = BUFFER_MAX_SIZE);
    ~DataBuffer();

    void addRecord(const FeatureVector& features, const FaultResult* fault = nullptr);

    size_t getSize() const { return records.size(); }
    size_t getMaxSize() const { return maxBufferSize; }
    bool isFull() const { return records.size() >= maxBufferSize; }
    bool isEmpty() const { return records.empty(); }

    float getUsagePercent() const {
        return (float)records.size() / maxBufferSize * 100.0f;
    }

    const DataRecord& getRecord(size_t index) const;
    std::vector<DataRecord> getRecords(size_t start, size_t count) const;
    std::vector<DataRecord> getLatestRecords(size_t count) const;
    std::vector<DataRecord> getAllRecords() const { return records; }

    void clear();
    void removeOldest(size_t count = 1);

    String exportToJSON(size_t start = 0, size_t count = 0);
    String exportToCSV(size_t start = 0, size_t count = 0);

    bool saveToFile(const String& filename);
    bool loadFromFile(const String& filename);

    void setAutoExport(bool enable) { autoExportEnabled = enable; }
    bool isAutoExportEnabled() const { return autoExportEnabled; }
    void setAutoExportInterval(uint32_t intervalMs) { autoExportInterval = intervalMs; }

    bool shouldAutoExport() const;
    void updateLastExportTime() { lastExportTime = millis(); }

    size_t getFaultCount() const;
    float getAverageFaultRate() const;

private:
    std::vector<DataRecord> records;
    size_t maxBufferSize;
    bool autoExportEnabled;
    uint32_t autoExportInterval;
    uint32_t lastExportTime;

    String recordToJSON(const DataRecord& record);
    String recordToCSV(const DataRecord& record);
};

#endif
