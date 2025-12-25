#ifndef DRIFT_DETECTOR_H
#define DRIFT_DETECTOR_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "ExtendedFeatureExtractor.h"

#define DRIFT_TRACKED_FEATURES 8
#define DRIFT_WINDOW_SIZE 50
#define DRIFT_REFERENCE_SIZE 100
#define DRIFT_DEFAULT_THRESHOLD 0.15f
#define DRIFT_PAGE_HINKLEY_LAMBDA 0.1f
#define DRIFT_PAGE_HINKLEY_THRESHOLD 50.0f
#define DRIFT_VARIANCE_THRESHOLD 2.0f
#define DRIFT_CUSUM_THRESHOLD 5.0f

enum DriftType {
    DRIFT_NONE = 0,
    DRIFT_GRADUAL,
    DRIFT_SUDDEN,
    DRIFT_RECURRING,
    DRIFT_INCREMENTAL
};

struct FeatureStatistics {
    float mean;
    float variance;
    float min;
    float max;
    float m2;
    uint32_t count;
};

struct DriftMetrics {
    float featureDrift[DRIFT_TRACKED_FEATURES];
    float overallDrift;
    float predictionDrift;
    float confidenceDrift;
    DriftType detectedType;
    uint32_t driftStartTime;
    float magnitude;
    uint8_t affectedFeatureCount;
    uint8_t affectedFeatures[DRIFT_TRACKED_FEATURES];
};

struct PageHinkleyState {
    float sum;
    float minSum;
    float mean;
    uint32_t sampleCount;
    bool driftDetected;
};

struct CUSUMState {
    float posSum;
    float negSum;
    float target;
    bool driftDetected;
};

typedef void (*DriftCallback)(DriftType type, float magnitude, const uint8_t* affectedFeatures, uint8_t count);

class DriftDetector {
public:
    DriftDetector();
    ~DriftDetector();

    bool begin();
    void reset();

    void updateStatistics(const FeatureVector& features);
    void updateStatistics(const ExtendedFeatureVector& features);
    void updateStatistics(const float* features, size_t count);

    DriftType detectDrift();
    DriftMetrics getDriftMetrics() const { return currentMetrics; }

    bool isDriftDetected() const { return currentMetrics.detectedType != DRIFT_NONE; }
    float getDriftMagnitude() const { return currentMetrics.magnitude; }
    DriftType getDriftType() const { return currentMetrics.detectedType; }

    void getAffectedFeatures(uint8_t* features, uint8_t& count) const;
    bool isFeatureAffected(uint8_t featureIndex) const;

    void setDriftThreshold(float threshold) { driftThreshold = threshold; }
    float getDriftThreshold() const { return driftThreshold; }

    void setReferenceWindow(const float* features, size_t windowSize, size_t featureCount);
    void captureCurrentAsReference();
    void clearReference();

    void updatePredictionStats(uint8_t predictedClass, float confidence);
    float getPredictionDrift() const;
    float getConfidenceDrift() const;

    void enablePageHinkley(bool enable) { usePageHinkley = enable; }
    void enableCUSUM(bool enable) { useCUSUM = enable; }
    void enableADWIN(bool enable) { useADWIN = enable; }

    void setCallback(DriftCallback callback) { driftCallback = callback; }

    FeatureStatistics getFeatureStats(uint8_t featureIndex) const;
    FeatureStatistics getReferenceStats(uint8_t featureIndex) const;

    String getDriftTypeName(DriftType type) const;
    String generateDriftReport() const;

    bool saveState(const char* filename);
    bool loadState(const char* filename);

    static const uint8_t trackedFeatureIndices[DRIFT_TRACKED_FEATURES];

private:
    FeatureStatistics currentStats[DRIFT_TRACKED_FEATURES];
    FeatureStatistics referenceStats[DRIFT_TRACKED_FEATURES];

    PageHinkleyState phStates[DRIFT_TRACKED_FEATURES];
    CUSUMState cusumStates[DRIFT_TRACKED_FEATURES];

    DriftMetrics currentMetrics;

    float driftThreshold;
    bool hasReference;
    uint32_t lastDriftTime;
    uint32_t samplesSinceReference;

    bool usePageHinkley;
    bool useCUSUM;
    bool useADWIN;

    float predictionHistory[DRIFT_WINDOW_SIZE];
    float confidenceHistory[DRIFT_WINDOW_SIZE];
    uint8_t historyIndex;
    uint8_t historyCount;
    float referencePredictionMean;
    float referenceConfidenceMean;

    DriftCallback driftCallback;

    void initializeStats(FeatureStatistics& stats);
    void updateWelfordStats(FeatureStatistics& stats, float value);

    void updatePageHinkley(uint8_t featureIndex, float value);
    bool checkPageHinkley(uint8_t featureIndex);

    void updateCUSUM(uint8_t featureIndex, float value);
    bool checkCUSUM(uint8_t featureIndex);

    float computeKSStatistic(uint8_t featureIndex);
    float computeZScore(uint8_t featureIndex);

    DriftType classifyDriftType();
    void identifyAffectedFeatures();

    float extractTrackedFeature(const FeatureVector& features, uint8_t trackedIndex);
    float extractTrackedFeature(const ExtendedFeatureVector& features, uint8_t trackedIndex);
};

#endif
