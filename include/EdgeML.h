#ifndef EDGE_ML_H
#define EDGE_ML_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"

#define ML_INPUT_FEATURES 10
#define ML_HIDDEN_NEURONS 8
#define ML_OUTPUT_CLASSES 5
#define ML_LEARNING_RATE 0.01
#define ML_EPOCHS 100
#define ML_TRAINING_SAMPLES 50

enum MLFaultClass {
    ML_NORMAL = 0,
    ML_IMBALANCE = 1,
    ML_MISALIGNMENT = 2,
    ML_BEARING_FAULT = 3,
    ML_LOOSENESS = 4
};

struct MLPrediction {
    MLFaultClass predictedClass;
    float confidence;
    float probabilities[ML_OUTPUT_CLASSES];
};

class EdgeML {
public:
    EdgeML();
    ~EdgeML();

    bool begin();

    void startTraining();
    void addTrainingSample(const FeatureVector& features, MLFaultClass label);
    bool train();
    bool isTraining() const { return trainingMode; }

    MLPrediction predict(const FeatureVector& features);

    float getAccuracy() const { return accuracy; }
    uint32_t getTrainingSampleCount() const { return trainingSampleCount; }

    bool saveModel(const String& filename);
    bool loadModel(const String& filename);

    void reset();

private:
    float weightsInputHidden[ML_INPUT_FEATURES][ML_HIDDEN_NEURONS];
    float weightsHiddenOutput[ML_HIDDEN_NEURONS][ML_OUTPUT_CLASSES];
    float biasHidden[ML_HIDDEN_NEURONS];
    float biasOutput[ML_OUTPUT_CLASSES];

    float trainingInputs[ML_TRAINING_SAMPLES][ML_INPUT_FEATURES];
    uint8_t trainingLabels[ML_TRAINING_SAMPLES];
    uint32_t trainingSampleCount;
    bool trainingMode;
    float accuracy;

    void initializeWeights();
    void forwardPass(const float* input, float* hiddenLayer, float* outputLayer);
    void backwardPass(const float* input, const float* hiddenLayer,
                     const float* outputLayer, const float* target);

    float sigmoid(float x);
    float sigmoidDerivative(float x);
    void softmax(float* values, size_t length);

    void featureVectorToInput(const FeatureVector& features, float* input);
    MLFaultClass outputToClass(const float* output);
    void classToTarget(MLFaultClass cls, float* target);
};

#endif
