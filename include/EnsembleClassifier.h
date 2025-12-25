#ifndef ENSEMBLE_CLASSIFIER_H
#define ENSEMBLE_CLASSIFIER_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "EdgeML.h"
#include "TFLiteEngine.h"
#include "OnlineLearner.h"

#define MAX_ENSEMBLE_MODELS 6
#define ENSEMBLE_OUTPUT_SIZE 5

enum EnsembleMethod {
    ENSEMBLE_VOTING = 0,
    ENSEMBLE_AVERAGING,
    ENSEMBLE_WEIGHTED,
    ENSEMBLE_STACKING
};

enum ModelBackend {
    BACKEND_CUSTOM_NN = 0,
    BACKEND_TFLITE,
    BACKEND_THRESHOLD,
    BACKEND_ONLINE_LEARNER
};

struct EnsembleModel {
    const char* name;
    ModelBackend backend;
    float weight;
    bool enabled;
    uint8_t modelIndex;
};

struct EnsemblePrediction {
    uint8_t predictedClass;
    float confidence;
    float classScores[ENSEMBLE_OUTPUT_SIZE];
    uint8_t modelAgreement;
    uint8_t modelsUsed;
    float inferenceTimeMs;
    bool valid;
};

struct ModelVote {
    uint8_t predictedClass;
    float confidence;
    bool valid;
};

class EnsembleClassifier {
public:
    EnsembleClassifier();
    ~EnsembleClassifier();

    bool begin();
    void setMethod(EnsembleMethod method) { ensembleMethod = method; }
    EnsembleMethod getMethod() const { return ensembleMethod; }

    bool addModel(const char* name, ModelBackend backend, float weight = 1.0f);
    bool removeModel(size_t index);
    bool enableModel(size_t index, bool enable);
    bool setModelWeight(size_t index, float weight);

    void setCustomNN(EdgeML* nn) { customNN = nn; }
    void setTFLiteEngine(TFLiteEngine* engine) { tfliteEngine = engine; }
    void setOnlineLearner(OnlineLearner* learner) { onlineLearner = learner; }

    EnsemblePrediction predict(const FeatureVector& features);
    EnsemblePrediction predictWithDetails(const FeatureVector& features, ModelVote* individualVotes);

    void calibrateWeights(const FeatureVector* samples, const uint8_t* labels, size_t sampleCount);
    void resetWeights();

    size_t getModelCount() const { return modelCount; }
    EnsembleModel getModel(size_t index) const;
    float getModelAccuracy(size_t index) const;

    void setConfidenceThreshold(float threshold) { confidenceThreshold = threshold; }
    float getConfidenceThreshold() const { return confidenceThreshold; }

    void setDisagreementCallback(void (*callback)(const EnsemblePrediction&)) {
        disagreementCallback = callback;
    }

private:
    EnsembleModel models[MAX_ENSEMBLE_MODELS];
    size_t modelCount;
    EnsembleMethod ensembleMethod;

    EdgeML* customNN;
    TFLiteEngine* tfliteEngine;
    OnlineLearner* onlineLearner;

    float modelAccuracies[MAX_ENSEMBLE_MODELS];
    float confidenceThreshold;

    void (*disagreementCallback)(const EnsemblePrediction&);

    ModelVote getModelPrediction(size_t modelIndex, const FeatureVector& features);
    EnsemblePrediction combineVoting(ModelVote* votes, size_t voteCount);
    EnsemblePrediction combineAveraging(ModelVote* votes, size_t voteCount);
    EnsemblePrediction combineWeighted(ModelVote* votes, size_t voteCount);
    EnsemblePrediction combineStacking(ModelVote* votes, size_t voteCount);

    uint8_t thresholdClassify(const FeatureVector& features);
    float calculateAgreement(ModelVote* votes, size_t voteCount, uint8_t predictedClass);
};

#endif
