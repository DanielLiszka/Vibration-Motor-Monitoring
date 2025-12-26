#include "FaultDetector.h"
#include "StorageManager.h"
#include <math.h>
#include <float.h>

FaultDetector::FaultDetector()
    : warningThreshold(THRESHOLD_MULTIPLIER_WARNING)
    , criticalThreshold(THRESHOLD_MULTIPLIER_CRITICAL)
    , calibrating(false)
    , calibrationTarget(0)
    , calibrationCount(0)
{
    baseline.init();
}

FaultDetector::~FaultDetector() {
}

bool FaultDetector::begin() {
    DEBUG_PRINTLN("Initializing Fault Detector...");
    reset();

    if (loadBaseline()) {
        DEBUG_PRINTLN("Baseline loaded from storage");
    } else {
        DEBUG_PRINTLN("No baseline found - calibration required");
    }

    DEBUG_PRINTLN("Fault Detector initialized");
    return true;
}

void FaultDetector::startCalibration(uint32_t numSamples) {
    DEBUG_PRINTF("Starting calibration with %d samples\n", numSamples);

    calibrating = true;
    calibrationTarget = numSamples;
    calibrationCount = 0;

    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        calibrationSums[i] = 0.0f;
        calibrationSumSquares[i] = 0.0f;
        baseline.min[i] = FLT_MAX;
        baseline.max[i] = -FLT_MAX;
    }

    baseline.sampleCount = 0;
    baseline.isCalibrated = false;
}

bool FaultDetector::addCalibrationSample(const FeatureVector& features) {
    if (!calibrating) {
        DEBUG_PRINTLN("Not in calibration mode");
        return false;
    }

    float featureArray[NUM_TOTAL_FEATURES];
    features.toArray(featureArray);

    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        float value = featureArray[i];
        calibrationSums[i] += value;
        calibrationSumSquares[i] += value * value;

        if (value < baseline.min[i]) baseline.min[i] = value;
        if (value > baseline.max[i]) baseline.max[i] = value;
    }

    calibrationCount++;
    baseline.sampleCount = calibrationCount;

    if (calibrationCount >= calibrationTarget) {
        finalizeCalibration();
        return true;
    }

    return false;
}

void FaultDetector::finalizeCalibration() {
    DEBUG_PRINTLN("Finalizing calibration...");

    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        baseline.mean[i] = calibrationSums[i] / calibrationCount;

        float variance = (calibrationSumSquares[i] / calibrationCount)
                       - (baseline.mean[i] * baseline.mean[i]);

        baseline.stdDev[i] = sqrt(fabs(variance));

        if (baseline.stdDev[i] < 0.0001f) {
            baseline.stdDev[i] = 0.0001f;
        }

        DEBUG_PRINTF("Feature %d: mean=%.4f, stdDev=%.4f, min=%.4f, max=%.4f\n",
                     i, baseline.mean[i], baseline.stdDev[i],
                     baseline.min[i], baseline.max[i]);
    }

    baseline.isCalibrated = true;
    calibrating = false;

    DEBUG_PRINTLN("Calibration complete!");

    saveBaseline();
}

void FaultDetector::setBaseline(const BaselineStats& baseline) {
    this->baseline = baseline;
}

bool FaultDetector::detectFault(const FeatureVector& features, FaultResult& result) {
    result.reset();
    result.timestamp = millis();

    if (!baseline.isCalibrated) {
        result.description = "System not calibrated";
        return false;
    }

    result.anomalyScore = calculateAnomalyScore(features);

    if (result.anomalyScore > criticalThreshold) {
        result.severity = SEVERITY_CRITICAL;
    } else if (result.anomalyScore > warningThreshold) {
        result.severity = SEVERITY_WARNING;
    } else {
        result.severity = SEVERITY_NORMAL;
    }

    if (result.severity != SEVERITY_NORMAL) {
        classifyFault(features, result);
        return true;
    }

    result.type = FAULT_NONE;
    result.description = "Normal operation";
    result.confidence = 1.0f;
    return false;
}

float FaultDetector::calculateAnomalyScore(const FeatureVector& features) {

    float featureArray[NUM_TOTAL_FEATURES];
    features.toArray(featureArray);

    float sumSquaredDist = 0.0f;

    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        float zScore = calculateZScore(featureArray[i], i);
        sumSquaredDist += zScore * zScore;
    }

    return sqrt(sumSquaredDist / NUM_TOTAL_FEATURES);
}

void FaultDetector::classifyFault(const FeatureVector& features, FaultResult& result) {

    result.type = classifyByRules(features);

    result.confidence = min(result.anomalyScore / 10.0f, 1.0f);

    switch(result.type) {
        case FAULT_IMBALANCE:
            result.description = "Rotor imbalance detected - check for uneven weight distribution";
            break;
        case FAULT_MISALIGNMENT:
            result.description = "Shaft misalignment detected - check coupling alignment";
            break;
        case FAULT_BEARING:
            result.description = "Bearing fault detected - inspect bearing condition";
            break;
        case FAULT_LOOSENESS:
            result.description = "Mechanical looseness detected - check mounting bolts";
            break;
        case FAULT_UNKNOWN:
        default:
            result.description = "Abnormal vibration pattern - requires inspection";
            break;
    }
}

void FaultDetector::setThresholds(float warningMultiplier, float criticalMultiplier) {
    warningThreshold = warningMultiplier;
    criticalThreshold = criticalMultiplier;
    DEBUG_PRINTF("Thresholds set: Warning=%.2f, Critical=%.2f\n",
                 warningThreshold, criticalThreshold);
}

void FaultDetector::reset() {
    baseline.init();
    calibrating = false;
    calibrationTarget = 0;
    calibrationCount = 0;
}

bool FaultDetector::saveBaseline() {
    StorageManager storage;
    if (!storage.begin()) {
        DEBUG_PRINTLN("Failed to initialize storage for baseline save");
        return false;
    }

    if (storage.saveBaseline(baseline)) {
        DEBUG_PRINTLN("Baseline saved successfully");
        return true;
    }

    DEBUG_PRINTLN("Failed to save baseline");
    return false;
}

bool FaultDetector::loadBaseline() {
    StorageManager storage;
    if (!storage.begin()) {
        DEBUG_PRINTLN("Failed to initialize storage for baseline load");
        return false;
    }

    if (storage.loadBaseline(baseline)) {
        DEBUG_PRINTLN("Baseline loaded successfully");
        return true;
    }

    DEBUG_PRINTLN("No baseline found in storage");
    return false;
}

float FaultDetector::calculateZScore(float value, int featureIndex) {
    if (featureIndex < 0 || featureIndex >= NUM_TOTAL_FEATURES) {
        return 0.0f;
    }

    float mean = baseline.mean[featureIndex];
    float stdDev = baseline.stdDev[featureIndex];

    if (stdDev == 0.0f) return 0.0f;

    return (value - mean) / stdDev;
}

FaultType FaultDetector::classifyByRules(const FeatureVector& features) {

    if (checkBearing(features)) {
        return FAULT_BEARING;
    }

    if (checkImbalance(features)) {
        return FAULT_IMBALANCE;
    }

    if (checkMisalignment(features)) {
        return FAULT_MISALIGNMENT;
    }

    if (checkLooseness(features)) {
        return FAULT_LOOSENESS;
    }

    return FAULT_UNKNOWN;
}

bool FaultDetector::checkImbalance(const FeatureVector& features) {

    bool highLowFreq = (features.dominantFrequency >= BAND_1_MIN &&
                       features.dominantFrequency <= BAND_1_MAX);

    bool lowKurtosis = (features.kurtosis < 1.0f);

    bool highBandPowerRatio = (features.bandPowerRatio > 2.0f);

    return highLowFreq && (lowKurtosis || highBandPowerRatio);
}

bool FaultDetector::checkMisalignment(const FeatureVector& features) {

    bool moderateFreq = (features.dominantFrequency > 10.0f &&
                        features.dominantFrequency < 25.0f);

    bool elevatedRMS = (calculateZScore(features.rms, FEAT_RMS) > 2.0f);

    return moderateFreq && elevatedRMS;
}

bool FaultDetector::checkBearing(const FeatureVector& features) {

    bool highFreq = (features.dominantFrequency >= BAND_3_MIN);

    bool highKurtosis = (features.kurtosis > 3.0f);

    bool highCrestFactor = (features.crestFactor > 4.0f);

    bool lowBandPowerRatio = (features.bandPowerRatio < 0.5f);

    int score = (highFreq ? 1 : 0) + (highKurtosis ? 1 : 0) +
                (highCrestFactor ? 1 : 0) + (lowBandPowerRatio ? 1 : 0);

    return score >= 2;
}

bool FaultDetector::checkLooseness(const FeatureVector& features) {

    bool highVariance = (calculateZScore(features.variance, FEAT_VARIANCE) > 2.5f);

    bool highSpectralSpread = (features.spectralSpread > 15.0f);

    bool highCrestFactor = (features.crestFactor > 3.5f);

    int score = (highVariance ? 1 : 0) + (highSpectralSpread ? 1 : 0) +
                (highCrestFactor ? 1 : 0);

    return score >= 2;
}
