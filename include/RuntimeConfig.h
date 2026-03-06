#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

struct RuntimeSettings {
    char deviceName[32];
    char deviceId[64];

    float warningThreshold;
    float criticalThreshold;
    uint32_t webBroadcastIntervalMs;

    bool wifiEnabled;
    char wifiSsid[64];
    char wifiPassword[64];

    bool mqttEnabled;
    char mqttBroker[128];
    uint16_t mqttPort;
    char mqttUser[64];
    char mqttPassword[128];

    bool otaEnabled;
    char otaPassword[64];
};

class RuntimeConfigManager {
public:
    RuntimeConfigManager();

    bool begin();
    bool save();

    const RuntimeSettings& getSettings() const { return settings; }
    RuntimeSettings& getMutableSettings() { return settings; }

    void populateJson(JsonVariant root, bool includeSecrets = false) const;
    bool updateFromJson(JsonVariantConst json, bool& restartRequired, String& error);

private:
    RuntimeSettings settings;
    bool initialized;

    void setDefaults();
    bool load();
    static void copyString(char* dest, size_t destSize, const char* value);
    static bool updateStringField(JsonObjectConst obj, const char* key,
                                  char* dest, size_t destSize,
                                  bool& restartRequired,
                                  bool triggersRestart);
};

#endif
