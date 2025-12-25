#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "TFLiteEngine.h"
#include "EdgeML.h"

#define MODEL_REGISTRY_SIZE 8
#define MODEL_NAME_MAX_LEN 32
#define MODEL_VERSION_MAX_LEN 16
#define MODEL_HASH_LEN 32

struct ModelMetadata {
    char name[MODEL_NAME_MAX_LEN];
    char version[MODEL_VERSION_MAX_LEN];
    char filename[64];
    TFLiteModelType type;
    size_t size;
    uint32_t crc32;
    uint32_t createdTimestamp;
    uint32_t loadedTimestamp;
    float accuracy;
    bool isActive;
    bool isValid;
};

struct ModelPerformanceStats {
    uint32_t totalInferences;
    uint32_t correctPredictions;
    float avgInferenceTimeMs;
    float maxInferenceTimeMs;
    float minInferenceTimeMs;
    uint32_t lastInferenceTime;
};

enum ModelUpdateStatus {
    UPDATE_IDLE = 0,
    UPDATE_DOWNLOADING,
    UPDATE_VALIDATING,
    UPDATE_INSTALLING,
    UPDATE_COMPLETE,
    UPDATE_FAILED
};

class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    bool begin();
    void loop();

    bool registerModel(const char* name, const char* version, TFLiteModelType type,
                       const uint8_t* data, size_t size);
    bool registerModelFromFile(const char* name, const char* version,
                               TFLiteModelType type, const char* filename);
    bool unregisterModel(const char* name);

    bool activateModel(const char* name);
    bool deactivateModel(const char* name);
    bool loadActiveModels();

    ModelMetadata* getModelInfo(const char* name);
    ModelMetadata* getModelByIndex(size_t index);
    size_t getModelCount() const { return modelCount; }

    void setTFLiteEngine(TFLiteEngine* engine) { tfliteEngine = engine; }
    void setEdgeML(EdgeML* ml) { edgeML = ml; }

    bool startOTAUpdate(const char* url, const char* modelName);
    ModelUpdateStatus getUpdateStatus() const { return updateStatus; }
    uint8_t getUpdateProgress() const { return updateProgress; }
    void cancelUpdate();

    bool validateModel(const char* name);
    bool validateModelData(const uint8_t* data, size_t size, uint32_t expectedCrc);

    void recordInference(const char* name, float inferenceTimeMs, bool correct);
    ModelPerformanceStats getPerformanceStats(const char* name);
    void resetPerformanceStats(const char* name);

    bool exportModelRegistry(String& jsonOutput);
    bool importModelRegistry(const String& jsonInput);

    void setAutoLoadEnabled(bool enabled) { autoLoadEnabled = enabled; }
    void setValidationEnabled(bool enabled) { validationEnabled = enabled; }

    bool backupModel(const char* name, const char* backupFilename);
    bool restoreModel(const char* name, const char* backupFilename);
    bool deleteModelFile(const char* name);

    // Continuous learning model management
    bool hotSwapModel(const char* newModelData, size_t size, const char* version);
    bool rollbackToPreviousModel();
    const char* getCurrentModelVersion() const;
    const char* getPreviousModelVersion() const;
    bool compareModelPerformance(const char* model1, const char* model2);
    void setMinAccuracyForSwap(float minAccuracy) { minAccuracyForSwap = minAccuracy; }
    bool shouldSwapModel(float newModelAccuracy);
    bool archiveCurrentModel();

private:
    ModelMetadata registry[MODEL_REGISTRY_SIZE];
    ModelPerformanceStats perfStats[MODEL_REGISTRY_SIZE];
    size_t modelCount;

    TFLiteEngine* tfliteEngine;
    EdgeML* edgeML;

    ModelUpdateStatus updateStatus;
    uint8_t updateProgress;
    String currentUpdateUrl;
    String currentUpdateModel;

    bool autoLoadEnabled;
    bool validationEnabled;

    // Continuous learning state
    char currentModelVersion[MODEL_VERSION_MAX_LEN];
    char previousModelVersion[MODEL_VERSION_MAX_LEN];
    float minAccuracyForSwap;
    int activeModelIndex;
    int previousModelIndex;

    int findModelIndex(const char* name);
    int findFreeSlot();
    uint32_t calculateCRC32(const uint8_t* data, size_t length);
    bool saveRegistry();
    bool loadRegistry();

    void processOTAUpdate();
    bool downloadModel(const char* url, uint8_t* buffer, size_t maxSize, size_t& downloadedSize);
    bool installModel(const char* name, const uint8_t* data, size_t size);
};

#endif
