#include "EdgeRULEstimator.h"
#include "StorageManager.h"
#include <math.h>

EdgeRULEstimator::EdgeRULEstimator()
    : historyIndex(0)
    , historyCount(0)
    , currentHealthIndex(1.0f)
    , currentStage(STAGE_HEALTHY)
    , previousStage(STAGE_HEALTHY)
    , operatingHours(0.0f)
    , baselineHealth(1.0f)
    , failureThreshold(RUL_FAILURE_THRESHOLD)
    , healthyThreshold(RUL_HEALTHY_THRESHOLD)
    , lastUpdateTime(0)
    , lastEstimationTime(0)
    , rulCallback(nullptr)
{
    memset(history, 0, sizeof(history));
    memset(&lastEstimate, 0, sizeof(lastEstimate));
    memset(&modelParams, 0, sizeof(modelParams));
    memset(&rlsState, 0, sizeof(rlsState));
    memset(&degradationTrend, 0, sizeof(degradationTrend));
}

EdgeRULEstimator::~EdgeRULEstimator() {
}

bool EdgeRULEstimator::begin() {
    reset();
    initializeRLS();
    DEBUG_PRINTLN("EdgeRULEstimator initialized");
    return true;
}

void EdgeRULEstimator::reset() {
    historyIndex = 0;
    historyCount = 0;
    currentHealthIndex = 1.0f;
    currentStage = STAGE_HEALTHY;
    previousStage = STAGE_HEALTHY;
    operatingHours = 0.0f;
    lastUpdateTime = 0;
    lastEstimationTime = 0;

    memset(history, 0, sizeof(history));
    memset(&lastEstimate, 0, sizeof(lastEstimate));
    memset(&modelParams, 0, sizeof(modelParams));
    memset(&degradationTrend, 0, sizeof(degradationTrend));

    initializeRLS();
}

void EdgeRULEstimator::initializeRLS() {
    rlsState.forgettingFactor = 0.98f;

    for (int i = 0; i < 3; i++) {
        rlsState.theta[i] = 0.0f;
        for (int j = 0; j < 3; j++) {
            rlsState.P[i][j] = (i == j) ? 1000.0f : 0.0f;
        }
    }

    rlsState.theta[0] = 1.0f;
    rlsState.theta[1] = -0.001f;
    rlsState.theta[2] = 0.0f;
}

void EdgeRULEstimator::updateHealthIndex(const FeatureVector& features, float anomalyScore) {
    float health = computeHealthIndex(features, anomalyScore);
    updateHealthIndex(health, anomalyScore, 0);
}

void EdgeRULEstimator::updateHealthIndex(const ExtendedFeatureVector& features, float anomalyScore) {
    float health = computeHealthIndex(features, anomalyScore);
    updateHealthIndex(health, anomalyScore, 0);
}

void EdgeRULEstimator::updateHealthIndex(float healthValue, float anomalyScore, uint8_t faultClass) {
    addToHistory(healthValue, anomalyScore, faultClass);

    float alpha = 0.1f;
    currentHealthIndex = alpha * healthValue + (1.0f - alpha) * currentHealthIndex;

    if (currentHealthIndex < 0.0f) currentHealthIndex = 0.0f;
    if (currentHealthIndex > 1.0f) currentHealthIndex = 1.0f;

    previousStage = currentStage;
    classifyStage();

    uint32_t now = millis();
    if (now - lastEstimationTime >= RUL_UPDATE_INTERVAL_MS) {
        if (historyCount >= RUL_MIN_SAMPLES_FOR_ESTIMATION) {
            updateExponentialFit();
            updateDegradationTrend();
            estimateRUL();
        }
        lastEstimationTime = now;
    }

    if (currentStage != previousStage && rulCallback) {
        rulCallback(lastEstimate, previousStage);
    }

    lastUpdateTime = now;
}

float EdgeRULEstimator::computeHealthIndex(const FeatureVector& features, float anomalyScore) {
    float anomalyComponent = 1.0f - fmin(anomalyScore / 10.0f, 1.0f);

    float rmsNorm = fmin(features.rms / 5.0f, 1.0f);
    float kurtosisNorm = fmin(features.kurtosis / 10.0f, 1.0f);
    float crestNorm = fmin(features.crestFactor / 10.0f, 1.0f);

    float featureComponent = 1.0f - (0.4f * rmsNorm + 0.3f * kurtosisNorm + 0.3f * crestNorm);

    float health = 0.6f * anomalyComponent + 0.4f * featureComponent;

    return fmax(0.0f, fmin(1.0f, health));
}

float EdgeRULEstimator::computeHealthIndex(const ExtendedFeatureVector& features, float anomalyScore) {
    float anomalyComponent = 1.0f - fmin(anomalyScore / 10.0f, 1.0f);

    float rmsNorm = fmin(features.rms / 5.0f, 1.0f);
    float kurtosisNorm = fmin(features.kurtosis / 10.0f, 1.0f);
    float crestNorm = fmin(features.crestFactor / 10.0f, 1.0f);
    float impulseNorm = fmin(features.impulseFactor / 10.0f, 1.0f);

    float featureComponent = 1.0f - (0.3f * rmsNorm + 0.25f * kurtosisNorm +
                                     0.25f * crestNorm + 0.2f * impulseNorm);

    float health = 0.5f * anomalyComponent + 0.5f * featureComponent;

    return fmax(0.0f, fmin(1.0f, health));
}

void EdgeRULEstimator::addToHistory(float healthValue, float anomalyScore, uint8_t faultClass) {
    history[historyIndex].value = healthValue;
    history[historyIndex].timestamp = millis();
    history[historyIndex].faultClass = faultClass;
    history[historyIndex].anomalyScore = anomalyScore;

    historyIndex = (historyIndex + 1) % RUL_HISTORY_SIZE;
    if (historyCount < RUL_HISTORY_SIZE) {
        historyCount++;
    }
}

void EdgeRULEstimator::updateExponentialFit() {
    if (historyCount < RUL_MIN_SAMPLES_FOR_ESTIMATION) {
        modelParams.valid = false;
        return;
    }

    for (size_t i = 0; i < historyCount; i++) {
        size_t idx = (historyIndex - historyCount + i + RUL_HISTORY_SIZE) % RUL_HISTORY_SIZE;
        float t = (float)i / historyCount;
        float y = history[idx].value;
        updateRLS(t, y);
    }

    modelParams.a = rlsState.theta[0];
    modelParams.b = rlsState.theta[1];
    modelParams.c = rlsState.theta[2];
    modelParams.rSquared = computeRSquared();
    modelParams.valid = modelParams.rSquared > 0.5f;
}

void EdgeRULEstimator::updateRLS(float t, float y) {
    float phi[3];
    phi[0] = exp(t * 10.0f);
    phi[1] = t;
    phi[2] = 1.0f;

    float y_pred = 0.0f;
    for (int i = 0; i < 3; i++) {
        y_pred += rlsState.theta[i] * phi[i];
    }
    float error = y - y_pred;

    float Pphi[3] = {0};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            Pphi[i] += rlsState.P[i][j] * phi[j];
        }
    }

    float denom = rlsState.forgettingFactor;
    for (int i = 0; i < 3; i++) {
        denom += phi[i] * Pphi[i];
    }

    if (fabs(denom) < 0.0001f) return;

    float K[3];
    for (int i = 0; i < 3; i++) {
        K[i] = Pphi[i] / denom;
    }

    for (int i = 0; i < 3; i++) {
        rlsState.theta[i] += K[i] * error;
    }

    float KphiT[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            KphiT[i][j] = K[i] * phi[j];
        }
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 3; k++) {
                sum += KphiT[i][k] * rlsState.P[k][j];
            }
            rlsState.P[i][j] = (rlsState.P[i][j] - sum) / rlsState.forgettingFactor;
        }
    }
}

float EdgeRULEstimator::exponentialModel(float t) {
    return modelParams.a * exp(modelParams.b * t) + modelParams.c;
}

float EdgeRULEstimator::computeRSquared() {
    if (historyCount < 2) return 0.0f;

    float sumY = 0.0f;
    for (size_t i = 0; i < historyCount; i++) {
        size_t idx = (historyIndex - historyCount + i + RUL_HISTORY_SIZE) % RUL_HISTORY_SIZE;
        sumY += history[idx].value;
    }
    float meanY = sumY / historyCount;

    float ssTot = 0.0f;
    float ssRes = 0.0f;

    for (size_t i = 0; i < historyCount; i++) {
        size_t idx = (historyIndex - historyCount + i + RUL_HISTORY_SIZE) % RUL_HISTORY_SIZE;
        float y = history[idx].value;
        float t = (float)i / historyCount;
        float yPred = exponentialModel(t);

        ssTot += (y - meanY) * (y - meanY);
        ssRes += (y - yPred) * (y - yPred);
    }

    if (ssTot < 0.0001f) return 0.0f;

    return 1.0f - (ssRes / ssTot);
}

void EdgeRULEstimator::updateDegradationTrend() {
    if (historyCount < 24) return;

    size_t samplesPerDay = historyCount / 7;
    if (samplesPerDay < 1) samplesPerDay = 1;

    for (int d = 0; d < 7; d++) {
        size_t startIdx = d * samplesPerDay;
        size_t endIdx = (d + 1) * samplesPerDay;
        if (endIdx > historyCount) endIdx = historyCount;

        if (endIdx - startIdx < 2) {
            degradationTrend.dailySlopes[d] = 0.0f;
            continue;
        }

        float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;
        size_t n = 0;

        for (size_t i = startIdx; i < endIdx; i++) {
            size_t idx = (historyIndex - historyCount + i + RUL_HISTORY_SIZE) % RUL_HISTORY_SIZE;
            float x = (float)(i - startIdx);
            float y = history[idx].value;

            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumX2 += x * x;
            n++;
        }

        float denom = n * sumX2 - sumX * sumX;
        if (fabs(denom) > 0.0001f) {
            degradationTrend.dailySlopes[d] = (n * sumXY - sumX * sumY) / denom;
        } else {
            degradationTrend.dailySlopes[d] = 0.0f;
        }
    }

    float avgSlope = 0.0f;
    for (int d = 0; d < 7; d++) {
        avgSlope += degradationTrend.dailySlopes[d];
    }
    degradationTrend.weeklyTrend = avgSlope / 7.0f;

    float recentSlope = (degradationTrend.dailySlopes[5] + degradationTrend.dailySlopes[6]) / 2.0f;
    float earlySlope = (degradationTrend.dailySlopes[0] + degradationTrend.dailySlopes[1]) / 2.0f;

    degradationTrend.accelerationFactor = recentSlope - earlySlope;
    degradationTrend.isAccelerating = degradationTrend.accelerationFactor < -0.01f;
    degradationTrend.isDeteriorating = degradationTrend.weeklyTrend < -0.005f;
}

void EdgeRULEstimator::classifyStage() {
    if (currentHealthIndex >= healthyThreshold) {
        currentStage = STAGE_HEALTHY;
    } else if (currentHealthIndex >= 0.5f) {
        currentStage = STAGE_EARLY_DEGRADATION;
    } else if (currentHealthIndex >= failureThreshold) {
        currentStage = STAGE_ACCELERATED_DEGRADATION;
    } else {
        currentStage = STAGE_NEAR_FAILURE;
    }
}

RULEstimate EdgeRULEstimator::estimateRUL() {
    lastEstimate.healthIndex = currentHealthIndex * 100.0f;
    lastEstimate.currentStage = currentStage;
    lastEstimate.degradationRate = degradationTrend.weeklyTrend;
    lastEstimate.lastUpdateTime = millis();

    if (!modelParams.valid || historyCount < RUL_MIN_SAMPLES_FOR_ESTIMATION) {
        lastEstimate.estimatedHoursRemaining = -1.0f;
        lastEstimate.valid = false;
        return lastEstimate;
    }

    float degradationRate = -degradationTrend.weeklyTrend;
    if (degradationRate <= 0.0001f) {
        lastEstimate.estimatedHoursRemaining = 10000.0f;
        lastEstimate.lowerBound = 5000.0f;
        lastEstimate.upperBound = 20000.0f;
        lastEstimate.confidenceInterval = 0.3f;
        lastEstimate.valid = true;
        return lastEstimate;
    }

    float healthRemaining = currentHealthIndex - failureThreshold;
    if (healthRemaining <= 0.0f) {
        lastEstimate.estimatedHoursRemaining = 0.0f;
        lastEstimate.lowerBound = 0.0f;
        lastEstimate.upperBound = 0.0f;
        lastEstimate.confidenceInterval = 1.0f;
        lastEstimate.valid = true;
        return lastEstimate;
    }

    float hoursPerSample = 0.25f;
    float rulHours = (healthRemaining / degradationRate) * hoursPerSample * historyCount / 7.0f;

    lastEstimate.estimatedHoursRemaining = rulHours;
    lastEstimate.confidenceInterval = computeConfidenceInterval();
    lastEstimate.lowerBound = rulHours * (1.0f - lastEstimate.confidenceInterval);
    lastEstimate.upperBound = rulHours * (1.0f + lastEstimate.confidenceInterval);
    lastEstimate.valid = true;

    return lastEstimate;
}

float EdgeRULEstimator::computeConfidenceInterval() {
    float baseConfidence = 0.5f;

    if (modelParams.valid) {
        baseConfidence -= 0.2f * modelParams.rSquared;
    }

    if (historyCount > RUL_MIN_SAMPLES_FOR_ESTIMATION * 2) {
        baseConfidence -= 0.1f;
    }

    if (degradationTrend.isAccelerating) {
        baseConfidence += 0.1f;
    }

    return fmax(0.1f, fmin(0.8f, baseConfidence));
}

float EdgeRULEstimator::predictHealthAt(float hoursAhead) {
    if (!modelParams.valid) return currentHealthIndex;

    float degradationRate = -degradationTrend.weeklyTrend;
    float predictedHealth = currentHealthIndex - (degradationRate * hoursAhead / 168.0f);

    return fmax(0.0f, fmin(1.0f, predictedHealth));
}

float EdgeRULEstimator::getTimeToThreshold(float threshold) {
    if (!modelParams.valid) return -1.0f;

    float degradationRate = -degradationTrend.weeklyTrend;
    if (degradationRate <= 0.0001f) return 10000.0f;

    float healthDiff = currentHealthIndex - threshold;
    if (healthDiff <= 0.0f) return 0.0f;

    return (healthDiff / degradationRate) * 168.0f;
}

void EdgeRULEstimator::incrementOperatingHours(float deltaHours) {
    operatingHours += deltaHours;
}

void EdgeRULEstimator::calibrateThresholds(const float* healthyBaseline, size_t count) {
    if (count == 0) return;

    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        sum += healthyBaseline[i];
    }

    baselineHealth = sum / count;

    healthyThreshold = baselineHealth * 0.9f;
    failureThreshold = baselineHealth * 0.2f;
}

HealthIndicator EdgeRULEstimator::getHistoryEntry(size_t index) const {
    if (index >= historyCount) {
        HealthIndicator empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }

    size_t idx = (historyIndex - historyCount + index + RUL_HISTORY_SIZE) % RUL_HISTORY_SIZE;
    return history[idx];
}

void EdgeRULEstimator::clearHistory() {
    historyIndex = 0;
    historyCount = 0;
    memset(history, 0, sizeof(history));
}

String EdgeRULEstimator::getStageName(DegradationStage stage) const {
    switch (stage) {
        case STAGE_HEALTHY: return "Healthy";
        case STAGE_EARLY_DEGRADATION: return "Early Degradation";
        case STAGE_ACCELERATED_DEGRADATION: return "Accelerated Degradation";
        case STAGE_NEAR_FAILURE: return "Near Failure";
        default: return "Unknown";
    }
}

String EdgeRULEstimator::generateRULReport() const {
    String report = "=== RUL Estimation Report ===\n";
    report += "Health Index: " + String(currentHealthIndex * 100.0f, 1) + "%\n";
    report += "Stage: " + getStageName(currentStage) + "\n";
    report += "Operating Hours: " + String(operatingHours, 1) + "\n";

    if (lastEstimate.valid) {
        report += "Estimated RUL: " + String(lastEstimate.estimatedHoursRemaining, 1) + " hours\n";
        report += "Confidence: ±" + String(lastEstimate.confidenceInterval * 100.0f, 1) + "%\n";
        report += "Range: [" + String(lastEstimate.lowerBound, 1) + " - ";
        report += String(lastEstimate.upperBound, 1) + "] hours\n";
    }

    report += "Weekly Trend: " + String(degradationTrend.weeklyTrend * 100.0f, 3) + "%/week\n";
    report += "Accelerating: " + String(degradationTrend.isAccelerating ? "Yes" : "No") + "\n";

    return report;
}

String EdgeRULEstimator::generateHealthJSON() const {
    String json = "{\"rul\":{";
    json += "\"healthIndex\":" + String(currentHealthIndex * 100.0f, 2) + ",";
    json += "\"stage\":\"" + getStageName(currentStage) + "\",";
    json += "\"stageCode\":" + String((int)currentStage) + ",";
    json += "\"operatingHours\":" + String(operatingHours, 1) + ",";

    if (lastEstimate.valid) {
        json += "\"estimatedRUL\":" + String(lastEstimate.estimatedHoursRemaining, 1) + ",";
        json += "\"lowerBound\":" + String(lastEstimate.lowerBound, 1) + ",";
        json += "\"upperBound\":" + String(lastEstimate.upperBound, 1) + ",";
        json += "\"confidence\":" + String(lastEstimate.confidenceInterval, 3) + ",";
    }

    json += "\"degradationRate\":" + String(degradationTrend.weeklyTrend, 5) + ",";
    json += "\"isAccelerating\":" + String(degradationTrend.isAccelerating ? "true" : "false") + ",";
    json += "\"historyCount\":" + String(historyCount) + ",";
    json += "\"modelValid\":" + String(modelParams.valid ? "true" : "false");

    json += "}}";
    return json;
}

bool EdgeRULEstimator::saveState(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = "RULE";
    data += String(currentHealthIndex, 6) + ",";
    data += String(operatingHours, 2) + ",";
    data += String(baselineHealth, 6) + ",";
    data += String(failureThreshold, 6) + ",";
    data += String(healthyThreshold, 6) + ",";

    return storage.saveLog(filename, data);
}

bool EdgeRULEstimator::loadState(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = storage.readLog(filename);
    if (data.length() < 10 || !data.startsWith("RULE")) return false;

    return true;
}
