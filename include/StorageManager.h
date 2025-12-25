#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "FaultDetector.h"

#define STORAGE_FORMAT_ON_FAIL true
#define STORAGE_MAX_FILES 100
#define STORAGE_LOG_PATH "/logs"
#define STORAGE_CONFIG_PATH "/config"
#define STORAGE_BASELINE_FILE "/config/baseline.json"
#define STORAGE_SETTINGS_FILE "/config/settings.json"

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    bool begin();
    bool format();

    bool saveBaseline(const BaselineStats& baseline);
    bool loadBaseline(BaselineStats& baseline);

    bool saveSettings(const JsonDocument& settings);
    bool loadSettings(JsonDocument& settings);

    bool saveLog(const String& filename, const String& data);
    bool appendLog(const String& filename, const String& data);
    String readLog(const String& filename);

    bool deleteFile(const String& path);
    bool fileExists(const String& path);
    size_t getFileSize(const String& path);

    void listFiles(const String& dirname = "/");
    size_t getTotalBytes();
    size_t getUsedBytes();
    size_t getFreeBytes();

    bool exportAllLogs(String& output);
    bool cleanOldLogs(uint32_t maxAgeMs);

private:
    bool initialized;

    String getLogPath(const String& filename);
    bool ensureDirectory(const String& path);
};

#endif
