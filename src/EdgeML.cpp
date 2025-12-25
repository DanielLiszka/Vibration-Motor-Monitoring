#include "EdgeML.h"
#include "StorageManager.h"
#include <math.h>

EdgeML::EdgeML()
    : trainingSampleCount(0)
    , trainingMode(false)
    , accuracy(0.0f)
{
    initializeWeights();
}

EdgeML::~EdgeML() {
}

bool EdgeML::begin() {
    DEBUG_PRINTLN("Initializing Edge ML...");
    initializeWeights();
    return true;
}

void EdgeML::initializeWeights() {
    for (size_t i = 0; i < ML_INPUT_FEATURES; i++) {
        for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
            weightsInputHidden[i][j] = ((float)random(-1000, 1000)) / 1000.0f;
        }
    }

    for (size_t i = 0; i < ML_HIDDEN_NEURONS; i++) {
        biasHidden[i] = ((float)random(-1000, 1000)) / 1000.0f;
        for (size_t j = 0; j < ML_OUTPUT_CLASSES; j++) {
            weightsHiddenOutput[i][j] = ((float)random(-1000, 1000)) / 1000.0f;
        }
    }

    for (size_t i = 0; i < ML_OUTPUT_CLASSES; i++) {
        biasOutput[i] = ((float)random(-1000, 1000)) / 1000.0f;
    }
}

void EdgeML::startTraining() {
    DEBUG_PRINTLN("Starting ML training mode");
    trainingMode = true;
    trainingSampleCount = 0;
    accuracy = 0.0f;
}

void EdgeML::addTrainingSample(const FeatureVector& features, MLFaultClass label) {
    if (trainingSampleCount >= ML_TRAINING_SAMPLES) {
        DEBUG_PRINTLN("Training buffer full");
        return;
    }

    float input[ML_INPUT_FEATURES];
    featureVectorToInput(features, input);

    for (size_t i = 0; i < ML_INPUT_FEATURES; i++) {
        trainingInputs[trainingSampleCount][i] = input[i];
    }

    trainingLabels[trainingSampleCount] = (uint8_t)label;
    trainingSampleCount++;

    DEBUG_PRINT("Added training sample ");
    DEBUG_PRINT(trainingSampleCount);
    DEBUG_PRINT("/");
    DEBUG_PRINTLN(ML_TRAINING_SAMPLES);
}

bool EdgeML::train() {
    if (trainingSampleCount < 10) {
        DEBUG_PRINTLN("Not enough training samples");
        return false;
    }

    DEBUG_PRINTLN("Training neural network...");

    for (uint32_t epoch = 0; epoch < ML_EPOCHS; epoch++) {
        float totalLoss = 0.0f;

        for (uint32_t sample = 0; sample < trainingSampleCount; sample++) {
            float hiddenLayer[ML_HIDDEN_NEURONS];
            float outputLayer[ML_OUTPUT_CLASSES];
            float target[ML_OUTPUT_CLASSES];

            classToTarget((MLFaultClass)trainingLabels[sample], target);

            forwardPass(trainingInputs[sample], hiddenLayer, outputLayer);

            for (size_t i = 0; i < ML_OUTPUT_CLASSES; i++) {
                float error = target[i] - outputLayer[i];
                totalLoss += error * error;
            }

            backwardPass(trainingInputs[sample], hiddenLayer, outputLayer, target);
        }

        if (epoch % 10 == 0) {
            DEBUG_PRINT("Epoch ");
            DEBUG_PRINT(epoch);
            DEBUG_PRINT(" Loss: ");
            DEBUG_PRINTLN(totalLoss / trainingSampleCount);
        }
    }

    uint32_t correct = 0;
    for (uint32_t sample = 0; sample < trainingSampleCount; sample++) {
        float hiddenLayer[ML_HIDDEN_NEURONS];
        float outputLayer[ML_OUTPUT_CLASSES];

        forwardPass(trainingInputs[sample], hiddenLayer, outputLayer);

        MLFaultClass predicted = outputToClass(outputLayer);
        if (predicted == (MLFaultClass)trainingLabels[sample]) {
            correct++;
        }
    }

    accuracy = (float)correct / trainingSampleCount * 100.0f;

    DEBUG_PRINT("Training complete. Accuracy: ");
    DEBUG_PRINT(accuracy);
    DEBUG_PRINTLN("%");

    trainingMode = false;
    return true;
}

MLPrediction EdgeML::predict(const FeatureVector& features) {
    MLPrediction prediction;

    float input[ML_INPUT_FEATURES];
    featureVectorToInput(features, input);

    float hiddenLayer[ML_HIDDEN_NEURONS];
    float outputLayer[ML_OUTPUT_CLASSES];

    forwardPass(input, hiddenLayer, outputLayer);

    prediction.predictedClass = outputToClass(outputLayer);

    float maxProb = 0.0f;
    for (size_t i = 0; i < ML_OUTPUT_CLASSES; i++) {
        prediction.probabilities[i] = outputLayer[i];
        if (outputLayer[i] > maxProb) {
            maxProb = outputLayer[i];
        }
    }

    prediction.confidence = maxProb;

    return prediction;
}

void EdgeML::forwardPass(const float* input, float* hiddenLayer, float* outputLayer) {
    for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
        float sum = biasHidden[j];
        for (size_t i = 0; i < ML_INPUT_FEATURES; i++) {
            sum += input[i] * weightsInputHidden[i][j];
        }
        hiddenLayer[j] = sigmoid(sum);
    }

    for (size_t k = 0; k < ML_OUTPUT_CLASSES; k++) {
        float sum = biasOutput[k];
        for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
            sum += hiddenLayer[j] * weightsHiddenOutput[j][k];
        }
        outputLayer[k] = sum;
    }

    softmax(outputLayer, ML_OUTPUT_CLASSES);
}

void EdgeML::backwardPass(const float* input, const float* hiddenLayer,
                         const float* outputLayer, const float* target) {
    float outputError[ML_OUTPUT_CLASSES];
    for (size_t k = 0; k < ML_OUTPUT_CLASSES; k++) {
        outputError[k] = target[k] - outputLayer[k];
    }

    float hiddenError[ML_HIDDEN_NEURONS];
    for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
        hiddenError[j] = 0.0f;
        for (size_t k = 0; k < ML_OUTPUT_CLASSES; k++) {
            hiddenError[j] += outputError[k] * weightsHiddenOutput[j][k];
        }
        hiddenError[j] *= sigmoidDerivative(hiddenLayer[j]);
    }

    for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
        for (size_t k = 0; k < ML_OUTPUT_CLASSES; k++) {
            weightsHiddenOutput[j][k] += ML_LEARNING_RATE * outputError[k] * hiddenLayer[j];
        }
    }

    for (size_t k = 0; k < ML_OUTPUT_CLASSES; k++) {
        biasOutput[k] += ML_LEARNING_RATE * outputError[k];
    }

    for (size_t i = 0; i < ML_INPUT_FEATURES; i++) {
        for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
            weightsInputHidden[i][j] += ML_LEARNING_RATE * hiddenError[j] * input[i];
        }
    }

    for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
        biasHidden[j] += ML_LEARNING_RATE * hiddenError[j];
    }
}

float EdgeML::sigmoid(float x) {
    return 1.0f / (1.0f + exp(-x));
}

float EdgeML::sigmoidDerivative(float x) {
    return x * (1.0f - x);
}

void EdgeML::softmax(float* values, size_t length) {
    float max = values[0];
    for (size_t i = 1; i < length; i++) {
        if (values[i] > max) max = values[i];
    }

    float sum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        values[i] = exp(values[i] - max);
        sum += values[i];
    }

    for (size_t i = 0; i < length; i++) {
        values[i] /= sum;
    }
}

void EdgeML::featureVectorToInput(const FeatureVector& features, float* input) {
    input[0] = features.rms;
    input[1] = features.peakToPeak;
    input[2] = features.kurtosis;
    input[3] = features.skewness;
    input[4] = features.crestFactor;
    input[5] = features.variance;
    input[6] = features.spectralCentroid / 100.0f;
    input[7] = features.spectralSpread / 100.0f;
    input[8] = features.bandPowerRatio;
    input[9] = features.dominantFrequency / 100.0f;
}

MLFaultClass EdgeML::outputToClass(const float* output) {
    float maxProb = output[0];
    size_t maxIdx = 0;

    for (size_t i = 1; i < ML_OUTPUT_CLASSES; i++) {
        if (output[i] > maxProb) {
            maxProb = output[i];
            maxIdx = i;
        }
    }

    return (MLFaultClass)maxIdx;
}

void EdgeML::classToTarget(MLFaultClass cls, float* target) {
    for (size_t i = 0; i < ML_OUTPUT_CLASSES; i++) {
        target[i] = (i == (size_t)cls) ? 1.0f : 0.0f;
    }
}

void EdgeML::reset() {
    initializeWeights();
    trainingSampleCount = 0;
    trainingMode = false;
    accuracy = 0.0f;
}

bool EdgeML::saveModel(const String& filename) {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    String data = "";

    for (size_t i = 0; i < ML_INPUT_FEATURES; i++) {
        for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
            data += String(weightsInputHidden[i][j], 6) + ",";
        }
    }

    for (size_t i = 0; i < ML_HIDDEN_NEURONS; i++) {
        data += String(biasHidden[i], 6) + ",";
        for (size_t j = 0; j < ML_OUTPUT_CLASSES; j++) {
            data += String(weightsHiddenOutput[i][j], 6) + ",";
        }
    }

    for (size_t i = 0; i < ML_OUTPUT_CLASSES; i++) {
        data += String(biasOutput[i], 6) + ",";
    }

    return storage.saveLog(filename, data);
}

bool EdgeML::loadModel(const String& filename) {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    String data = storage.readLog(filename);
    if (data.length() == 0) {
        return false;
    }

    int idx = 0;
    int commaIdx;

    for (size_t i = 0; i < ML_INPUT_FEATURES; i++) {
        for (size_t j = 0; j < ML_HIDDEN_NEURONS; j++) {
            commaIdx = data.indexOf(',', idx);
            if (commaIdx == -1) return false;
            weightsInputHidden[i][j] = data.substring(idx, commaIdx).toFloat();
            idx = commaIdx + 1;
        }
    }

    for (size_t i = 0; i < ML_HIDDEN_NEURONS; i++) {
        commaIdx = data.indexOf(',', idx);
        if (commaIdx == -1) return false;
        biasHidden[i] = data.substring(idx, commaIdx).toFloat();
        idx = commaIdx + 1;

        for (size_t j = 0; j < ML_OUTPUT_CLASSES; j++) {
            commaIdx = data.indexOf(',', idx);
            if (commaIdx == -1) return false;
            weightsHiddenOutput[i][j] = data.substring(idx, commaIdx).toFloat();
            idx = commaIdx + 1;
        }
    }

    for (size_t i = 0; i < ML_OUTPUT_CLASSES; i++) {
        commaIdx = data.indexOf(',', idx);
        if (commaIdx == -1 && i < ML_OUTPUT_CLASSES - 1) return false;
        if (commaIdx == -1) {
            biasOutput[i] = data.substring(idx).toFloat();
        } else {
            biasOutput[i] = data.substring(idx, commaIdx).toFloat();
            idx = commaIdx + 1;
        }
    }

    DEBUG_PRINTLN("ML model loaded successfully");
    return true;
}
