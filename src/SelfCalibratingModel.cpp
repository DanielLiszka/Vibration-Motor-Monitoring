#include "SelfCalibratingModel.h"
#include "StorageManager.h"
#include <math.h>

SelfCalibratingModel::SelfCalibratingModel()
    : minCalibrationSamples(100)
    , featureCount(0)
    , calibrationInProgress(false)
    , calibCallback(nullptr)
{
    memset(&state, 0, sizeof(state));

    thresholdConfig.warningMultiplier = 2.0f;
    thresholdConfig.criticalMultiplier = 3.0f;
    thresholdConfig.adaptationRate = 0.01f;
    thresholdConfig.useAdaptive = true;

    memset(&autoThresholds, 0, sizeof(autoThresholds));

    for (int i = 0; i < CALIB_MAX_FEATURES; i++) {
        quantiles[i].centroidCount = 0;
        quantiles[i].totalWeight = 0.0f;
    }
}

SelfCalibratingModel::~SelfCalibratingModel() {
}

bool SelfCalibratingModel::begin() {
    reset();
    DEBUG_PRINTLN("SelfCalibratingModel initialized");
    return true;
}

void SelfCalibratingModel::reset() {
    memset(&state, 0, sizeof(state));
    memset(&autoThresholds, 0, sizeof(autoThresholds));

    for (int i = 0; i < CALIB_MAX_FEATURES; i++) {
        state.featureMin[i] = INFINITY;
        state.featureMax[i] = -INFINITY;
        quantiles[i].centroidCount = 0;
        quantiles[i].totalWeight = 0.0f;
    }

    featureCount = 0;
    calibrationInProgress = false;
}

void SelfCalibratingModel::startCalibration() {
    reset();
    calibrationInProgress = true;
    DEBUG_PRINTLN("Calibration started");
}

void SelfCalibratingModel::completeCalibration() {
    if (state.sampleCount < minCalibrationSamples) {
        DEBUG_PRINTLN("Not enough samples for calibration");
        return;
    }

    state.isCalibrated = true;
    calibrationInProgress = false;

    computeThresholds();

    if (calibCallback) {
        calibCallback(true, state.sampleCount);
    }

    DEBUG_PRINT("Calibration complete with ");
    DEBUG_PRINT(state.sampleCount);
    DEBUG_PRINTLN(" samples");
}

void SelfCalibratingModel::updateStatistics(const FeatureVector& features) {
    float featureArray[10] = {
        features.rms,
        features.peakToPeak,
        features.kurtosis,
        features.skewness,
        features.crestFactor,
        features.variance,
        features.spectralCentroid,
        features.spectralSpread,
        features.bandPowerRatio,
        features.dominantFrequency
    };

    updateStatistics(featureArray, 10);
}

void SelfCalibratingModel::updateStatistics(const ExtendedFeatureVector& features) {
    float featureArray[24];

    featureArray[0] = features.rms;
    featureArray[1] = features.peakToPeak;
    featureArray[2] = features.kurtosis;
    featureArray[3] = features.skewness;
    featureArray[4] = features.crestFactor;
    featureArray[5] = features.variance;
    featureArray[6] = features.spectralCentroid;
    featureArray[7] = features.spectralSpread;
    featureArray[8] = features.bandPowerRatio;
    featureArray[9] = features.dominantFrequency;
    featureArray[10] = features.zeroCrossingRate;
    featureArray[11] = features.spectralFlux;
    featureArray[12] = features.spectralRolloff;
    featureArray[13] = features.spectralFlatness;
    featureArray[14] = features.harmonicRatio;
    featureArray[15] = features.impulseFactor;
    featureArray[16] = features.shapeFactor;
    featureArray[17] = features.clearanceFactor;
    featureArray[18] = features.totalEnergy;
    featureArray[19] = features.entropyValue;
    featureArray[20] = features.autocorrPeak;
    featureArray[21] = features.mfcc[0];
    featureArray[22] = features.mfcc[1];
    featureArray[23] = features.mfcc[2];

    updateStatistics(featureArray, 24);
}

void SelfCalibratingModel::updateStatistics(const float* features, size_t count) {
    if (count > CALIB_MAX_FEATURES) count = CALIB_MAX_FEATURES;

    if (featureCount == 0) featureCount = count;

    state.sampleCount++;

    for (size_t i = 0; i < count; i++) {
        updateWelfordStats(i, features[i]);
        updateMinMax(i, features[i]);

        if (thresholdConfig.useAdaptive) {
            updateQuantileEstimator(i, features[i]);
        }
    }

    if (!state.isCalibrated && state.sampleCount >= minCalibrationSamples) {
        completeCalibration();
    }
}

void SelfCalibratingModel::updateWelfordStats(size_t featureIndex, float value) {
    if (featureIndex >= CALIB_MAX_FEATURES) return;

    float delta = value - state.featureMeans[featureIndex];
    state.featureMeans[featureIndex] += delta / state.sampleCount;
    float delta2 = value - state.featureMeans[featureIndex];
    state.featureM2[featureIndex] += delta * delta2;

    if (state.sampleCount > 1) {
        state.featureStdDevs[featureIndex] = sqrt(state.featureM2[featureIndex] / (state.sampleCount - 1));
    }
}

void SelfCalibratingModel::updateMinMax(size_t featureIndex, float value) {
    if (featureIndex >= CALIB_MAX_FEATURES) return;

    if (value < state.featureMin[featureIndex]) {
        state.featureMin[featureIndex] = value;
    }
    if (value > state.featureMax[featureIndex]) {
        state.featureMax[featureIndex] = value;
    }
}

void SelfCalibratingModel::normalize(const float* input, float* output, size_t count) {
    for (size_t i = 0; i < count && i < CALIB_MAX_FEATURES; i++) {
        output[i] = normalizeValue(input[i], i);
    }
}

void SelfCalibratingModel::normalizeFeatureVector(const FeatureVector& input, float* output) {
    float featureArray[10] = {
        input.rms, input.peakToPeak, input.kurtosis, input.skewness,
        input.crestFactor, input.variance, input.spectralCentroid,
        input.spectralSpread, input.bandPowerRatio, input.dominantFrequency
    };

    normalize(featureArray, output, 10);
}

void SelfCalibratingModel::normalizeExtendedFeatures(const ExtendedFeatureVector& input, float* output) {
    float featureArray[24];

    featureArray[0] = input.rms;
    featureArray[1] = input.peakToPeak;
    featureArray[2] = input.kurtosis;
    featureArray[3] = input.skewness;
    featureArray[4] = input.crestFactor;
    featureArray[5] = input.variance;
    featureArray[6] = input.spectralCentroid;
    featureArray[7] = input.spectralSpread;
    featureArray[8] = input.bandPowerRatio;
    featureArray[9] = input.dominantFrequency;
    featureArray[10] = input.zeroCrossingRate;
    featureArray[11] = input.spectralFlux;
    featureArray[12] = input.spectralRolloff;
    featureArray[13] = input.spectralFlatness;
    featureArray[14] = input.harmonicRatio;
    featureArray[15] = input.impulseFactor;
    featureArray[16] = input.shapeFactor;
    featureArray[17] = input.clearanceFactor;
    featureArray[18] = input.totalEnergy;
    featureArray[19] = input.entropyValue;
    featureArray[20] = input.autocorrPeak;
    featureArray[21] = input.mfcc[0];
    featureArray[22] = input.mfcc[1];
    featureArray[23] = input.mfcc[2];

    normalize(featureArray, output, 24);
}

float SelfCalibratingModel::normalizeValue(float value, size_t featureIndex) {
    if (featureIndex >= CALIB_MAX_FEATURES) return value;

    float mean = state.featureMeans[featureIndex];
    float std = state.featureStdDevs[featureIndex];

    if (std < 0.0001f) {
        std = 1.0f;
    }

    return (value - mean) / std;
}

float SelfCalibratingModel::denormalizeValue(float normalizedValue, size_t featureIndex) {
    if (featureIndex >= CALIB_MAX_FEATURES) return normalizedValue;

    float mean = state.featureMeans[featureIndex];
    float std = state.featureStdDevs[featureIndex];

    return normalizedValue * std + mean;
}

NormalizationParams SelfCalibratingModel::getNormParams(size_t featureIndex) const {
    NormalizationParams params;
    memset(&params, 0, sizeof(params));

    if (featureIndex >= CALIB_MAX_FEATURES) {
        params.valid = false;
        return params;
    }

    params.mean = state.featureMeans[featureIndex];
    params.stdDev = state.featureStdDevs[featureIndex];
    params.min = state.featureMin[featureIndex];
    params.max = state.featureMax[featureIndex];
    params.valid = state.isCalibrated;

    return params;
}

void SelfCalibratingModel::setNormParams(size_t featureIndex, const NormalizationParams& params) {
    if (featureIndex >= CALIB_MAX_FEATURES) return;

    state.featureMeans[featureIndex] = params.mean;
    state.featureStdDevs[featureIndex] = params.stdDev;
    state.featureMin[featureIndex] = params.min;
    state.featureMax[featureIndex] = params.max;
}

void SelfCalibratingModel::forceCalibration(const float* means, const float* stdDevs, size_t count) {
    if (count > CALIB_MAX_FEATURES) count = CALIB_MAX_FEATURES;

    for (size_t i = 0; i < count; i++) {
        state.featureMeans[i] = means[i];
        state.featureStdDevs[i] = stdDevs[i];
    }

    featureCount = count;
    state.isCalibrated = true;
    computeThresholds();

    DEBUG_PRINTLN("Calibration forced with provided parameters");
}

void SelfCalibratingModel::computeThresholds() {
    for (size_t i = 0; i < featureCount; i++) {
        autoThresholds.baseline[i] = state.featureMeans[i];
        autoThresholds.warningLevel[i] = state.featureMeans[i] +
                                         thresholdConfig.warningMultiplier * state.featureStdDevs[i];
        autoThresholds.criticalLevel[i] = state.featureMeans[i] +
                                          thresholdConfig.criticalMultiplier * state.featureStdDevs[i];
    }
}

void SelfCalibratingModel::updateAutoThresholds() {
    if (thresholdConfig.useAdaptive) {
        computeThresholds();
    }
}

float SelfCalibratingModel::getWarningThreshold(size_t featureIndex) const {
    if (featureIndex >= CALIB_MAX_FEATURES) return 0.0f;
    return autoThresholds.warningLevel[featureIndex];
}

float SelfCalibratingModel::getCriticalThreshold(size_t featureIndex) const {
    if (featureIndex >= CALIB_MAX_FEATURES) return 0.0f;
    return autoThresholds.criticalLevel[featureIndex];
}

bool SelfCalibratingModel::isAboveWarning(float value, size_t featureIndex) const {
    if (featureIndex >= CALIB_MAX_FEATURES) return false;
    return value > autoThresholds.warningLevel[featureIndex];
}

bool SelfCalibratingModel::isAboveCritical(float value, size_t featureIndex) const {
    if (featureIndex >= CALIB_MAX_FEATURES) return false;
    return value > autoThresholds.criticalLevel[featureIndex];
}

float SelfCalibratingModel::computeZScore(float value, size_t featureIndex) {
    if (featureIndex >= CALIB_MAX_FEATURES) return 0.0f;

    float std = state.featureStdDevs[featureIndex];
    if (std < 0.0001f) return 0.0f;

    return (value - state.featureMeans[featureIndex]) / std;
}

void SelfCalibratingModel::updateQuantileEstimator(size_t featureIndex, float value) {
    if (featureIndex >= CALIB_MAX_FEATURES) return;

    addTDigestCentroid(featureIndex, value);

    if (quantiles[featureIndex].centroidCount >= CALIB_PERCENTILE_BINS) {
        compressTDigest(featureIndex);
    }
}

void SelfCalibratingModel::addTDigestCentroid(size_t featureIndex, float value) {
    QuantileEstimator& q = quantiles[featureIndex];

    if (q.centroidCount < CALIB_PERCENTILE_BINS) {
        q.centroids[q.centroidCount].mean = value;
        q.centroids[q.centroidCount].weight = 1.0f;
        q.centroidCount++;
        q.totalWeight += 1.0f;
    }
}

void SelfCalibratingModel::compressTDigest(size_t featureIndex) {
    QuantileEstimator& q = quantiles[featureIndex];

    for (size_t i = 0; i < q.centroidCount - 1; i++) {
        for (size_t j = 0; j < q.centroidCount - i - 1; j++) {
            if (q.centroids[j].mean > q.centroids[j + 1].mean) {
                TDigestCentroid temp = q.centroids[j];
                q.centroids[j] = q.centroids[j + 1];
                q.centroids[j + 1] = temp;
            }
        }
    }

    size_t newCount = 0;
    for (size_t i = 0; i < q.centroidCount; i += 2) {
        if (i + 1 < q.centroidCount) {
            float totalW = q.centroids[i].weight + q.centroids[i + 1].weight;
            float newMean = (q.centroids[i].mean * q.centroids[i].weight +
                            q.centroids[i + 1].mean * q.centroids[i + 1].weight) / totalW;
            q.centroids[newCount].mean = newMean;
            q.centroids[newCount].weight = totalW;
        } else {
            q.centroids[newCount] = q.centroids[i];
        }
        newCount++;
    }
    q.centroidCount = newCount;
}

float SelfCalibratingModel::queryTDigest(size_t featureIndex, float quantile) {
    QuantileEstimator& q = quantiles[featureIndex];

    if (q.centroidCount == 0) return 0.0f;

    float targetWeight = quantile * q.totalWeight;
    float cumWeight = 0.0f;

    for (size_t i = 0; i < q.centroidCount; i++) {
        cumWeight += q.centroids[i].weight;
        if (cumWeight >= targetWeight) {
            return q.centroids[i].mean;
        }
    }

    return q.centroids[q.centroidCount - 1].mean;
}

float SelfCalibratingModel::getPercentile(size_t featureIndex, float percentile) {
    return queryTDigest(featureIndex, percentile / 100.0f);
}

float SelfCalibratingModel::getConfidenceScore(const float* features, size_t count) {
    if (!state.isCalibrated) return 0.0f;

    float totalZScore = 0.0f;

    for (size_t i = 0; i < count && i < featureCount; i++) {
        float zScore = fabs(computeZScore(features[i], i));
        totalZScore += zScore;
    }

    float avgZScore = totalZScore / count;

    float confidence = 1.0f / (1.0f + exp(avgZScore - 2.0f));

    return confidence;
}

float SelfCalibratingModel::getOutlierScore(const float* features, size_t count) {
    if (!state.isCalibrated) return 0.0f;

    float maxZScore = 0.0f;

    for (size_t i = 0; i < count && i < featureCount; i++) {
        float zScore = fabs(computeZScore(features[i], i));
        if (zScore > maxZScore) maxZScore = zScore;
    }

    return fmin(maxZScore / 5.0f, 1.0f);
}

String SelfCalibratingModel::generateCalibrationReport() const {
    String report = "=== Calibration Report ===\n";
    report += "Calibrated: " + String(state.isCalibrated ? "Yes" : "No") + "\n";
    report += "Sample Count: " + String(state.sampleCount) + "\n";
    report += "Feature Count: " + String(featureCount) + "\n\n";

    for (size_t i = 0; i < featureCount && i < 10; i++) {
        report += "Feature " + String(i) + ":\n";
        report += "  Mean: " + String(state.featureMeans[i], 4) + "\n";
        report += "  StdDev: " + String(state.featureStdDevs[i], 4) + "\n";
        report += "  Range: [" + String(state.featureMin[i], 4) + ", ";
        report += String(state.featureMax[i], 4) + "]\n";
    }

    return report;
}

String SelfCalibratingModel::exportNormalizationJSON() const {
    String json = "{\"normalization\":{";
    json += "\"calibrated\":" + String(state.isCalibrated ? "true" : "false") + ",";
    json += "\"sampleCount\":" + String(state.sampleCount) + ",";
    json += "\"featureCount\":" + String(featureCount) + ",";
    json += "\"features\":[";

    for (size_t i = 0; i < featureCount; i++) {
        if (i > 0) json += ",";
        json += "{\"mean\":" + String(state.featureMeans[i], 6);
        json += ",\"std\":" + String(state.featureStdDevs[i], 6);
        json += ",\"min\":" + String(state.featureMin[i], 6);
        json += ",\"max\":" + String(state.featureMax[i], 6) + "}";
    }

    json += "]}}";
    return json;
}

String SelfCalibratingModel::exportNormalizationHeader() const {
    String header = "#ifndef NORM_PARAMS_H\n#define NORM_PARAMS_H\n\n";
    header += "#define NORM_FEATURE_COUNT " + String(featureCount) + "\n\n";

    header += "const float NORM_MEAN[] = {\n    ";
    for (size_t i = 0; i < featureCount; i++) {
        if (i > 0) header += ", ";
        header += String(state.featureMeans[i], 6) + "f";
    }
    header += "\n};\n\n";

    header += "const float NORM_STD[] = {\n    ";
    for (size_t i = 0; i < featureCount; i++) {
        if (i > 0) header += ", ";
        header += String(state.featureStdDevs[i], 6) + "f";
    }
    header += "\n};\n\n#endif\n";

    return header;
}

bool SelfCalibratingModel::saveCalibration(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = "CALB";
    data += String(featureCount) + ",";
    data += String(state.sampleCount) + ",";

    for (size_t i = 0; i < featureCount; i++) {
        data += String(state.featureMeans[i], 6) + ",";
        data += String(state.featureStdDevs[i], 6) + ",";
    }

    return storage.saveLog(filename, data);
}

bool SelfCalibratingModel::loadCalibration(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = storage.readLog(filename);
    if (data.length() < 10 || !data.startsWith("CALB")) return false;

    state.isCalibrated = true;
    return true;
}
