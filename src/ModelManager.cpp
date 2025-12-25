#include "ModelManager.h"
#include "StorageManager.h"
#include <HTTPClient.h>

#define REGISTRY_FILENAME "/model_registry.json"
#define MODEL_DIR "/models/"

ModelManager::ModelManager()
    : modelCount(0)
    , tfliteEngine(nullptr)
    , edgeML(nullptr)
    , updateStatus(UPDATE_IDLE)
    , updateProgress(0)
    , autoLoadEnabled(true)
    , validationEnabled(true)
{
    memset(registry, 0, sizeof(registry));
    memset(perfStats, 0, sizeof(perfStats));
}

ModelManager::~ModelManager() {
}

bool ModelManager::begin() {
    DEBUG_PRINTLN("Initializing Model Manager...");

    StorageManager storage;
    if (!storage.begin()) {
        DEBUG_PRINTLN("Storage init failed for Model Manager");
        return false;
    }

    loadRegistry();

    if (autoLoadEnabled) {
        loadActiveModels();
    }

    DEBUG_PRINTLN("Model Manager initialized");
    return true;
}

void ModelManager::loop() {
    if (updateStatus == UPDATE_DOWNLOADING || updateStatus == UPDATE_VALIDATING) {
        processOTAUpdate();
    }
}

bool ModelManager::registerModel(const char* name, const char* version, TFLiteModelType type,
                                  const uint8_t* data, size_t size) {
    int existingIdx = findModelIndex(name);
    int idx = (existingIdx >= 0) ? existingIdx : findFreeSlot();

    if (idx < 0) {
        DEBUG_PRINTLN("No free slots for model registration");
        return false;
    }

    StorageManager storage;
    if (!storage.begin()) return false;

    String filename = String(MODEL_DIR) + String(name) + ".tflite";

    File file = SPIFFS.open(filename, "w");
    if (!file) {
        DEBUG_PRINT("Failed to create model file: ");
        DEBUG_PRINTLN(filename);
        return false;
    }

    size_t written = file.write(data, size);
    file.close();

    if (written != size) {
        DEBUG_PRINTLN("Failed to write complete model data");
        SPIFFS.remove(filename);
        return false;
    }

    strncpy(registry[idx].name, name, MODEL_NAME_MAX_LEN - 1);
    strncpy(registry[idx].version, version, MODEL_VERSION_MAX_LEN - 1);
    strncpy(registry[idx].filename, filename.c_str(), sizeof(registry[idx].filename) - 1);
    registry[idx].type = type;
    registry[idx].size = size;
    registry[idx].crc32 = calculateCRC32(data, size);
    registry[idx].createdTimestamp = millis();
    registry[idx].loadedTimestamp = 0;
    registry[idx].accuracy = 0.0f;
    registry[idx].isActive = false;
    registry[idx].isValid = true;

    if (existingIdx < 0) {
        modelCount++;
    }

    saveRegistry();

    DEBUG_PRINT("Model registered: ");
    DEBUG_PRINT(name);
    DEBUG_PRINT(" v");
    DEBUG_PRINTLN(version);

    return true;
}

bool ModelManager::registerModelFromFile(const char* name, const char* version,
                                          TFLiteModelType type, const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    File file = SPIFFS.open(filename, "r");
    if (!file) {
        DEBUG_PRINT("Model file not found: ");
        DEBUG_PRINTLN(filename);
        return false;
    }

    size_t fileSize = file.size();
    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        file.close();
        DEBUG_PRINTLN("Failed to allocate buffer for model");
        return false;
    }

    size_t bytesRead = file.read(buffer, fileSize);
    file.close();

    if (bytesRead != fileSize) {
        free(buffer);
        return false;
    }

    int idx = findFreeSlot();
    if (idx < 0) {
        free(buffer);
        return false;
    }

    strncpy(registry[idx].name, name, MODEL_NAME_MAX_LEN - 1);
    strncpy(registry[idx].version, version, MODEL_VERSION_MAX_LEN - 1);
    strncpy(registry[idx].filename, filename, sizeof(registry[idx].filename) - 1);
    registry[idx].type = type;
    registry[idx].size = fileSize;
    registry[idx].crc32 = calculateCRC32(buffer, fileSize);
    registry[idx].createdTimestamp = millis();
    registry[idx].isActive = false;
    registry[idx].isValid = true;

    modelCount++;

    free(buffer);
    saveRegistry();

    return true;
}

bool ModelManager::unregisterModel(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0) return false;

    if (registry[idx].isActive) {
        deactivateModel(name);
    }

    SPIFFS.remove(registry[idx].filename);

    memset(&registry[idx], 0, sizeof(ModelMetadata));
    memset(&perfStats[idx], 0, sizeof(ModelPerformanceStats));

    modelCount--;
    saveRegistry();

    return true;
}

bool ModelManager::activateModel(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0 || !registry[idx].isValid) return false;

#ifdef USE_TFLITE
    if (tfliteEngine) {
        if (!tfliteEngine->loadModelFromFlash(registry[idx].filename, registry[idx].type)) {
            DEBUG_PRINT("Failed to load model: ");
            DEBUG_PRINTLN(name);
            return false;
        }
    }
#endif

    registry[idx].isActive = true;
    registry[idx].loadedTimestamp = millis();
    saveRegistry();

    DEBUG_PRINT("Model activated: ");
    DEBUG_PRINTLN(name);

    return true;
}

bool ModelManager::deactivateModel(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0) return false;

#ifdef USE_TFLITE
    if (tfliteEngine) {
        tfliteEngine->unloadModel(registry[idx].type);
    }
#endif

    registry[idx].isActive = false;
    saveRegistry();

    return true;
}

bool ModelManager::loadActiveModels() {
    bool allLoaded = true;

    for (size_t i = 0; i < MODEL_REGISTRY_SIZE; i++) {
        if (registry[i].isValid && registry[i].isActive) {
            if (!activateModel(registry[i].name)) {
                allLoaded = false;
            }
        }
    }

    return allLoaded;
}

ModelMetadata* ModelManager::getModelInfo(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0) return nullptr;
    return &registry[idx];
}

ModelMetadata* ModelManager::getModelByIndex(size_t index) {
    if (index >= MODEL_REGISTRY_SIZE || !registry[index].isValid) return nullptr;
    return &registry[index];
}

bool ModelManager::startOTAUpdate(const char* url, const char* modelName) {
    if (updateStatus != UPDATE_IDLE) {
        DEBUG_PRINTLN("Update already in progress");
        return false;
    }

    currentUpdateUrl = String(url);
    currentUpdateModel = String(modelName);
    updateStatus = UPDATE_DOWNLOADING;
    updateProgress = 0;

    DEBUG_PRINT("Starting OTA update for model: ");
    DEBUG_PRINTLN(modelName);

    return true;
}

void ModelManager::cancelUpdate() {
    updateStatus = UPDATE_IDLE;
    updateProgress = 0;
    currentUpdateUrl = "";
    currentUpdateModel = "";
}

void ModelManager::processOTAUpdate() {
    if (updateStatus != UPDATE_DOWNLOADING) return;

    HTTPClient http;
    http.begin(currentUpdateUrl);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        DEBUG_PRINT("HTTP error: ");
        DEBUG_PRINTLN(httpCode);
        updateStatus = UPDATE_FAILED;
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        DEBUG_PRINTLN("Invalid content length");
        updateStatus = UPDATE_FAILED;
        http.end();
        return;
    }

    uint8_t* buffer = (uint8_t*)malloc(contentLength);
    if (!buffer) {
        DEBUG_PRINTLN("Failed to allocate download buffer");
        updateStatus = UPDATE_FAILED;
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;

    while (http.connected() && bytesRead < (size_t)contentLength) {
        size_t available = stream->available();
        if (available > 0) {
            size_t toRead = min(available, (size_t)(contentLength - bytesRead));
            stream->readBytes(buffer + bytesRead, toRead);
            bytesRead += toRead;
            updateProgress = (bytesRead * 100) / contentLength;
        }
        yield();
    }

    http.end();

    if (bytesRead != (size_t)contentLength) {
        DEBUG_PRINTLN("Download incomplete");
        free(buffer);
        updateStatus = UPDATE_FAILED;
        return;
    }

    updateStatus = UPDATE_VALIDATING;
    updateProgress = 95;

    if (validationEnabled) {
        if (!validateModelData(buffer, bytesRead, 0)) {
            DEBUG_PRINTLN("Model validation failed");
            free(buffer);
            updateStatus = UPDATE_FAILED;
            return;
        }
    }

    updateStatus = UPDATE_INSTALLING;
    updateProgress = 98;

    if (registerModel(currentUpdateModel.c_str(), "ota", MODEL_CLASSIFIER, buffer, bytesRead)) {
        activateModel(currentUpdateModel.c_str());
        updateStatus = UPDATE_COMPLETE;
        updateProgress = 100;
    } else {
        updateStatus = UPDATE_FAILED;
    }

    free(buffer);
}

bool ModelManager::validateModel(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0) return false;

    File file = SPIFFS.open(registry[idx].filename, "r");
    if (!file) return false;

    size_t fileSize = file.size();
    if (fileSize != registry[idx].size) {
        file.close();
        registry[idx].isValid = false;
        return false;
    }

    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        file.close();
        return false;
    }

    file.read(buffer, fileSize);
    file.close();

    uint32_t crc = calculateCRC32(buffer, fileSize);
    free(buffer);

    bool valid = (crc == registry[idx].crc32);
    registry[idx].isValid = valid;

    return valid;
}

bool ModelManager::validateModelData(const uint8_t* data, size_t size, uint32_t expectedCrc) {
    if (!data || size < 8) return false;

    if (data[0] != 0x18 || data[1] != 0x00 || data[2] != 0x00 || data[3] != 0x00) {
        DEBUG_PRINTLN("Invalid TFLite header");
        return false;
    }

    if (expectedCrc != 0) {
        uint32_t actualCrc = calculateCRC32(data, size);
        if (actualCrc != expectedCrc) {
            DEBUG_PRINTLN("CRC mismatch");
            return false;
        }
    }

    return true;
}

void ModelManager::recordInference(const char* name, float inferenceTimeMs, bool correct) {
    int idx = findModelIndex(name);
    if (idx < 0) return;

    perfStats[idx].totalInferences++;
    if (correct) {
        perfStats[idx].correctPredictions++;
    }

    float n = (float)perfStats[idx].totalInferences;
    perfStats[idx].avgInferenceTimeMs =
        perfStats[idx].avgInferenceTimeMs * ((n - 1) / n) + inferenceTimeMs / n;

    if (inferenceTimeMs > perfStats[idx].maxInferenceTimeMs) {
        perfStats[idx].maxInferenceTimeMs = inferenceTimeMs;
    }
    if (perfStats[idx].minInferenceTimeMs == 0 || inferenceTimeMs < perfStats[idx].minInferenceTimeMs) {
        perfStats[idx].minInferenceTimeMs = inferenceTimeMs;
    }

    perfStats[idx].lastInferenceTime = millis();

    if (perfStats[idx].totalInferences > 0) {
        registry[idx].accuracy = (float)perfStats[idx].correctPredictions / perfStats[idx].totalInferences;
    }
}

ModelPerformanceStats ModelManager::getPerformanceStats(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0) {
        ModelPerformanceStats empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return perfStats[idx];
}

void ModelManager::resetPerformanceStats(const char* name) {
    int idx = findModelIndex(name);
    if (idx >= 0) {
        memset(&perfStats[idx], 0, sizeof(ModelPerformanceStats));
    }
}

bool ModelManager::exportModelRegistry(String& jsonOutput) {
    jsonOutput = "{\"models\":[";

    bool first = true;
    for (size_t i = 0; i < MODEL_REGISTRY_SIZE; i++) {
        if (!registry[i].isValid) continue;

        if (!first) jsonOutput += ",";
        first = false;

        jsonOutput += "{";
        jsonOutput += "\"name\":\"" + String(registry[i].name) + "\",";
        jsonOutput += "\"version\":\"" + String(registry[i].version) + "\",";
        jsonOutput += "\"type\":" + String((int)registry[i].type) + ",";
        jsonOutput += "\"size\":" + String(registry[i].size) + ",";
        jsonOutput += "\"crc32\":" + String(registry[i].crc32) + ",";
        jsonOutput += "\"active\":" + String(registry[i].isActive ? "true" : "false") + ",";
        jsonOutput += "\"accuracy\":" + String(registry[i].accuracy, 4);
        jsonOutput += "}";
    }

    jsonOutput += "]}";
    return true;
}

bool ModelManager::importModelRegistry(const String& jsonInput) {
    return true;
}

bool ModelManager::backupModel(const char* name, const char* backupFilename) {
    int idx = findModelIndex(name);
    if (idx < 0) return false;

    File src = SPIFFS.open(registry[idx].filename, "r");
    if (!src) return false;

    File dst = SPIFFS.open(backupFilename, "w");
    if (!dst) {
        src.close();
        return false;
    }

    uint8_t buffer[512];
    while (src.available()) {
        size_t bytesRead = src.read(buffer, sizeof(buffer));
        dst.write(buffer, bytesRead);
    }

    src.close();
    dst.close();

    return true;
}

bool ModelManager::restoreModel(const char* name, const char* backupFilename) {
    int idx = findModelIndex(name);
    if (idx < 0) return false;

    File src = SPIFFS.open(backupFilename, "r");
    if (!src) return false;

    File dst = SPIFFS.open(registry[idx].filename, "w");
    if (!dst) {
        src.close();
        return false;
    }

    uint8_t buffer[512];
    while (src.available()) {
        size_t bytesRead = src.read(buffer, sizeof(buffer));
        dst.write(buffer, bytesRead);
    }

    src.close();
    dst.close();

    return validateModel(name);
}

bool ModelManager::deleteModelFile(const char* name) {
    int idx = findModelIndex(name);
    if (idx < 0) return false;

    return SPIFFS.remove(registry[idx].filename);
}

int ModelManager::findModelIndex(const char* name) {
    for (size_t i = 0; i < MODEL_REGISTRY_SIZE; i++) {
        if (registry[i].isValid && strcmp(registry[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int ModelManager::findFreeSlot() {
    for (size_t i = 0; i < MODEL_REGISTRY_SIZE; i++) {
        if (!registry[i].isValid) {
            return i;
        }
    }
    return -1;
}

uint32_t ModelManager::calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    static const uint32_t table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
    };

    for (size_t i = 0; i < length; i++) {
        crc = table[(crc ^ data[i]) & 0x0F] ^ (crc >> 4);
        crc = table[(crc ^ (data[i] >> 4)) & 0x0F] ^ (crc >> 4);
    }

    return ~crc;
}

bool ModelManager::saveRegistry() {
    File file = SPIFFS.open(REGISTRY_FILENAME, "w");
    if (!file) return false;

    String json;
    exportModelRegistry(json);
    file.print(json);
    file.close();

    return true;
}

bool ModelManager::loadRegistry() {
    File file = SPIFFS.open(REGISTRY_FILENAME, "r");
    if (!file) return false;

    String json = file.readString();
    file.close();

    return true;
}
