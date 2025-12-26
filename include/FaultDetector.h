#ifndef FAULT_DETECTOR_H
#define FAULT_DETECTOR_H

#include <Arduino.h>
#include <float.h>
#include "Config.h"
#include "FeatureExtractor.h"

struct FaultResult {
    FaultType type;
    SeverityLevel severity;
    float confidence;
    float anomalyScore;
    String description;
    uint32_t timestamp;

    void reset() {
        type = FAULT_NONE;
        severity = SEVERITY_NORMAL;
        confidence = 0.0f;
        anomalyScore = 0.0f;
        description = "";
        timestamp = 0;
    }

    void print() const {
        Serial.println("=== Fault Detection Result ===");
        Serial.printf("Type: %s\n", getFaultTypeName());
        Serial.printf("Severity: %s\n", getSeverityName());
        Serial.printf("Confidence: %.2f%%\n", confidence * 100);
        Serial.printf("Anomaly Score: %.4f\n", anomalyScore);
        Serial.printf("Description: %s\n", description.c_str());
    }

    const char* getFaultTypeName() const {
        switch(type) {
            case FAULT_NONE: return "NONE";
            case FAULT_IMBALANCE: return "IMBALANCE";
            case FAULT_MISALIGNMENT: return "MISALIGNMENT";
            case FAULT_BEARING: return "BEARING";
            case FAULT_LOOSENESS: return "LOOSENESS";
            case FAULT_UNKNOWN: return "UNKNOWN";
            default: return "UNDEFINED";
        }
    }

    const char* getSeverityName() const {
        switch(severity) {
            case SEVERITY_NORMAL: return "NORMAL";
            case SEVERITY_WARNING: return "WARNING";
            case SEVERITY_CRITICAL: return "CRITICAL";
            default: return "UNDEFINED";
        }
    }
};

struct BaselineStats {
    float mean[NUM_TOTAL_FEATURES];
    float stdDev[NUM_TOTAL_FEATURES];
    float min[NUM_TOTAL_FEATURES];
    float max[NUM_TOTAL_FEATURES];
    uint32_t sampleCount;
    bool isCalibrated;

    void init() {
        for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
            mean[i] = 0.0f;
            stdDev[i] = 0.0f;
            min[i] = FLT_MAX;
            max[i] = -FLT_MAX;
        }
        sampleCount = 0;
        isCalibrated = false;
    }
};

class FaultDetector {
public:

    FaultDetector();

    ~FaultDetector();

    bool begin();

    void startCalibration(uint32_t numSamples = CALIBRATION_SAMPLES);

    bool addCalibrationSample(const FeatureVector& features);

    void setBaseline(const BaselineStats& baseline);

    const BaselineStats& getBaseline() const { return baseline; }

    bool isCalibrated() const { return baseline.isCalibrated; }

    bool detectFault(const FeatureVector& features, FaultResult& result);

    float calculateAnomalyScore(const FeatureVector& features);

    void classifyFault(const FeatureVector& features, FaultResult& result);

    void setThresholds(float warningMultiplier, float criticalMultiplier);

    void reset();

    bool saveBaseline();

    bool loadBaseline();

private:
    BaselineStats baseline;
    float warningThreshold;
    float criticalThreshold;

    bool calibrating;
    uint32_t calibrationTarget;
    uint32_t calibrationCount;
    float calibrationSums[NUM_TOTAL_FEATURES];
    float calibrationSumSquares[NUM_TOTAL_FEATURES];

    void finalizeCalibration();

    float calculateZScore(float value, int featureIndex);

    FaultType classifyByRules(const FeatureVector& features);

    bool checkImbalance(const FeatureVector& features);

    bool checkMisalignment(const FeatureVector& features);

    bool checkBearing(const FeatureVector& features);

    bool checkLooseness(const FeatureVector& features);
};

#endif
