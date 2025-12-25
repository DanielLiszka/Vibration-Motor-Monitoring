#include "StorageManager.h"

StorageManager::StorageManager() : initialized(false) {
}

StorageManager::~StorageManager() {
    if (initialized) {
        SPIFFS.end();
    }
}

bool StorageManager::begin() {
    DEBUG_PRINTLN("Initializing Storage Manager...");

    if (!SPIFFS.begin(STORAGE_FORMAT_ON_FAIL)) {
        DEBUG_PRINTLN("SPIFFS mount failed");
        return false;
    }

    ensureDirectory(STORAGE_LOG_PATH);
    ensureDirectory(STORAGE_CONFIG_PATH);

    initialized = true;

    DEBUG_PRINTF("Storage initialized. Total: %u bytes, Used: %u bytes\n",
                 getTotalBytes(), getUsedBytes());

    return true;
}

bool StorageManager::format() {
    DEBUG_PRINTLN("Formatting SPIFFS...");
    return SPIFFS.format();
}

bool StorageManager::saveBaseline(const BaselineStats& baseline) {
    if (!initialized) return false;

    StaticJsonDocument<2048> doc;

    doc["isCalibrated"] = baseline.isCalibrated;
    doc["sampleCount"] = baseline.sampleCount;

    JsonArray meanArray = doc.createNestedArray("mean");
    JsonArray stdDevArray = doc.createNestedArray("stdDev");
    JsonArray minArray = doc.createNestedArray("min");
    JsonArray maxArray = doc.createNestedArray("max");

    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        meanArray.add(baseline.mean[i]);
        stdDevArray.add(baseline.stdDev[i]);
        minArray.add(baseline.min[i]);
        maxArray.add(baseline.max[i]);
    }

    File file = SPIFFS.open(STORAGE_BASELINE_FILE, FILE_WRITE);
    if (!file) {
        DEBUG_PRINTLN("Failed to open baseline file for writing");
        return false;
    }

    serializeJson(doc, file);
    file.close();

    DEBUG_PRINTLN("Baseline saved to flash");
    return true;
}

bool StorageManager::loadBaseline(BaselineStats& baseline) {
    if (!initialized) return false;

    if (!SPIFFS.exists(STORAGE_BASELINE_FILE)) {
        DEBUG_PRINTLN("Baseline file not found");
        return false;
    }

    File file = SPIFFS.open(STORAGE_BASELINE_FILE, FILE_READ);
    if (!file) {
        DEBUG_PRINTLN("Failed to open baseline file for reading");
        return false;
    }

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        DEBUG_PRINTF("Failed to parse baseline: %s\n", error.c_str());
        return false;
    }

    baseline.isCalibrated = doc["isCalibrated"];
    baseline.sampleCount = doc["sampleCount"];

    JsonArray meanArray = doc["mean"];
    JsonArray stdDevArray = doc["stdDev"];
    JsonArray minArray = doc["min"];
    JsonArray maxArray = doc["max"];

    for (int i = 0; i < NUM_TOTAL_FEATURES && i < meanArray.size(); i++) {
        baseline.mean[i] = meanArray[i];
        baseline.stdDev[i] = stdDevArray[i];
        baseline.min[i] = minArray[i];
        baseline.max[i] = maxArray[i];
    }

    DEBUG_PRINTLN("Baseline loaded from flash");
    return true;
}

bool StorageManager::saveSettings(const JsonDocument& settings) {
    if (!initialized) return false;

    File file = SPIFFS.open(STORAGE_SETTINGS_FILE, FILE_WRITE);
    if (!file) {
        DEBUG_PRINTLN("Failed to open settings file for writing");
        return false;
    }

    serializeJson(settings, file);
    file.close();

    return true;
}

bool StorageManager::loadSettings(JsonDocument& settings) {
    if (!initialized) return false;

    if (!SPIFFS.exists(STORAGE_SETTINGS_FILE)) {
        return false;
    }

    File file = SPIFFS.open(STORAGE_SETTINGS_FILE, FILE_READ);
    if (!file) {
        return false;
    }

    DeserializationError error = deserializeJson(settings, file);
    file.close();

    return !error;
}

bool StorageManager::saveLog(const String& filename, const String& data) {
    if (!initialized) return false;

    String path = getLogPath(filename);
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        return false;
    }

    file.print(data);
    file.close();

    return true;
}

bool StorageManager::appendLog(const String& filename, const String& data) {
    if (!initialized) return false;

    String path = getLogPath(filename);
    File file = SPIFFS.open(path, FILE_APPEND);
    if (!file) {
        return false;
    }

    file.print(data);
    file.close();

    return true;
}

String StorageManager::readLog(const String& filename) {
    if (!initialized) return "";

    String path = getLogPath(filename);
    if (!SPIFFS.exists(path)) {
        return "";
    }

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        return "";
    }

    String content = file.readString();
    file.close();

    return content;
}

bool StorageManager::deleteFile(const String& path) {
    if (!initialized) return false;
    return SPIFFS.remove(path);
}

bool StorageManager::fileExists(const String& path) {
    if (!initialized) return false;
    return SPIFFS.exists(path);
}

size_t StorageManager::getFileSize(const String& path) {
    if (!initialized) return 0;

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return 0;

    size_t size = file.size();
    file.close();

    return size;
}

void StorageManager::listFiles(const String& dirname) {
    if (!initialized) return;

    File root = SPIFFS.open(dirname);
    if (!root) {
        DEBUG_PRINTLN("Failed to open directory");
        return;
    }

    if (!root.isDirectory()) {
        DEBUG_PRINTLN("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            DEBUG_PRINTF("  DIR : %s\n", file.name());
        } else {
            DEBUG_PRINTF("  FILE: %s  SIZE: %u\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

size_t StorageManager::getTotalBytes() {
    return SPIFFS.totalBytes();
}

size_t StorageManager::getUsedBytes() {
    return SPIFFS.usedBytes();
}

size_t StorageManager::getFreeBytes() {
    return getTotalBytes() - getUsedBytes();
}

bool StorageManager::exportAllLogs(String& output) {
    if (!initialized) return false;

    output = "";
    File root = SPIFFS.open(STORAGE_LOG_PATH);
    if (!root || !root.isDirectory()) {
        return false;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            output += "=== ";
            output += file.name();
            output += " ===\n";
            output += file.readString();
            output += "\n\n";
        }
        file = root.openNextFile();
    }

    return true;
}

bool StorageManager::cleanOldLogs(uint32_t maxAgeMs) {
    if (!initialized) return false;

    uint32_t currentTime = millis();
    File root = SPIFFS.open(STORAGE_LOG_PATH);
    if (!root || !root.isDirectory()) {
        return false;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            time_t fileTime = file.getLastWrite();
            if (currentTime - (fileTime * 1000) > maxAgeMs) {
                String name = file.name();
                file.close();
                SPIFFS.remove(name);
                DEBUG_PRINTF("Deleted old log: %s\n", name.c_str());
            }
        }
        file = root.openNextFile();
    }

    return true;
}

String StorageManager::getLogPath(const String& filename) {
    if (filename.startsWith("/")) {
        return filename;
    }
    return String(STORAGE_LOG_PATH) + "/" + filename;
}

bool StorageManager::ensureDirectory(const String& path) {
    if (SPIFFS.exists(path)) {
        return true;
    }

    return SPIFFS.mkdir(path);
}
