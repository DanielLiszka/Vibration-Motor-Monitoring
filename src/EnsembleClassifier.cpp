#include "EnsembleClassifier.h"
#include <math.h>

EnsembleClassifier::EnsembleClassifier()
    : modelCount(0)
    , ensembleMethod(ENSEMBLE_VOTING)
    , customNN(nullptr)
    , tfliteEngine(nullptr)
    , onlineLearner(nullptr)
    , confidenceThreshold(0.5f)
    , disagreementCallback(nullptr)
{
    memset(models, 0, sizeof(models));
    for (int i = 0; i < MAX_ENSEMBLE_MODELS; i++) {
        modelAccuracies[i] = 0.0f;
    }
}

EnsembleClassifier::~EnsembleClassifier() {
}

bool EnsembleClassifier::begin() {
    DEBUG_PRINTLN("Initializing Ensemble Classifier...");

    addModel("Threshold", BACKEND_THRESHOLD, 0.5f);

    DEBUG_PRINTLN("Ensemble Classifier initialized");
    return true;
}

bool EnsembleClassifier::addModel(const char* name, ModelBackend backend, float weight) {
    if (modelCount >= MAX_ENSEMBLE_MODELS) {
        DEBUG_PRINTLN("Maximum models reached");
        return false;
    }

    models[modelCount].name = name;
    models[modelCount].backend = backend;
    models[modelCount].weight = weight;
    models[modelCount].enabled = true;
    models[modelCount].modelIndex = modelCount;
    modelCount++;

    DEBUG_PRINT("Added model: ");
    DEBUG_PRINTLN(name);

    return true;
}

bool EnsembleClassifier::removeModel(size_t index) {
    if (index >= modelCount) return false;

    for (size_t i = index; i < modelCount - 1; i++) {
        models[i] = models[i + 1];
        modelAccuracies[i] = modelAccuracies[i + 1];
    }

    modelCount--;
    return true;
}

bool EnsembleClassifier::enableModel(size_t index, bool enable) {
    if (index >= modelCount) return false;
    models[index].enabled = enable;
    return true;
}

bool EnsembleClassifier::setModelWeight(size_t index, float weight) {
    if (index >= modelCount) return false;
    models[index].weight = weight;
    return true;
}

EnsemblePrediction EnsembleClassifier::predict(const FeatureVector& features) {
    ModelVote votes[MAX_ENSEMBLE_MODELS];
    return predictWithDetails(features, votes);
}

EnsemblePrediction EnsembleClassifier::predictWithDetails(const FeatureVector& features, ModelVote* individualVotes) {
    uint32_t startTime = micros();

    ModelVote votes[MAX_ENSEMBLE_MODELS];
    size_t validVotes = 0;

    for (size_t i = 0; i < modelCount; i++) {
        if (!models[i].enabled) {
            votes[i].valid = false;
            continue;
        }

        votes[i] = getModelPrediction(i, features);
        if (votes[i].valid) {
            validVotes++;
        }
    }

    if (individualVotes) {
        memcpy(individualVotes, votes, modelCount * sizeof(ModelVote));
    }

    EnsemblePrediction result;

    if (validVotes == 0) {
        result.valid = false;
        result.predictedClass = 0;
        result.confidence = 0.0f;
        result.modelAgreement = 0;
        result.modelsUsed = 0;
        result.inferenceTimeMs = 0.0f;
        memset(result.classScores, 0, sizeof(result.classScores));
        return result;
    }

    switch (ensembleMethod) {
        case ENSEMBLE_VOTING:
            result = combineVoting(votes, modelCount);
            break;
        case ENSEMBLE_AVERAGING:
            result = combineAveraging(votes, modelCount);
            break;
        case ENSEMBLE_WEIGHTED:
            result = combineWeighted(votes, modelCount);
            break;
        case ENSEMBLE_STACKING:
            result = combineStacking(votes, modelCount);
            break;
        default:
            result = combineVoting(votes, modelCount);
    }

    result.inferenceTimeMs = (micros() - startTime) / 1000.0f;
    result.modelsUsed = validVotes;
    result.modelAgreement = (uint8_t)(calculateAgreement(votes, modelCount, result.predictedClass) * 100);

    if (disagreementCallback && result.modelAgreement < 50) {
        disagreementCallback(result);
    }

    return result;
}

ModelVote EnsembleClassifier::getModelPrediction(size_t modelIndex, const FeatureVector& features) {
    ModelVote vote;
    vote.valid = false;
    vote.predictedClass = 0;
    vote.confidence = 0.0f;

    if (modelIndex >= modelCount) return vote;

    switch (models[modelIndex].backend) {
        case BACKEND_CUSTOM_NN:
            if (customNN) {
                MLPrediction pred = customNN->predict(features);
                vote.predictedClass = (uint8_t)pred.predictedClass;
                vote.confidence = pred.confidence;
                vote.valid = true;
            }
            break;

        case BACKEND_TFLITE:
#ifdef USE_TFLITE
            if (tfliteEngine && tfliteEngine->isModelLoaded(MODEL_CLASSIFIER)) {
                TFLitePrediction pred = tfliteEngine->predict(features, MODEL_CLASSIFIER);
                vote.predictedClass = pred.predictedClass;
                vote.confidence = pred.confidence;
                vote.valid = pred.valid;
            }
#endif
            break;

        case BACKEND_THRESHOLD:
            vote.predictedClass = thresholdClassify(features);
            vote.confidence = 0.7f;
            vote.valid = true;
            break;

        case BACKEND_ONLINE_LEARNER:
            if (onlineLearner && onlineLearner->isInitialized()) {
                OnlinePrediction pred = onlineLearner->predict(features);
                vote.predictedClass = pred.predictedClass;
                vote.confidence = pred.confidence;
                vote.valid = true;
            }
            break;
    }

    return vote;
}

EnsemblePrediction EnsembleClassifier::combineVoting(ModelVote* votes, size_t voteCount) {
    EnsemblePrediction result;
    memset(result.classScores, 0, sizeof(result.classScores));

    uint8_t voteCounts[ENSEMBLE_OUTPUT_SIZE] = {0};

    for (size_t i = 0; i < voteCount; i++) {
        if (votes[i].valid && votes[i].predictedClass < ENSEMBLE_OUTPUT_SIZE) {
            voteCounts[votes[i].predictedClass]++;
        }
    }

    uint8_t maxVotes = 0;
    result.predictedClass = 0;

    for (int c = 0; c < ENSEMBLE_OUTPUT_SIZE; c++) {
        if (voteCounts[c] > maxVotes) {
            maxVotes = voteCounts[c];
            result.predictedClass = c;
        }
    }

    size_t validVotes = 0;
    for (size_t i = 0; i < voteCount; i++) {
        if (votes[i].valid) validVotes++;
    }

    result.confidence = (validVotes > 0) ? (float)maxVotes / validVotes : 0.0f;

    for (int c = 0; c < ENSEMBLE_OUTPUT_SIZE; c++) {
        result.classScores[c] = (validVotes > 0) ? (float)voteCounts[c] / validVotes : 0.0f;
    }

    result.valid = true;
    return result;
}

EnsemblePrediction EnsembleClassifier::combineAveraging(ModelVote* votes, size_t voteCount) {
    EnsemblePrediction result;
    memset(result.classScores, 0, sizeof(result.classScores));

    size_t validVotes = 0;

    for (size_t i = 0; i < voteCount; i++) {
        if (votes[i].valid && votes[i].predictedClass < ENSEMBLE_OUTPUT_SIZE) {
            result.classScores[votes[i].predictedClass] += votes[i].confidence;
            validVotes++;
        }
    }

    if (validVotes > 0) {
        for (int c = 0; c < ENSEMBLE_OUTPUT_SIZE; c++) {
            result.classScores[c] /= validVotes;
        }
    }

    float maxScore = 0.0f;
    result.predictedClass = 0;

    for (int c = 0; c < ENSEMBLE_OUTPUT_SIZE; c++) {
        if (result.classScores[c] > maxScore) {
            maxScore = result.classScores[c];
            result.predictedClass = c;
        }
    }

    result.confidence = maxScore;
    result.valid = true;
    return result;
}

EnsemblePrediction EnsembleClassifier::combineWeighted(ModelVote* votes, size_t voteCount) {
    EnsemblePrediction result;
    memset(result.classScores, 0, sizeof(result.classScores));

    float totalWeight = 0.0f;

    for (size_t i = 0; i < voteCount; i++) {
        if (votes[i].valid && i < modelCount && votes[i].predictedClass < ENSEMBLE_OUTPUT_SIZE) {
            float weight = models[i].weight;
            result.classScores[votes[i].predictedClass] += votes[i].confidence * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f) {
        for (int c = 0; c < ENSEMBLE_OUTPUT_SIZE; c++) {
            result.classScores[c] /= totalWeight;
        }
    }

    float maxScore = 0.0f;
    result.predictedClass = 0;

    for (int c = 0; c < ENSEMBLE_OUTPUT_SIZE; c++) {
        if (result.classScores[c] > maxScore) {
            maxScore = result.classScores[c];
            result.predictedClass = c;
        }
    }

    result.confidence = maxScore;
    result.valid = true;
    return result;
}

EnsemblePrediction EnsembleClassifier::combineStacking(ModelVote* votes, size_t voteCount) {
    return combineWeighted(votes, voteCount);
}

uint8_t EnsembleClassifier::thresholdClassify(const FeatureVector& features) {
    if (features.kurtosis > 5.0f || features.crestFactor > 6.0f) {
        return 4;
    }

    if (features.dominantFrequency > 500.0f && features.rms > 1.5f) {
        return 3;
    }

    if (features.bandPowerRatio > 2.0f && features.spectralCentroid > 200.0f) {
        return 2;
    }

    if (features.rms > 2.0f || features.variance > 1.0f) {
        return 1;
    }

    return 0;
}

float EnsembleClassifier::calculateAgreement(ModelVote* votes, size_t voteCount, uint8_t predictedClass) {
    size_t agreeing = 0;
    size_t validVotes = 0;

    for (size_t i = 0; i < voteCount; i++) {
        if (votes[i].valid) {
            validVotes++;
            if (votes[i].predictedClass == predictedClass) {
                agreeing++;
            }
        }
    }

    return (validVotes > 0) ? (float)agreeing / validVotes : 0.0f;
}

void EnsembleClassifier::calibrateWeights(const FeatureVector* samples, const uint8_t* labels, size_t sampleCount) {
    DEBUG_PRINTLN("Calibrating ensemble weights...");

    for (size_t m = 0; m < modelCount; m++) {
        if (!models[m].enabled) continue;

        size_t correct = 0;

        for (size_t s = 0; s < sampleCount; s++) {
            ModelVote vote = getModelPrediction(m, samples[s]);
            if (vote.valid && vote.predictedClass == labels[s]) {
                correct++;
            }
        }

        modelAccuracies[m] = (sampleCount > 0) ? (float)correct / sampleCount : 0.0f;
        models[m].weight = modelAccuracies[m];

        DEBUG_PRINT("Model ");
        DEBUG_PRINT(models[m].name);
        DEBUG_PRINT(" accuracy: ");
        DEBUG_PRINT(modelAccuracies[m] * 100);
        DEBUG_PRINTLN("%");
    }

    float totalWeight = 0.0f;
    for (size_t m = 0; m < modelCount; m++) {
        totalWeight += models[m].weight;
    }

    if (totalWeight > 0.0f) {
        for (size_t m = 0; m < modelCount; m++) {
            models[m].weight /= totalWeight;
        }
    }

    DEBUG_PRINTLN("Calibration complete");
}

void EnsembleClassifier::resetWeights() {
    float equalWeight = 1.0f / modelCount;
    for (size_t m = 0; m < modelCount; m++) {
        models[m].weight = equalWeight;
    }
}

EnsembleModel EnsembleClassifier::getModel(size_t index) const {
    if (index >= modelCount) {
        EnsembleModel empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return models[index];
}

float EnsembleClassifier::getModelAccuracy(size_t index) const {
    if (index >= modelCount) return 0.0f;
    return modelAccuracies[index];
}
