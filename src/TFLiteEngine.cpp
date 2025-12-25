#include "TFLiteEngine.h"
#include "StorageManager.h"
#include <math.h>

TFLiteEngine::TFLiteEngine()
    : lastInferenceTime(0.0f)
    , inputMean(0.0f)
    , inputStddev(1.0f)
    , useQuantization(false)
    , initialized(false)
{
#ifdef USE_TFLITE
    resolver = nullptr;
    for (int i = 0; i < TFLITE_MAX_MODELS; i++) {
        interpreters[i] = nullptr;
        models[i] = nullptr;
        tensorArenas[i] = nullptr;
    }
#endif
    for (int i = 0; i < TFLITE_MAX_MODELS; i++) {
        modelInfos[i].loaded = false;
        modelInfos[i].name = "";
        modelInfos[i].modelData = nullptr;
        modelInfos[i].modelSize = 0;
    }
}

TFLiteEngine::~TFLiteEngine() {
    unloadAll();
#ifdef USE_TFLITE
    if (resolver) {
        delete resolver;
    }
#endif
}

bool TFLiteEngine::begin() {
    DEBUG_PRINTLN("Initializing TFLite Engine...");

#ifdef USE_TFLITE
    resolver = new tflite::AllOpsResolver();
    if (!resolver) {
        DEBUG_PRINTLN("Failed to create ops resolver");
        return false;
    }

    for (int i = 0; i < TFLITE_MAX_MODELS; i++) {
        tensorArenas[i] = (uint8_t*)heap_caps_malloc(TFLITE_TENSOR_ARENA_SIZE, MALLOC_CAP_8BIT);
        if (!tensorArenas[i]) {
            DEBUG_PRINT("Failed to allocate tensor arena ");
            DEBUG_PRINTLN(i);
            return false;
        }
    }

    initialized = true;
    DEBUG_PRINTLN("TFLite Engine initialized");
    return true;
#else
    DEBUG_PRINTLN("TFLite not enabled in build");
    initialized = false;
    return false;
#endif
}

bool TFLiteEngine::loadModel(const uint8_t* modelData, size_t modelSize, TFLiteModelType type) {
#ifdef USE_TFLITE
    if (!initialized) {
        DEBUG_PRINTLN("TFLite not initialized");
        return false;
    }

    if (type >= TFLITE_MAX_MODELS) {
        DEBUG_PRINTLN("Invalid model type");
        return false;
    }

    if (modelInfos[type].loaded) {
        unloadModel(type);
    }

    models[type] = tflite::GetModel(modelData);
    if (!models[type]) {
        DEBUG_PRINTLN("Failed to load model");
        return false;
    }

    if (models[type]->version() != TFLITE_SCHEMA_VERSION) {
        DEBUG_PRINT("Model schema version mismatch: ");
        DEBUG_PRINT(models[type]->version());
        DEBUG_PRINT(" vs ");
        DEBUG_PRINTLN(TFLITE_SCHEMA_VERSION);
        return false;
    }

    interpreters[type] = new tflite::MicroInterpreter(
        models[type],
        *resolver,
        tensorArenas[type],
        TFLITE_TENSOR_ARENA_SIZE
    );

    if (!interpreters[type]) {
        DEBUG_PRINTLN("Failed to create interpreter");
        return false;
    }

    TfLiteStatus allocate_status = interpreters[type]->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        DEBUG_PRINTLN("Failed to allocate tensors");
        delete interpreters[type];
        interpreters[type] = nullptr;
        return false;
    }

    TfLiteTensor* input = interpreters[type]->input(0);
    TfLiteTensor* output = interpreters[type]->output(0);

    modelInfos[type].type = type;
    modelInfos[type].modelData = modelData;
    modelInfos[type].modelSize = modelSize;
    modelInfos[type].inputSize = input->bytes / sizeof(float);
    modelInfos[type].outputSize = output->bytes / sizeof(float);
    modelInfos[type].loaded = true;

    switch (type) {
        case MODEL_CLASSIFIER:
            modelInfos[type].name = "Fault Classifier";
            break;
        case MODEL_ANOMALY_DETECTOR:
            modelInfos[type].name = "Anomaly Detector";
            break;
        case MODEL_SEVERITY_ESTIMATOR:
            modelInfos[type].name = "Severity Estimator";
            break;
    }

    DEBUG_PRINT("Model loaded: ");
    DEBUG_PRINTLN(modelInfos[type].name);
    DEBUG_PRINT("Input size: ");
    DEBUG_PRINTLN(modelInfos[type].inputSize);
    DEBUG_PRINT("Output size: ");
    DEBUG_PRINTLN(modelInfos[type].outputSize);

    return true;
#else
    return false;
#endif
}

bool TFLiteEngine::loadModelFromFlash(const char* filename, TFLiteModelType type) {
#ifdef USE_TFLITE
    StorageManager storage;
    if (!storage.begin()) {
        DEBUG_PRINTLN("Failed to init storage");
        return false;
    }

    File file = SPIFFS.open(filename, "r");
    if (!file) {
        DEBUG_PRINT("Failed to open model file: ");
        DEBUG_PRINTLN(filename);
        return false;
    }

    size_t fileSize = file.size();
    uint8_t* modelBuffer = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_8BIT);
    if (!modelBuffer) {
        DEBUG_PRINTLN("Failed to allocate model buffer");
        file.close();
        return false;
    }

    size_t bytesRead = file.read(modelBuffer, fileSize);
    file.close();

    if (bytesRead != fileSize) {
        DEBUG_PRINTLN("Failed to read complete model file");
        free(modelBuffer);
        return false;
    }

    bool result = loadModel(modelBuffer, fileSize, type);
    if (!result) {
        free(modelBuffer);
    }

    return result;
#else
    return false;
#endif
}

void TFLiteEngine::unloadModel(TFLiteModelType type) {
#ifdef USE_TFLITE
    if (type >= TFLITE_MAX_MODELS) return;

    if (interpreters[type]) {
        delete interpreters[type];
        interpreters[type] = nullptr;
    }
    models[type] = nullptr;
    modelInfos[type].loaded = false;
#endif
}

void TFLiteEngine::unloadAll() {
#ifdef USE_TFLITE
    for (int i = 0; i < TFLITE_MAX_MODELS; i++) {
        unloadModel((TFLiteModelType)i);
    }
    for (int i = 0; i < TFLITE_MAX_MODELS; i++) {
        if (tensorArenas[i]) {
            free(tensorArenas[i]);
            tensorArenas[i] = nullptr;
        }
    }
#endif
}

TFLitePrediction TFLiteEngine::predict(const FeatureVector& features, TFLiteModelType type) {
    float input[TFLITE_INPUT_SIZE];
    featureVectorToInput(features, input);
    return predictRaw(input, TFLITE_INPUT_SIZE, type);
}

TFLitePrediction TFLiteEngine::predictRaw(const float* input, size_t inputSize, TFLiteModelType type) {
    TFLitePrediction result;
    result.valid = false;
    result.predictedClass = 0;
    result.confidence = 0.0f;
    result.inferenceTimeMs = 0.0f;
    memset(result.outputs, 0, sizeof(result.outputs));

#ifdef USE_TFLITE
    if (!initialized || type >= TFLITE_MAX_MODELS || !modelInfos[type].loaded) {
        DEBUG_PRINTLN("Model not available for prediction");
        return result;
    }

    TfLiteTensor* inputTensor = interpreters[type]->input(0);
    if (!inputTensor) {
        DEBUG_PRINTLN("Failed to get input tensor");
        return result;
    }

    float normalizedInput[TFLITE_INPUT_SIZE];
    memcpy(normalizedInput, input, inputSize * sizeof(float));
    normalizeInput(normalizedInput, inputSize);

    size_t copySize = min(inputSize, modelInfos[type].inputSize);
    memcpy(inputTensor->data.f, normalizedInput, copySize * sizeof(float));

    uint32_t startTime = micros();
    TfLiteStatus invoke_status = interpreters[type]->Invoke();
    uint32_t endTime = micros();

    lastInferenceTime = (endTime - startTime) / 1000.0f;
    result.inferenceTimeMs = lastInferenceTime;

    if (invoke_status != kTfLiteOk) {
        DEBUG_PRINTLN("Inference failed");
        return result;
    }

    TfLiteTensor* outputTensor = interpreters[type]->output(0);
    if (!outputTensor) {
        DEBUG_PRINTLN("Failed to get output tensor");
        return result;
    }

    size_t outputCopySize = min((size_t)TFLITE_OUTPUT_SIZE, modelInfos[type].outputSize);
    memcpy(result.outputs, outputTensor->data.f, outputCopySize * sizeof(float));

    result.predictedClass = getMaxOutputIndex(result.outputs, outputCopySize);
    result.confidence = result.outputs[result.predictedClass];
    result.valid = true;
#endif

    return result;
}

bool TFLiteEngine::isModelLoaded(TFLiteModelType type) const {
    if (type >= TFLITE_MAX_MODELS) return false;
    return modelInfos[type].loaded;
}

TFLiteModelInfo TFLiteEngine::getModelInfo(TFLiteModelType type) const {
    if (type >= TFLITE_MAX_MODELS) {
        TFLiteModelInfo empty;
        empty.loaded = false;
        return empty;
    }
    return modelInfos[type];
}

size_t TFLiteEngine::getFreeArenaMemory() const {
#ifdef USE_TFLITE
    if (!initialized) return 0;
    size_t used = 0;
    for (int i = 0; i < TFLITE_MAX_MODELS; i++) {
        if (interpreters[i]) {
            used += interpreters[i]->arena_used_bytes();
        }
    }
    return (TFLITE_TENSOR_ARENA_SIZE * TFLITE_MAX_MODELS) - used;
#else
    return 0;
#endif
}

void TFLiteEngine::setInputNormalization(float mean, float stddev) {
    inputMean = mean;
    inputStddev = (stddev > 0.0001f) ? stddev : 1.0f;
}

bool TFLiteEngine::benchmark(TFLiteModelType type, uint32_t iterations, float& avgTimeMs, float& minTimeMs, float& maxTimeMs) {
#ifdef USE_TFLITE
    if (!initialized || type >= TFLITE_MAX_MODELS || !modelInfos[type].loaded) {
        return false;
    }

    float testInput[TFLITE_INPUT_SIZE];
    for (int i = 0; i < TFLITE_INPUT_SIZE; i++) {
        testInput[i] = random(-1000, 1000) / 1000.0f;
    }

    float totalTime = 0.0f;
    minTimeMs = 999999.0f;
    maxTimeMs = 0.0f;

    for (uint32_t i = 0; i < iterations; i++) {
        TFLitePrediction result = predictRaw(testInput, TFLITE_INPUT_SIZE, type);
        if (!result.valid) {
            return false;
        }

        totalTime += result.inferenceTimeMs;
        if (result.inferenceTimeMs < minTimeMs) minTimeMs = result.inferenceTimeMs;
        if (result.inferenceTimeMs > maxTimeMs) maxTimeMs = result.inferenceTimeMs;
    }

    avgTimeMs = totalTime / iterations;
    return true;
#else
    return false;
#endif
}

void TFLiteEngine::normalizeInput(float* input, size_t size) {
    for (size_t i = 0; i < size; i++) {
        input[i] = (input[i] - inputMean) / inputStddev;
    }
}

uint8_t TFLiteEngine::getMaxOutputIndex(const float* outputs, size_t size) {
    uint8_t maxIdx = 0;
    float maxVal = outputs[0];
    for (size_t i = 1; i < size; i++) {
        if (outputs[i] > maxVal) {
            maxVal = outputs[i];
            maxIdx = i;
        }
    }
    return maxIdx;
}

void TFLiteEngine::featureVectorToInput(const FeatureVector& features, float* input) {
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
