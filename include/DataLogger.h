#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"

struct LogEntry {
    uint32_t timestamp;
    FeatureVector features;
    FaultResult fault;
    float temperature;

    String toJSON() const;

    String toCSV() const;
};


struct TrainingSample {
    uint32_t timestamp;
    float features[16];
    uint8_t numFeatures;
    uint8_t label;
    float confidence;
    uint8_t labelSource;
    bool uploaded;
};

#define MAX_TRAINING_SAMPLES 100

class DataLogger {
public:

    DataLogger();

    ~DataLogger();

    bool begin();

    void log(const FeatureVector& features, const FaultResult& fault, float temperature = 0.0f);

    void logAlert(const FaultResult& fault);

    void logToSerial(const LogEntry& entry);

    bool logToFlash(const LogEntry& entry);

    const LogEntry& getLastEntry() const { return lastEntry; }

    uint32_t getLogCount() const { return logCount; }

    uint32_t getAlertCount() const { return alertCount; }

    void clearLogs();

    String exportLogs(uint8_t format = 0, uint32_t maxEntries = 0);

    void setLogInterval(uint32_t intervalMs) { logInterval = intervalMs; }

    bool shouldLog();


    bool logTrainingSample(const float* features, uint8_t numFeatures,
                           uint8_t label, float confidence, uint8_t labelSource);
    size_t getTrainingSampleCount() const { return trainingSampleCount; }
    TrainingSample* getTrainingSample(size_t index);
    void markSampleUploaded(size_t index);
    void clearUploadedSamples();
    String exportTrainingSamples(size_t maxSamples = 0);
    bool saveTrainingSamplesToFlash();
    bool loadTrainingSamplesFromFlash();

private:
    LogEntry lastEntry;
    uint32_t logCount;
    uint32_t alertCount;
    uint32_t lastLogTime;
    uint32_t lastAlertTime;
    uint32_t logInterval;


    TrainingSample trainingSamples[MAX_TRAINING_SAMPLES];
    size_t trainingSampleCount;
    size_t trainingSampleHead;

    String formatTimestamp(uint32_t timestamp) const;

    bool canSendAlert();
};

#endif
