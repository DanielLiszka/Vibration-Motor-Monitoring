#include "DriftDetector.h"
#include "StorageManager.h"
#include <math.h>

const uint8_t DriftDetector::trackedFeatureIndices[DRIFT_TRACKED_FEATURES] = {
    0, 2, 4, 6, 9, 10, 13, 15
};

DriftDetector::DriftDetector()
    : driftThreshold(DRIFT_DEFAULT_THRESHOLD)
    , hasReference(false)
    , lastDriftTime(0)
    , samplesSinceReference(0)
    , usePageHinkley(true)
    , useCUSUM(true)
    , useADWIN(false)
    , historyIndex(0)
    , historyCount(0)
    , referencePredictionMean(0.0f)
    , referenceConfidenceMean(0.0f)
    , driftCallback(nullptr)
{
    memset(&currentMetrics, 0, sizeof(currentMetrics));
    memset(currentStats, 0, sizeof(currentStats));
    memset(referenceStats, 0, sizeof(referenceStats));
    memset(phStates, 0, sizeof(phStates));
    memset(cusumStates, 0, sizeof(cusumStates));
    memset(predictionHistory, 0, sizeof(predictionHistory));
    memset(confidenceHistory, 0, sizeof(confidenceHistory));
}

DriftDetector::~DriftDetector() {
}

bool DriftDetector::begin() {
    reset();
    DEBUG_PRINTLN("DriftDetector initialized");
    return true;
}

void DriftDetector::reset() {
    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        initializeStats(currentStats[i]);
        initializeStats(referenceStats[i]);

        phStates[i].sum = 0.0f;
        phStates[i].minSum = 0.0f;
        phStates[i].mean = 0.0f;
        phStates[i].sampleCount = 0;
        phStates[i].driftDetected = false;

        cusumStates[i].posSum = 0.0f;
        cusumStates[i].negSum = 0.0f;
        cusumStates[i].target = 0.0f;
        cusumStates[i].driftDetected = false;
    }

    memset(&currentMetrics, 0, sizeof(currentMetrics));
    hasReference = false;
    lastDriftTime = 0;
    samplesSinceReference = 0;
    historyIndex = 0;
    historyCount = 0;
}

void DriftDetector::initializeStats(FeatureStatistics& stats) {
    stats.mean = 0.0f;
    stats.variance = 0.0f;
    stats.min = INFINITY;
    stats.max = -INFINITY;
    stats.m2 = 0.0f;
    stats.count = 0;
}

void DriftDetector::updateWelfordStats(FeatureStatistics& stats, float value) {
    stats.count++;
    float delta = value - stats.mean;
    stats.mean += delta / stats.count;
    float delta2 = value - stats.mean;
    stats.m2 += delta * delta2;

    if (stats.count > 1) {
        stats.variance = stats.m2 / (stats.count - 1);
    }

    if (value < stats.min) stats.min = value;
    if (value > stats.max) stats.max = value;
}

void DriftDetector::updateStatistics(const FeatureVector& features) {
    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        float value = extractTrackedFeature(features, i);
        updateWelfordStats(currentStats[i], value);

        if (usePageHinkley) {
            updatePageHinkley(i, value);
        }
        if (useCUSUM) {
            updateCUSUM(i, value);
        }
    }

    samplesSinceReference++;
}

void DriftDetector::updateStatistics(const ExtendedFeatureVector& features) {
    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        float value = extractTrackedFeature(features, i);
        updateWelfordStats(currentStats[i], value);

        if (usePageHinkley) {
            updatePageHinkley(i, value);
        }
        if (useCUSUM) {
            updateCUSUM(i, value);
        }
    }

    samplesSinceReference++;
}

void DriftDetector::updateStatistics(const float* features, size_t count) {
    for (int i = 0; i < DRIFT_TRACKED_FEATURES && trackedFeatureIndices[i] < count; i++) {
        float value = features[trackedFeatureIndices[i]];
        updateWelfordStats(currentStats[i], value);

        if (usePageHinkley) {
            updatePageHinkley(i, value);
        }
        if (useCUSUM) {
            updateCUSUM(i, value);
        }
    }

    samplesSinceReference++;
}

float DriftDetector::extractTrackedFeature(const FeatureVector& features, uint8_t trackedIndex) {
    switch (trackedFeatureIndices[trackedIndex]) {
        case 0: return features.rms;
        case 2: return features.kurtosis;
        case 4: return features.crestFactor;
        case 6: return features.spectralCentroid;
        case 9: return features.dominantFrequency;
        default: return 0.0f;
    }
}

float DriftDetector::extractTrackedFeature(const ExtendedFeatureVector& features, uint8_t trackedIndex) {
    switch (trackedFeatureIndices[trackedIndex]) {
        case 0: return features.rms;
        case 2: return features.kurtosis;
        case 4: return features.crestFactor;
        case 6: return features.spectralCentroid;
        case 9: return features.dominantFrequency;
        case 10: return features.zeroCrossingRate;
        case 13: return features.spectralFlatness;
        case 15: return features.impulseFactor;
        default: return 0.0f;
    }
}

void DriftDetector::updatePageHinkley(uint8_t featureIndex, float value) {
    PageHinkleyState& ph = phStates[featureIndex];

    ph.sampleCount++;
    float delta = value - ph.mean - DRIFT_PAGE_HINKLEY_LAMBDA;
    ph.mean += (value - ph.mean) / ph.sampleCount;
    ph.sum += delta;

    if (ph.sum < ph.minSum) {
        ph.minSum = ph.sum;
    }

    float pht = ph.sum - ph.minSum;
    ph.driftDetected = (pht > DRIFT_PAGE_HINKLEY_THRESHOLD);
}

bool DriftDetector::checkPageHinkley(uint8_t featureIndex) {
    return phStates[featureIndex].driftDetected;
}

void DriftDetector::updateCUSUM(uint8_t featureIndex, float value) {
    CUSUMState& cusum = cusumStates[featureIndex];

    if (cusum.target == 0.0f && hasReference) {
        cusum.target = referenceStats[featureIndex].mean;
    }

    float deviation = value - cusum.target;
    cusum.posSum = fmax(0.0f, cusum.posSum + deviation - DRIFT_PAGE_HINKLEY_LAMBDA);
    cusum.negSum = fmax(0.0f, cusum.negSum - deviation - DRIFT_PAGE_HINKLEY_LAMBDA);

    cusum.driftDetected = (cusum.posSum > DRIFT_CUSUM_THRESHOLD) ||
                          (cusum.negSum > DRIFT_CUSUM_THRESHOLD);
}

bool DriftDetector::checkCUSUM(uint8_t featureIndex) {
    return cusumStates[featureIndex].driftDetected;
}

float DriftDetector::computeZScore(uint8_t featureIndex) {
    if (!hasReference) return 0.0f;

    float currentMean = currentStats[featureIndex].mean;
    float refMean = referenceStats[featureIndex].mean;
    float refStd = sqrt(referenceStats[featureIndex].variance + 0.0001f);

    return fabs(currentMean - refMean) / refStd;
}

float DriftDetector::computeKSStatistic(uint8_t featureIndex) {
    if (!hasReference || currentStats[featureIndex].count < 10) return 0.0f;

    float currentStd = sqrt(currentStats[featureIndex].variance + 0.0001f);
    float refStd = sqrt(referenceStats[featureIndex].variance + 0.0001f);

    float meanDiff = fabs(currentStats[featureIndex].mean - referenceStats[featureIndex].mean);
    float stdRatio = fmax(currentStd / (refStd + 0.0001f), refStd / (currentStd + 0.0001f));

    return meanDiff / (refStd + 0.0001f) + (stdRatio - 1.0f);
}

DriftType DriftDetector::detectDrift() {
    if (!hasReference || samplesSinceReference < DRIFT_WINDOW_SIZE) {
        currentMetrics.detectedType = DRIFT_NONE;
        return DRIFT_NONE;
    }

    uint8_t phDriftCount = 0;
    uint8_t cusumDriftCount = 0;
    float totalDrift = 0.0f;

    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        if (checkPageHinkley(i)) phDriftCount++;
        if (checkCUSUM(i)) cusumDriftCount++;

        float drift = computeZScore(i);
        currentMetrics.featureDrift[i] = drift;
        totalDrift += drift;
    }

    currentMetrics.overallDrift = totalDrift / DRIFT_TRACKED_FEATURES;

    identifyAffectedFeatures();

    currentMetrics.detectedType = classifyDriftType();
    currentMetrics.magnitude = currentMetrics.overallDrift;

    if (currentMetrics.detectedType != DRIFT_NONE) {
        currentMetrics.driftStartTime = millis();
        lastDriftTime = millis();

        if (driftCallback) {
            driftCallback(currentMetrics.detectedType, currentMetrics.magnitude,
                         currentMetrics.affectedFeatures, currentMetrics.affectedFeatureCount);
        }
    }

    return currentMetrics.detectedType;
}

DriftType DriftDetector::classifyDriftType() {
    uint8_t phCount = 0;
    uint8_t cusumCount = 0;
    float avgDrift = currentMetrics.overallDrift;

    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        if (phStates[i].driftDetected) phCount++;
        if (cusumStates[i].driftDetected) cusumCount++;
    }

    if (avgDrift < driftThreshold && phCount < 2 && cusumCount < 2) {
        return DRIFT_NONE;
    }

    if (avgDrift > driftThreshold * 3.0f && phCount > 4) {
        return DRIFT_SUDDEN;
    }

    if (avgDrift > driftThreshold * 1.5f && cusumCount > 3) {
        return DRIFT_GRADUAL;
    }

    if (cusumCount > 2 && phCount < 2) {
        return DRIFT_INCREMENTAL;
    }

    if (avgDrift > driftThreshold) {
        return DRIFT_GRADUAL;
    }

    return DRIFT_NONE;
}

void DriftDetector::identifyAffectedFeatures() {
    currentMetrics.affectedFeatureCount = 0;

    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        if (currentMetrics.featureDrift[i] > driftThreshold ||
            phStates[i].driftDetected || cusumStates[i].driftDetected) {
            currentMetrics.affectedFeatures[currentMetrics.affectedFeatureCount++] = i;
        }
    }
}

void DriftDetector::getAffectedFeatures(uint8_t* features, uint8_t& count) const {
    count = currentMetrics.affectedFeatureCount;
    memcpy(features, currentMetrics.affectedFeatures, count);
}

bool DriftDetector::isFeatureAffected(uint8_t featureIndex) const {
    for (int i = 0; i < currentMetrics.affectedFeatureCount; i++) {
        if (currentMetrics.affectedFeatures[i] == featureIndex) {
            return true;
        }
    }
    return false;
}

void DriftDetector::captureCurrentAsReference() {
    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        referenceStats[i] = currentStats[i];
        cusumStates[i].target = currentStats[i].mean;

        phStates[i].sum = 0.0f;
        phStates[i].minSum = 0.0f;
        phStates[i].driftDetected = false;

        cusumStates[i].posSum = 0.0f;
        cusumStates[i].negSum = 0.0f;
        cusumStates[i].driftDetected = false;
    }

    referencePredictionMean = 0.0f;
    for (int i = 0; i < historyCount; i++) {
        referencePredictionMean += predictionHistory[i];
        referenceConfidenceMean += confidenceHistory[i];
    }
    if (historyCount > 0) {
        referencePredictionMean /= historyCount;
        referenceConfidenceMean /= historyCount;
    }

    hasReference = true;
    samplesSinceReference = 0;
    DEBUG_PRINTLN("Reference distribution captured");
}

void DriftDetector::clearReference() {
    hasReference = false;
    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        initializeStats(referenceStats[i]);
    }
}

void DriftDetector::setReferenceWindow(const float* features, size_t windowSize, size_t featureCount) {
    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        initializeStats(referenceStats[i]);
    }

    for (size_t s = 0; s < windowSize; s++) {
        for (int i = 0; i < DRIFT_TRACKED_FEATURES && trackedFeatureIndices[i] < featureCount; i++) {
            float value = features[s * featureCount + trackedFeatureIndices[i]];
            updateWelfordStats(referenceStats[i], value);
        }
    }

    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        cusumStates[i].target = referenceStats[i].mean;
    }

    hasReference = true;
    samplesSinceReference = 0;
}

void DriftDetector::updatePredictionStats(uint8_t predictedClass, float confidence) {
    predictionHistory[historyIndex] = (float)predictedClass;
    confidenceHistory[historyIndex] = confidence;
    historyIndex = (historyIndex + 1) % DRIFT_WINDOW_SIZE;
    if (historyCount < DRIFT_WINDOW_SIZE) historyCount++;
}

float DriftDetector::getPredictionDrift() const {
    if (historyCount < 10 || referencePredictionMean == 0.0f) return 0.0f;

    float currentMean = 0.0f;
    for (int i = 0; i < historyCount; i++) {
        currentMean += predictionHistory[i];
    }
    currentMean /= historyCount;

    return fabs(currentMean - referencePredictionMean);
}

float DriftDetector::getConfidenceDrift() const {
    if (historyCount < 10 || referenceConfidenceMean == 0.0f) return 0.0f;

    float currentMean = 0.0f;
    for (int i = 0; i < historyCount; i++) {
        currentMean += confidenceHistory[i];
    }
    currentMean /= historyCount;

    return fabs(currentMean - referenceConfidenceMean);
}

FeatureStatistics DriftDetector::getFeatureStats(uint8_t featureIndex) const {
    if (featureIndex < DRIFT_TRACKED_FEATURES) {
        return currentStats[featureIndex];
    }
    FeatureStatistics empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

FeatureStatistics DriftDetector::getReferenceStats(uint8_t featureIndex) const {
    if (featureIndex < DRIFT_TRACKED_FEATURES) {
        return referenceStats[featureIndex];
    }
    FeatureStatistics empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

String DriftDetector::getDriftTypeName(DriftType type) const {
    switch (type) {
        case DRIFT_NONE: return "None";
        case DRIFT_GRADUAL: return "Gradual";
        case DRIFT_SUDDEN: return "Sudden";
        case DRIFT_RECURRING: return "Recurring";
        case DRIFT_INCREMENTAL: return "Incremental";
        default: return "Unknown";
    }
}

String DriftDetector::generateDriftReport() const {
    String report = "{\"drift\":{";
    report += "\"type\":\"" + getDriftTypeName(currentMetrics.detectedType) + "\",";
    report += "\"magnitude\":" + String(currentMetrics.magnitude, 4) + ",";
    report += "\"overall\":" + String(currentMetrics.overallDrift, 4) + ",";
    report += "\"affectedCount\":" + String(currentMetrics.affectedFeatureCount) + ",";
    report += "\"features\":[";

    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        if (i > 0) report += ",";
        report += String(currentMetrics.featureDrift[i], 4);
    }

    report += "],\"hasReference\":" + String(hasReference ? "true" : "false");
    report += ",\"samplesSinceRef\":" + String(samplesSinceReference);
    report += "}}";

    return report;
}

bool DriftDetector::saveState(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = "DRFT";
    data += String(hasReference ? "1" : "0") + ",";
    data += String(driftThreshold, 4) + ",";
    data += String(samplesSinceReference) + ",";

    for (int i = 0; i < DRIFT_TRACKED_FEATURES; i++) {
        data += String(referenceStats[i].mean, 6) + ",";
        data += String(referenceStats[i].variance, 6) + ",";
    }

    return storage.saveLog(filename, data);
}

bool DriftDetector::loadState(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = storage.readLog(filename);
    if (data.length() < 10 || !data.startsWith("DRFT")) return false;

    return true;
}
