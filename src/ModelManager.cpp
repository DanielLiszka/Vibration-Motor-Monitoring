#include "ModelManager.h"
#include "StorageManager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

#define REGISTRY_FILENAME "/model_registry.json"
#define MODEL_DIR "/models/"
#define MODEL_DOWNLOAD_TIMEOUT_MS 15000

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
    memset(currentModelVersion, 0, sizeof(currentModelVersion));
    memset(previousModelVersion, 0, sizeof(previousModelVersion));
    minAccuracyForSwap = 0.70f;
    activeModelIndex = -1;
    previousModelIndex = -1;
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

    SPIFFS.mkdir("/models");
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

    if (registry[idx].type == MODEL_CLASSIFIER) {
        activeModelIndex = idx;
        strncpy(currentModelVersion, registry[idx].version, MODEL_VERSION_MAX_LEN - 1);
    }

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

    if (idx == activeModelIndex) {
        activeModelIndex = -1;
        currentModelVersion[0] = '\0';
    }

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
    StaticJsonDocument<4096> doc;
    JsonArray models = doc.createNestedArray("models");

    for (size_t i = 0; i < MODEL_REGISTRY_SIZE; i++) {
        if (!registry[i].isValid) continue;

        JsonObject m = models.createNestedObject();
        m["name"] = registry[i].name;
        m["version"] = registry[i].version;
        m["filename"] = registry[i].filename;
        m["type"] = static_cast<int>(registry[i].type);
        m["size"] = registry[i].size;
        m["crc32"] = registry[i].crc32;
        m["created"] = registry[i].createdTimestamp;
        m["loaded"] = registry[i].loadedTimestamp;
        m["active"] = registry[i].isActive;
        m["accuracy"] = registry[i].accuracy;
        m["valid"] = registry[i].isValid;
    }

    serializeJson(doc, jsonOutput);
    return true;
}

bool ModelManager::importModelRegistry(const String& jsonInput) {
    if (jsonInput.length() == 0) return false;

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, jsonInput);
    if (err) {
        DEBUG_PRINT("Failed to parse model registry: ");
        DEBUG_PRINTLN(err.c_str());
        return false;
    }

    JsonArray models = doc["models"].as<JsonArray>();
    if (models.isNull()) return false;

    memset(registry, 0, sizeof(registry));
    memset(perfStats, 0, sizeof(perfStats));
    modelCount = 0;
    activeModelIndex = -1;
    previousModelIndex = -1;
    currentModelVersion[0] = '\0';
    previousModelVersion[0] = '\0';

    for (JsonObject m : models) {
        const char* name = m["name"] | "";
        if (!name || name[0] == '\0') continue;

        int idx = findFreeSlot();
        if (idx < 0) {
            DEBUG_PRINTLN("Model registry full while importing");
            break;
        }

        const char* version = m["version"] | "";
        const char* filename = m["filename"] | "";
        const int type = m["type"] | 0;

        strncpy(registry[idx].name, name, MODEL_NAME_MAX_LEN - 1);
        strncpy(registry[idx].version, version, MODEL_VERSION_MAX_LEN - 1);

        if (filename && filename[0]) {
            strncpy(registry[idx].filename, filename, sizeof(registry[idx].filename) - 1);
        } else {
            String derived = String(MODEL_DIR) + String(name) + ".tflite";
            strncpy(registry[idx].filename, derived.c_str(), sizeof(registry[idx].filename) - 1);
        }

        registry[idx].type = static_cast<TFLiteModelType>(type);
        registry[idx].size = m["size"] | 0;
        registry[idx].crc32 = m["crc32"] | 0;
        registry[idx].createdTimestamp = m["created"] | 0;
        registry[idx].loadedTimestamp = m["loaded"] | 0;
        registry[idx].accuracy = m["accuracy"] | 0.0f;
        registry[idx].isActive = m["active"] | false;
        registry[idx].isValid = m["valid"] | true;

        if (registry[idx].isValid) {
            modelCount++;
        }
    }

    for (size_t i = 0; i < MODEL_REGISTRY_SIZE; i++) {
        if (registry[i].isValid && registry[i].isActive) {
            activeModelIndex = (int)i;
            strncpy(currentModelVersion, registry[i].version, MODEL_VERSION_MAX_LEN - 1);
            break;
        }
    }

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
    if (!file) {
        memset(registry, 0, sizeof(registry));
        memset(perfStats, 0, sizeof(perfStats));
        modelCount = 0;
        activeModelIndex = -1;
        previousModelIndex = -1;
        currentModelVersion[0] = '\0';
        previousModelVersion[0] = '\0';
        return true;
    }

    String json = file.readString();
    file.close();

    return importModelRegistry(json);
}

bool ModelManager::downloadModel(const char* url, uint8_t* buffer, size_t maxSize, size_t& downloadedSize) {
    downloadedSize = 0;
    if (!url || !buffer || maxSize == 0) return false;

    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    if (contentLength > 0 && (size_t)contentLength > maxSize) {
        http.end();
        return false;
    }

    uint32_t start = millis();
    while (http.connected() && (contentLength < 0 || downloadedSize < (size_t)contentLength)) {
        size_t available = stream->available();
        if (available > 0) {
            size_t toRead = available;
            if (contentLength > 0) {
                toRead = min(toRead, (size_t)contentLength - downloadedSize);
            }
            toRead = min(toRead, maxSize - downloadedSize);

            if (toRead == 0) break;

            size_t read = stream->readBytes(buffer + downloadedSize, toRead);
            downloadedSize += read;
        } else {
            if (millis() - start > MODEL_DOWNLOAD_TIMEOUT_MS) break;
            delay(1);
        }
        yield();
    }

    http.end();

    if (contentLength > 0) {
        return downloadedSize == (size_t)contentLength;
    }
    return downloadedSize > 0;
}

bool ModelManager::installModel(const char* name, const uint8_t* data, size_t size) {
    if (!name || !data || size == 0) return false;

    if (!registerModel(name, "downloaded", MODEL_CLASSIFIER, data, size)) {
        return false;
    }
    return activateModel(name);
}

static void sanitizeModelName(const char* version, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!version) return;

    size_t pos = 0;
    for (size_t i = 0; version[i] != '\0' && pos + 1 < outSize; i++) {
        const char c = version[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            out[pos++] = c;
        } else {
            out[pos++] = '_';
        }
    }
    out[pos] = '\0';
}

bool ModelManager::archiveCurrentModel() {
    if (activeModelIndex < 0 || activeModelIndex >= (int)MODEL_REGISTRY_SIZE) return false;
    if (!registry[activeModelIndex].isValid) return false;

    char safeVer[MODEL_VERSION_MAX_LEN * 2];
    sanitizeModelName(registry[activeModelIndex].version, safeVer, sizeof(safeVer));

    String backupFilename = String(MODEL_DIR) + "archive_";
    backupFilename += String(registry[activeModelIndex].name);
    backupFilename += "_";
    backupFilename += String(safeVer);
    backupFilename += ".tflite";

    return backupModel(registry[activeModelIndex].name, backupFilename.c_str());
}

bool ModelManager::hotSwapModel(const char* newModelData, size_t size, const char* version) {
    if (!newModelData || size == 0 || !version || version[0] == '\0') return false;

    if (validationEnabled) {
        if (!validateModelData(reinterpret_cast<const uint8_t*>(newModelData), size, 0)) {
            DEBUG_PRINTLN("New model failed validation");
            return false;
        }
    }

    previousModelIndex = activeModelIndex;
    strncpy(previousModelVersion, currentModelVersion, MODEL_VERSION_MAX_LEN - 1);

    if (previousModelIndex >= 0) {
        archiveCurrentModel();
        deactivateModel(registry[previousModelIndex].name);
    }

    char safeVer[MODEL_VERSION_MAX_LEN * 2];
    sanitizeModelName(version, safeVer, sizeof(safeVer));

    char modelName[MODEL_NAME_MAX_LEN];
    snprintf(modelName, sizeof(modelName), "clf_%s", safeVer);

    if (!registerModel(modelName, version, MODEL_CLASSIFIER, reinterpret_cast<const uint8_t*>(newModelData), size)) {
        DEBUG_PRINTLN("Failed to register new model for hot-swap");
        return false;
    }

    if (!activateModel(modelName)) {
        DEBUG_PRINTLN("Failed to activate new model for hot-swap");
        return false;
    }

    activeModelIndex = findModelIndex(modelName);
    strncpy(currentModelVersion, version, MODEL_VERSION_MAX_LEN - 1);

    return true;
}

bool ModelManager::rollbackToPreviousModel() {
    if (previousModelIndex < 0 || previousModelIndex >= (int)MODEL_REGISTRY_SIZE) return false;
    if (!registry[previousModelIndex].isValid) return false;

    if (activeModelIndex >= 0 && activeModelIndex < (int)MODEL_REGISTRY_SIZE) {
        deactivateModel(registry[activeModelIndex].name);
    }

    if (!activateModel(registry[previousModelIndex].name)) {
        return false;
    }

    activeModelIndex = previousModelIndex;
    strncpy(currentModelVersion, registry[activeModelIndex].version, MODEL_VERSION_MAX_LEN - 1);

    previousModelIndex = -1;
    previousModelVersion[0] = '\0';
    return true;
}

const char* ModelManager::getCurrentModelVersion() const {
    return currentModelVersion;
}

const char* ModelManager::getPreviousModelVersion() const {
    return previousModelVersion;
}

bool ModelManager::compareModelPerformance(const char* model1, const char* model2) {
    ModelMetadata* m1 = getModelInfo(model1);
    ModelMetadata* m2 = getModelInfo(model2);
    if (!m1 || !m2) return false;
    return m1->accuracy >= m2->accuracy;
}

bool ModelManager::shouldSwapModel(float newModelAccuracy) {
    return newModelAccuracy >= minAccuracyForSwap;
}
