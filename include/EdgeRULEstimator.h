#ifndef EDGE_RUL_ESTIMATOR_H
#define EDGE_RUL_ESTIMATOR_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "ExtendedFeatureExtractor.h"

#define RUL_HISTORY_SIZE 200
#define RUL_MIN_SAMPLES_FOR_ESTIMATION 50
#define RUL_UPDATE_INTERVAL_MS 60000
#define RUL_FAILURE_THRESHOLD 0.2f
#define RUL_HEALTHY_THRESHOLD 0.8f

enum DegradationStage {
    STAGE_HEALTHY = 0,
    STAGE_EARLY_DEGRADATION,
    STAGE_ACCELERATED_DEGRADATION,
    STAGE_NEAR_FAILURE
};

struct HealthIndicator {
    float value;
    uint32_t timestamp;
    uint8_t faultClass;
    float anomalyScore;
};

struct RULEstimate {
    float estimatedHoursRemaining;
    float lowerBound;
    float upperBound;
    float confidenceInterval;
    DegradationStage currentStage;
    float healthIndex;
    float degradationRate;
    uint32_t lastUpdateTime;
    bool valid;
};

struct ExponentialParams {
    float a;
    float b;
    float c;
    float rSquared;
    bool valid;
};

struct RLSState {
    float P[3][3];
    float theta[3];
    float forgettingFactor;
};

struct DegradationTrend {
    float dailySlopes[7];
    float weeklyTrend;
    float accelerationFactor;
    bool isAccelerating;
    bool isDeteriorating;
};

typedef void (*RULCallback)(const RULEstimate& estimate, DegradationStage previousStage);

class EdgeRULEstimator {
public:
    EdgeRULEstimator();
    ~EdgeRULEstimator();

    bool begin();
    void reset();

    void updateHealthIndex(const FeatureVector& features, float anomalyScore);
    void updateHealthIndex(const ExtendedFeatureVector& features, float anomalyScore);
    void updateHealthIndex(float healthValue, float anomalyScore, uint8_t faultClass);

    RULEstimate estimateRUL();
    RULEstimate getLastEstimate() const { return lastEstimate; }

    float getCurrentHealthIndex() const { return currentHealthIndex; }
    DegradationStage getDegradationStage() const { return currentStage; }

    void setOperatingHours(float hours) { operatingHours = hours; }
    float getOperatingHours() const { return operatingHours; }
    void incrementOperatingHours(float deltaHours);

    void setFailureThreshold(float threshold) { failureThreshold = threshold; }
    float getFailureThreshold() const { return failureThreshold; }

    void setHealthyThreshold(float threshold) { healthyThreshold = threshold; }
    float getHealthyThreshold() const { return healthyThreshold; }

    ExponentialParams getModelParams() const { return modelParams; }
    float getModelFitQuality() const { return modelParams.rSquared; }

    DegradationTrend getDegradationTrend() const { return degradationTrend; }
    bool isAccelerating() const { return degradationTrend.isAccelerating; }
    bool isDeteriorating() const { return degradationTrend.isDeteriorating; }

    void calibrateThresholds(const float* healthyBaseline, size_t count);
    void setBaselineHealth(float baseline) { baselineHealth = baseline; }

    float predictHealthAt(float hoursAhead);
    float getTimeToThreshold(float threshold);

    void setCallback(RULCallback callback) { rulCallback = callback; }

    size_t getHistoryCount() const { return historyCount; }
    HealthIndicator getHistoryEntry(size_t index) const;
    void clearHistory();

    String generateRULReport() const;
    String generateHealthJSON() const;

    bool saveState(const char* filename);
    bool loadState(const char* filename);

    String getStageName(DegradationStage stage) const;

private:
    HealthIndicator history[RUL_HISTORY_SIZE];
    size_t historyIndex;
    size_t historyCount;

    RULEstimate lastEstimate;
    ExponentialParams modelParams;
    RLSState rlsState;
    DegradationTrend degradationTrend;

    float currentHealthIndex;
    DegradationStage currentStage;
    DegradationStage previousStage;

    float operatingHours;
    float baselineHealth;
    float failureThreshold;
    float healthyThreshold;

    uint32_t lastUpdateTime;
    uint32_t lastEstimationTime;

    RULCallback rulCallback;

    float computeHealthIndex(const FeatureVector& features, float anomalyScore);
    float computeHealthIndex(const ExtendedFeatureVector& features, float anomalyScore);

    void addToHistory(float healthValue, float anomalyScore, uint8_t faultClass);

    void updateExponentialFit();
    void updateRLS(float t, float y);
    void initializeRLS();

    float exponentialModel(float t);
    float computeRSquared();

    void updateDegradationTrend();
    void classifyStage();

    float computeConfidenceInterval();
    float computePredictionInterval(float hoursAhead);
};

#endif
