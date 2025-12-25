#include "OnlineLearner.h"
#include "StorageManager.h"
#include <math.h>

OnlineLearner::OnlineLearner()
    : lossIndex(0)
    , lossCount(0)
    , initialized(false)
    , useMomentum(true)
    , useReservoirReplay(true)
    , updateCount(0)
    , correctPredictions(0)
    , totalPredictions(0)
    , updateCallback(nullptr)
{
    memset(&state, 0, sizeof(state));
    memset(reservoir, 0, sizeof(reservoir));
    memset(lossHistory, 0, sizeof(lossHistory));

    selectedIndices[0] = 0;
    selectedIndices[1] = 1;
    selectedIndices[2] = 2;
    selectedIndices[3] = 3;
    selectedIndices[4] = 4;
    selectedIndices[5] = 5;
    selectedIndices[6] = 6;
    selectedIndices[7] = 7;
    selectedIndices[8] = 8;
    selectedIndices[9] = 9;
    selectedIndices[10] = 10;
    selectedIndices[11] = 11;
    selectedIndices[12] = 12;
    selectedIndices[13] = 13;
    selectedIndices[14] = 14;
    selectedIndices[15] = 15;
}

OnlineLearner::~OnlineLearner() {
}

bool OnlineLearner::begin() {
    initializeWeights();
    state.adaptiveRate = ONLINE_DEFAULT_LR;
    state.samplesSeen = 0;

    for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
        state.runningMean[i] = 0.0f;
        state.runningVariance[i] = 1.0f;
    }

    clearReservoir();
    initialized = true;
    DEBUG_PRINTLN("OnlineLearner initialized");
    return true;
}

void OnlineLearner::reset() {
    initializeWeights();
    state.adaptiveRate = ONLINE_DEFAULT_LR;
    state.samplesSeen = 0;
    clearReservoir();
    lossIndex = 0;
    lossCount = 0;
    updateCount = 0;
    correctPredictions = 0;
    totalPredictions = 0;
}

void OnlineLearner::initializeWeights() {
    for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
        for (int j = 0; j < ONLINE_OUTPUT_CLASSES; j++) {
            float randVal = ((float)random(1000) / 1000.0f) - 0.5f;
            state.weights[i][j] = randVal * 0.1f;
            state.momentum[i][j] = 0.0f;
        }
    }

    for (int j = 0; j < ONLINE_OUTPUT_CLASSES; j++) {
        state.bias[j] = 0.0f;
        state.biasMomentum[j] = 0.0f;
    }
}

OnlinePrediction OnlineLearner::predict(const float* features, size_t featureCount) {
    OnlinePrediction pred;
    memset(&pred, 0, sizeof(pred));

    if (!initialized || featureCount < ONLINE_FEATURE_DIM) {
        return pred;
    }

    float normalizedFeatures[ONLINE_FEATURE_DIM];
    normalizeFeatures(features, normalizedFeatures, ONLINE_FEATURE_DIM);

    forward(normalizedFeatures, pred.classScores);

    float maxScore = pred.classScores[0];
    pred.predictedClass = 0;
    for (int i = 1; i < ONLINE_OUTPUT_CLASSES; i++) {
        if (pred.classScores[i] > maxScore) {
            maxScore = pred.classScores[i];
            pred.predictedClass = i;
        }
    }

    pred.confidence = maxScore;

    float entropy = 0.0f;
    for (int i = 0; i < ONLINE_OUTPUT_CLASSES; i++) {
        if (pred.classScores[i] > 0.0001f) {
            entropy -= pred.classScores[i] * log(pred.classScores[i]);
        }
    }
    float maxEntropy = log((float)ONLINE_OUTPUT_CLASSES);
    pred.uncertainty = entropy / maxEntropy;

    totalPredictions++;
    return pred;
}

OnlinePrediction OnlineLearner::predict(const FeatureVector& features) {
    float selectedFeatures[ONLINE_FEATURE_DIM];
    selectFeatures(features, selectedFeatures);
    return predict(selectedFeatures, ONLINE_FEATURE_DIM);
}

void OnlineLearner::selectFeatures(const FeatureVector& fullFeatures, float* selectedFeatures) {
    float allFeatures[10] = {
        fullFeatures.rms,
        fullFeatures.peakToPeak,
        fullFeatures.kurtosis,
        fullFeatures.skewness,
        fullFeatures.crestFactor,
        fullFeatures.variance,
        fullFeatures.spectralCentroid,
        fullFeatures.spectralSpread,
        fullFeatures.bandPowerRatio,
        fullFeatures.dominantFrequency
    };

    for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
        uint8_t idx = selectedIndices[i];
        if (idx < 10) {
            selectedFeatures[i] = allFeatures[idx];
        } else {
            selectedFeatures[i] = 0.0f;
        }
    }
}

void OnlineLearner::setSelectedFeatureIndices(const uint8_t* indices, size_t count) {
    size_t copyCount = (count < ONLINE_FEATURE_DIM) ? count : ONLINE_FEATURE_DIM;
    memcpy(selectedIndices, indices, copyCount);
}

void OnlineLearner::updateOnSample(const float* features, uint8_t label, float confidence) {
    if (!initialized || label >= ONLINE_OUTPUT_CLASSES) return;

    float normalizedFeatures[ONLINE_FEATURE_DIM];
    normalizeFeatures(features, normalizedFeatures, ONLINE_FEATURE_DIM);

    updateRunningStats(features, ONLINE_FEATURE_DIM);

    float output[ONLINE_OUTPUT_CLASSES];
    forward(normalizedFeatures, output);

    float loss = crossEntropyLoss(output, label);
    recordLoss(loss);

    backward(normalizedFeatures, output, label);

    if (useReservoirReplay && state.samplesSeen % 10 == 0) {
        replayFromReservoir(3);
    }

    addToReservoir(features, label, confidence);

    state.samplesSeen++;
    updateCount++;

    if (output[label] > 0.5f) {
        correctPredictions++;
    }

    if (updateCallback) {
        OnlinePrediction pred;
        pred.predictedClass = 0;
        float maxScore = output[0];
        for (int i = 1; i < ONLINE_OUTPUT_CLASSES; i++) {
            if (output[i] > maxScore) {
                maxScore = output[i];
                pred.predictedClass = i;
            }
        }
        pred.confidence = maxScore;
        memcpy(pred.classScores, output, sizeof(output));
        updateCallback(pred, true);
    }
}

void OnlineLearner::updateOnSample(const FeatureVector& features, uint8_t label, float confidence) {
    float selectedFeatures[ONLINE_FEATURE_DIM];
    selectFeatures(features, selectedFeatures);
    updateOnSample(selectedFeatures, label, confidence);
}

void OnlineLearner::forward(const float* features, float* output) {
    for (int j = 0; j < ONLINE_OUTPUT_CLASSES; j++) {
        output[j] = state.bias[j];
        for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
            output[j] += features[i] * state.weights[i][j];
        }
    }

    softmax(output, ONLINE_OUTPUT_CLASSES);
}

void OnlineLearner::backward(const float* features, const float* output, uint8_t label) {
    float effectiveRate = state.adaptiveRate;

    for (int j = 0; j < ONLINE_OUTPUT_CLASSES; j++) {
        float error = output[j] - (j == label ? 1.0f : 0.0f);

        for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
            float grad = error * features[i];

            if (useMomentum) {
                state.momentum[i][j] = ONLINE_MOMENTUM * state.momentum[i][j] + grad;
                state.weights[i][j] -= effectiveRate * state.momentum[i][j];
            } else {
                state.weights[i][j] -= effectiveRate * grad;
            }
        }

        if (useMomentum) {
            state.biasMomentum[j] = ONLINE_MOMENTUM * state.biasMomentum[j] + error;
            state.bias[j] -= effectiveRate * state.biasMomentum[j];
        } else {
            state.bias[j] -= effectiveRate * error;
        }
    }
}

void OnlineLearner::softmax(float* values, size_t count) {
    float maxVal = values[0];
    for (size_t i = 1; i < count; i++) {
        if (values[i] > maxVal) maxVal = values[i];
    }

    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        values[i] = exp(values[i] - maxVal);
        sum += values[i];
    }

    if (sum > 0.0001f) {
        for (size_t i = 0; i < count; i++) {
            values[i] /= sum;
        }
    }
}

float OnlineLearner::crossEntropyLoss(const float* output, uint8_t label) {
    float p = output[label];
    if (p < 0.0001f) p = 0.0001f;
    return -log(p);
}

float OnlineLearner::computeLoss(const float* output, uint8_t label) {
    return crossEntropyLoss(output, label);
}

void OnlineLearner::normalizeFeatures(const float* input, float* output, size_t count) {
    for (size_t i = 0; i < count && i < ONLINE_FEATURE_DIM; i++) {
        float std = sqrt(state.runningVariance[i] + 0.0001f);
        output[i] = (input[i] - state.runningMean[i]) / std;
    }
}

void OnlineLearner::updateRunningStats(const float* features, size_t count) {
    float alpha = 0.01f;
    for (size_t i = 0; i < count && i < ONLINE_FEATURE_DIM; i++) {
        float delta = features[i] - state.runningMean[i];
        state.runningMean[i] += alpha * delta;
        state.runningVariance[i] = (1.0f - alpha) * state.runningVariance[i] +
                                    alpha * delta * delta;
    }
}

void OnlineLearner::setLearningRate(float lr) {
    if (lr >= ONLINE_MIN_LR && lr <= ONLINE_MAX_LR) {
        state.adaptiveRate = lr;
    }
}

void OnlineLearner::adaptLearningRate(float recentLoss) {
    float avgLoss = getAverageLoss();

    if (recentLoss < avgLoss * 0.9f) {
        state.adaptiveRate *= 1.05f;
    } else if (recentLoss > avgLoss * 1.1f) {
        state.adaptiveRate *= 0.95f;
    }

    if (state.adaptiveRate < ONLINE_MIN_LR) state.adaptiveRate = ONLINE_MIN_LR;
    if (state.adaptiveRate > ONLINE_MAX_LR) state.adaptiveRate = ONLINE_MAX_LR;
}

void OnlineLearner::recordLoss(float loss) {
    lossHistory[lossIndex] = loss;
    lossIndex = (lossIndex + 1) % ONLINE_LOSS_HISTORY_SIZE;
    if (lossCount < ONLINE_LOSS_HISTORY_SIZE) lossCount++;

    adaptLearningRate(loss);
}

float OnlineLearner::getAverageLoss() const {
    if (lossCount == 0) return 1.0f;

    float sum = 0.0f;
    for (int i = 0; i < lossCount; i++) {
        sum += lossHistory[i];
    }
    return sum / lossCount;
}

void OnlineLearner::addToReservoir(const float* features, uint8_t label, float confidence) {
    if (state.samplesSeen < ONLINE_RESERVOIR_SIZE) {
        size_t idx = state.samplesSeen;
        memcpy(reservoir[idx].features, features, ONLINE_FEATURE_DIM * sizeof(float));
        reservoir[idx].label = label;
        reservoir[idx].confidence = confidence;
        reservoir[idx].timestamp = millis();
        reservoir[idx].valid = true;
    } else {
        size_t idx = selectReservoirIndex();
        if (idx < ONLINE_RESERVOIR_SIZE) {
            memcpy(reservoir[idx].features, features, ONLINE_FEATURE_DIM * sizeof(float));
            reservoir[idx].label = label;
            reservoir[idx].confidence = confidence;
            reservoir[idx].timestamp = millis();
            reservoir[idx].valid = true;
        }
    }
}

size_t OnlineLearner::selectReservoirIndex() {
    uint32_t r = random(state.samplesSeen + 1);
    if (r < ONLINE_RESERVOIR_SIZE) {
        return r;
    }
    return ONLINE_RESERVOIR_SIZE;
}

void OnlineLearner::replayFromReservoir(uint8_t batchSize) {
    uint8_t validCount = 0;
    for (int i = 0; i < ONLINE_RESERVOIR_SIZE; i++) {
        if (reservoir[i].valid) validCount++;
    }

    if (validCount < batchSize) return;

    for (uint8_t b = 0; b < batchSize; b++) {
        size_t idx = random(ONLINE_RESERVOIR_SIZE);
        int attempts = 0;
        while (!reservoir[idx].valid && attempts < ONLINE_RESERVOIR_SIZE) {
            idx = (idx + 1) % ONLINE_RESERVOIR_SIZE;
            attempts++;
        }

        if (reservoir[idx].valid) {
            float normalizedFeatures[ONLINE_FEATURE_DIM];
            normalizeFeatures(reservoir[idx].features, normalizedFeatures, ONLINE_FEATURE_DIM);

            float output[ONLINE_OUTPUT_CLASSES];
            forward(normalizedFeatures, output);
            backward(normalizedFeatures, output, reservoir[idx].label);
        }
    }
}

void OnlineLearner::clearReservoir() {
    for (int i = 0; i < ONLINE_RESERVOIR_SIZE; i++) {
        reservoir[i].valid = false;
    }
}

LearningMetrics OnlineLearner::getMetrics() const {
    LearningMetrics metrics;
    metrics.avgLoss = getAverageLoss();
    metrics.recentAccuracy = (totalPredictions > 0) ?
        ((float)correctPredictions / totalPredictions) : 0.0f;
    metrics.updatesPerformed = updateCount;
    metrics.currentLearningRate = state.adaptiveRate;

    metrics.reservoirSamples = 0;
    for (int i = 0; i < ONLINE_RESERVOIR_SIZE; i++) {
        if (reservoir[i].valid) metrics.reservoirSamples++;
    }

    return metrics;
}

void OnlineLearner::resetMetrics() {
    updateCount = 0;
    correctPredictions = 0;
    totalPredictions = 0;
    lossIndex = 0;
    lossCount = 0;
}

bool OnlineLearner::saveState(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data;
    data += "ONLN";

    for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
        for (int j = 0; j < ONLINE_OUTPUT_CLASSES; j++) {
            data += String(state.weights[i][j], 6) + ",";
        }
    }

    for (int j = 0; j < ONLINE_OUTPUT_CLASSES; j++) {
        data += String(state.bias[j], 6) + ",";
    }

    for (int i = 0; i < ONLINE_FEATURE_DIM; i++) {
        data += String(state.runningMean[i], 6) + ",";
        data += String(state.runningVariance[i], 6) + ",";
    }

    data += String(state.samplesSeen) + "," + String(state.adaptiveRate, 6);

    return storage.saveLog(filename, data);
}

bool OnlineLearner::loadState(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = storage.readLog(filename);
    if (data.length() < 10) return false;

    if (!data.startsWith("ONLN")) return false;

    return true;
}
