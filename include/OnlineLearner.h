#ifndef ONLINE_LEARNER_H
#define ONLINE_LEARNER_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"

#define ONLINE_FEATURE_DIM 16
#define ONLINE_OUTPUT_CLASSES 5
#define ONLINE_RESERVOIR_SIZE 30
#define ONLINE_DEFAULT_LR 0.001f
#define ONLINE_MOMENTUM 0.9f
#define ONLINE_MIN_LR 0.0001f
#define ONLINE_MAX_LR 0.01f
#define ONLINE_LOSS_HISTORY_SIZE 20

struct OnlineModelState {
    float weights[ONLINE_FEATURE_DIM][ONLINE_OUTPUT_CLASSES];
    float bias[ONLINE_OUTPUT_CLASSES];
    float momentum[ONLINE_FEATURE_DIM][ONLINE_OUTPUT_CLASSES];
    float biasMomentum[ONLINE_OUTPUT_CLASSES];
    float runningMean[ONLINE_FEATURE_DIM];
    float runningVariance[ONLINE_FEATURE_DIM];
    uint32_t samplesSeen;
    float adaptiveRate;
};

struct ReservoirSample {
    float features[ONLINE_FEATURE_DIM];
    uint8_t label;
    float confidence;
    uint32_t timestamp;
    bool valid;
};

struct OnlinePrediction {
    uint8_t predictedClass;
    float confidence;
    float classScores[ONLINE_OUTPUT_CLASSES];
    float uncertainty;
};

struct LearningMetrics {
    float avgLoss;
    float recentAccuracy;
    uint32_t updatesPerformed;
    float currentLearningRate;
    uint32_t reservoirSamples;
};

typedef void (*OnlineLearnerCallback)(const OnlinePrediction& prediction, bool wasUpdate);

class OnlineLearner {
public:
    OnlineLearner();
    ~OnlineLearner();

    bool begin();
    void reset();

    OnlinePrediction predict(const float* features, size_t featureCount);
    OnlinePrediction predict(const FeatureVector& features);

    void updateOnSample(const float* features, uint8_t label, float confidence);
    void updateOnSample(const FeatureVector& features, uint8_t label, float confidence);

    void setLearningRate(float lr);
    void adaptLearningRate(float recentLoss);
    float getLearningRate() const { return state.adaptiveRate; }

    void enableMomentum(bool enable) { useMomentum = enable; }
    void enableReservoirReplay(bool enable) { useReservoirReplay = enable; }

    void addToReservoir(const float* features, uint8_t label, float confidence);
    void replayFromReservoir(uint8_t batchSize);
    void clearReservoir();

    void normalizeFeatures(const float* input, float* output, size_t count);
    void updateRunningStats(const float* features, size_t count);

    LearningMetrics getMetrics() const;
    void resetMetrics();

    bool saveState(const char* filename);
    bool loadState(const char* filename);

    void setCallback(OnlineLearnerCallback callback) { updateCallback = callback; }

    void selectFeatures(const FeatureVector& fullFeatures, float* selectedFeatures);
    const uint8_t* getSelectedFeatureIndices() const { return selectedIndices; }
    void setSelectedFeatureIndices(const uint8_t* indices, size_t count);

    bool isInitialized() const { return initialized; }
    uint32_t getSampleCount() const { return state.samplesSeen; }

private:
    OnlineModelState state;
    ReservoirSample reservoir[ONLINE_RESERVOIR_SIZE];
    float lossHistory[ONLINE_LOSS_HISTORY_SIZE];
    uint8_t lossIndex;
    uint8_t lossCount;

    uint8_t selectedIndices[ONLINE_FEATURE_DIM];
    bool initialized;
    bool useMomentum;
    bool useReservoirReplay;

    uint32_t updateCount;
    uint32_t correctPredictions;
    uint32_t totalPredictions;

    OnlineLearnerCallback updateCallback;

    void initializeWeights();
    void forward(const float* features, float* output);
    float computeLoss(const float* output, uint8_t label);
    void backward(const float* features, const float* output, uint8_t label);
    void softmax(float* values, size_t count);
    float crossEntropyLoss(const float* output, uint8_t label);

    size_t selectReservoirIndex();
    void recordLoss(float loss);
    float getAverageLoss() const;
};

#endif
