#ifndef SELF_CALIBRATING_MODEL_H
#define SELF_CALIBRATING_MODEL_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "ExtendedFeatureExtractor.h"

#define CALIB_MAX_FEATURES 32
#define CALIB_WINDOW_SIZE 500
#define CALIB_PERCENTILE_BINS 100
#define CALIB_UPDATE_INTERVAL 100

struct CalibrationState {
    float featureMeans[CALIB_MAX_FEATURES];
    float featureStdDevs[CALIB_MAX_FEATURES];
    float featureMin[CALIB_MAX_FEATURES];
    float featureMax[CALIB_MAX_FEATURES];
    float featureM2[CALIB_MAX_FEATURES];
    uint32_t sampleCount;
    bool isCalibrated;
};

struct ThresholdConfig {
    float warningMultiplier;
    float criticalMultiplier;
    float adaptationRate;
    bool useAdaptive;
};

struct AutoThresholds {
    float warningLevel[CALIB_MAX_FEATURES];
    float criticalLevel[CALIB_MAX_FEATURES];
    float baseline[CALIB_MAX_FEATURES];
};

struct TDigestCentroid {
    float mean;
    float weight;
};

struct QuantileEstimator {
    TDigestCentroid centroids[CALIB_PERCENTILE_BINS];
    size_t centroidCount;
    float totalWeight;
};

struct NormalizationParams {
    float mean;
    float stdDev;
    float min;
    float max;
    bool valid;
};

typedef void (*CalibrationCallback)(bool calibrated, uint32_t sampleCount);

class SelfCalibratingModel {
public:
    SelfCalibratingModel();
    ~SelfCalibratingModel();

    bool begin();
    void reset();

    void updateStatistics(const FeatureVector& features);
    void updateStatistics(const ExtendedFeatureVector& features);
    void updateStatistics(const float* features, size_t count);

    void normalize(const float* input, float* output, size_t count);
    void normalizeFeatureVector(const FeatureVector& input, float* output);
    void normalizeExtendedFeatures(const ExtendedFeatureVector& input, float* output);

    float normalizeValue(float value, size_t featureIndex);
    float denormalizeValue(float normalizedValue, size_t featureIndex);

    NormalizationParams getNormParams(size_t featureIndex) const;
    void setNormParams(size_t featureIndex, const NormalizationParams& params);

    bool isCalibrated() const { return state.isCalibrated; }
    uint32_t getSampleCount() const { return state.sampleCount; }
    void setMinCalibrationSamples(uint32_t min) { minCalibrationSamples = min; }

    void startCalibration();
    void completeCalibration();
    void forceCalibration(const float* means, const float* stdDevs, size_t count);

    void setThresholdConfig(const ThresholdConfig& config) { thresholdConfig = config; }
    ThresholdConfig getThresholdConfig() const { return thresholdConfig; }

    AutoThresholds getAutoThresholds() const { return autoThresholds; }
    void updateAutoThresholds();

    float getWarningThreshold(size_t featureIndex) const;
    float getCriticalThreshold(size_t featureIndex) const;

    bool isAboveWarning(float value, size_t featureIndex) const;
    bool isAboveCritical(float value, size_t featureIndex) const;

    float getPercentile(size_t featureIndex, float percentile);
    void updateQuantileEstimator(size_t featureIndex, float value);

    float getConfidenceScore(const float* features, size_t count);
    float getOutlierScore(const float* features, size_t count);

    void setCallback(CalibrationCallback callback) { calibCallback = callback; }

    String generateCalibrationReport() const;
    String exportNormalizationJSON() const;
    String exportNormalizationHeader() const;

    bool saveCalibration(const char* filename);
    bool loadCalibration(const char* filename);

private:
    CalibrationState state;
    ThresholdConfig thresholdConfig;
    AutoThresholds autoThresholds;
    QuantileEstimator quantiles[CALIB_MAX_FEATURES];

    uint32_t minCalibrationSamples;
    size_t featureCount;
    bool calibrationInProgress;

    CalibrationCallback calibCallback;

    void updateWelfordStats(size_t featureIndex, float value);
    void updateMinMax(size_t featureIndex, float value);

    void computeThresholds();
    float computeZScore(float value, size_t featureIndex);

    void addTDigestCentroid(size_t featureIndex, float value);
    void compressTDigest(size_t featureIndex);
    float queryTDigest(size_t featureIndex, float quantile);
};

#endif
