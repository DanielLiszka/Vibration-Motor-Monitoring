#ifndef TFLITE_ENGINE_H
#define TFLITE_ENGINE_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"

#ifdef USE_TFLITE
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#endif

#define TFLITE_TENSOR_ARENA_SIZE 16384
#define TFLITE_INPUT_SIZE 10
#define TFLITE_OUTPUT_SIZE 5
#define TFLITE_MAX_MODELS 3

enum TFLiteModelType {
    MODEL_CLASSIFIER = 0,
    MODEL_ANOMALY_DETECTOR,
    MODEL_SEVERITY_ESTIMATOR
};

struct TFLitePrediction {
    float outputs[TFLITE_OUTPUT_SIZE];
    uint8_t predictedClass;
    float confidence;
    float inferenceTimeMs;
    bool valid;
};

struct TFLiteModelInfo {
    const char* name;
    TFLiteModelType type;
    size_t inputSize;
    size_t outputSize;
    const uint8_t* modelData;
    size_t modelSize;
    bool loaded;
};

class TFLiteEngine {
public:
    TFLiteEngine();
    ~TFLiteEngine();

    bool begin();
    bool loadModel(const uint8_t* modelData, size_t modelSize, TFLiteModelType type);
    bool loadModelFromFlash(const char* filename, TFLiteModelType type);
    void unloadModel(TFLiteModelType type);
    void unloadAll();

    TFLitePrediction predict(const FeatureVector& features, TFLiteModelType type = MODEL_CLASSIFIER);
    TFLitePrediction predictRaw(const float* input, size_t inputSize, TFLiteModelType type = MODEL_CLASSIFIER);

    bool isModelLoaded(TFLiteModelType type) const;
    TFLiteModelInfo getModelInfo(TFLiteModelType type) const;
    size_t getFreeArenaMemory() const;
    float getLastInferenceTime() const { return lastInferenceTime; }

    void setInputNormalization(float mean, float stddev);
    void enableQuantization(bool enable) { useQuantization = enable; }

    bool benchmark(TFLiteModelType type, uint32_t iterations, float& avgTimeMs, float& minTimeMs, float& maxTimeMs);

private:
#ifdef USE_TFLITE
    tflite::MicroInterpreter* interpreters[TFLITE_MAX_MODELS];
    const tflite::Model* models[TFLITE_MAX_MODELS];
    tflite::AllOpsResolver* resolver;
    uint8_t* tensorArenas[TFLITE_MAX_MODELS];
#endif

    TFLiteModelInfo modelInfos[TFLITE_MAX_MODELS];
    float lastInferenceTime;
    float inputMean;
    float inputStddev;
    bool useQuantization;
    bool initialized;

    void normalizeInput(float* input, size_t size);
    uint8_t getMaxOutputIndex(const float* outputs, size_t size);
    void featureVectorToInput(const FeatureVector& features, float* input);
};

#endif
