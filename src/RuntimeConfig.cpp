#include "RuntimeConfig.h"
#include "StorageManager.h"

RuntimeConfigManager::RuntimeConfigManager()
    : initialized(false)
{
    setDefaults();
}

bool RuntimeConfigManager::begin() {
    setDefaults();

    if (!load()) {
        save();
    }

    initialized = true;
    return true;
}

bool RuntimeConfigManager::save() {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    StaticJsonDocument<2048> doc;
    populateJson(doc.as<JsonVariant>(), true);
    return storage.saveSettings(doc);
}

bool RuntimeConfigManager::load() {
    StorageManager storage;
    if (!storage.begin()) {
        return false;
    }

    DynamicJsonDocument doc(2048);
    if (!storage.loadSettings(doc)) {
        return false;
    }

    bool restartRequired = false;
    String error;
    return updateFromJson(doc.as<JsonVariantConst>(), restartRequired, error);
}

void RuntimeConfigManager::setDefaults() {
    memset(&settings, 0, sizeof(settings));

    copyString(settings.deviceName, sizeof(settings.deviceName), DEVICE_NAME);
    copyString(settings.deviceId, sizeof(settings.deviceId), DEVICE_ID);

    settings.warningThreshold = THRESHOLD_MULTIPLIER_WARNING;
    settings.criticalThreshold = THRESHOLD_MULTIPLIER_CRITICAL;
    settings.webBroadcastIntervalMs = 250;

    settings.wifiEnabled = WIFI_ENABLED || strcmp(WIFI_SSID, "your_ssid") == 0 || WIFI_SSID[0] == '\0';
    copyString(settings.wifiSsid, sizeof(settings.wifiSsid), WIFI_SSID);
    copyString(settings.wifiPassword, sizeof(settings.wifiPassword), WIFI_PASSWORD);

    settings.mqttEnabled = MQTT_ENABLED;
    copyString(settings.mqttBroker, sizeof(settings.mqttBroker), MQTT_BROKER_ADDRESS);
    settings.mqttPort = MQTT_BROKER_PORT;
    copyString(settings.mqttUser, sizeof(settings.mqttUser), MQTT_USER);
    copyString(settings.mqttPassword, sizeof(settings.mqttPassword), MQTT_PASSWORD);

    settings.otaEnabled = OTA_ENABLED;
    copyString(settings.otaPassword, sizeof(settings.otaPassword), OTA_PASSWORD);
}

void RuntimeConfigManager::populateJson(JsonVariant root, bool includeSecrets) const {
    root["deviceName"] = settings.deviceName;
    root["deviceId"] = settings.deviceId;

    JsonObject thresholds = root["thresholds"].to<JsonObject>();
    thresholds["warning"] = settings.warningThreshold;
    thresholds["critical"] = settings.criticalThreshold;

    JsonObject dashboard = root["dashboard"].to<JsonObject>();
    dashboard["broadcastIntervalMs"] = settings.webBroadcastIntervalMs;

    JsonObject wifi = root["wifi"].to<JsonObject>();
    wifi["enabled"] = settings.wifiEnabled;
    wifi["ssid"] = settings.wifiSsid;
    wifi["passwordSet"] = settings.wifiPassword[0] != '\0';
    if (includeSecrets) {
        wifi["password"] = settings.wifiPassword;
    }

    JsonObject mqtt = root["mqtt"].to<JsonObject>();
    mqtt["enabled"] = settings.mqttEnabled;
    mqtt["broker"] = settings.mqttBroker;
    mqtt["port"] = settings.mqttPort;
    mqtt["user"] = settings.mqttUser;
    mqtt["passwordSet"] = settings.mqttPassword[0] != '\0';
    if (includeSecrets) {
        mqtt["password"] = settings.mqttPassword;
    }

    JsonObject ota = root["ota"].to<JsonObject>();
    ota["enabled"] = settings.otaEnabled;
    ota["passwordSet"] = settings.otaPassword[0] != '\0';
    if (includeSecrets) {
        ota["password"] = settings.otaPassword;
    }
}

bool RuntimeConfigManager::updateFromJson(JsonVariantConst json,
                                          bool& restartRequired,
                                          String& error) {
    if (json.isNull()) {
        error = "Missing configuration payload";
        return false;
    }

    JsonObjectConst obj = json.as<JsonObjectConst>();
    if (obj.isNull()) {
        error = "Configuration payload must be a JSON object";
        return false;
    }

    if (obj.containsKey("deviceName")) {
        copyString(settings.deviceName, sizeof(settings.deviceName), obj["deviceName"] | settings.deviceName);
    }

    if (obj.containsKey("deviceId")) {
        const char* value = obj["deviceId"] | settings.deviceId;
        if (strncmp(settings.deviceId, value, sizeof(settings.deviceId)) != 0) {
            copyString(settings.deviceId, sizeof(settings.deviceId), value);
            restartRequired = true;
        }
    }

    JsonObjectConst thresholds = obj["thresholds"].as<JsonObjectConst>();
    if (!thresholds.isNull()) {
        settings.warningThreshold = thresholds["warning"] | settings.warningThreshold;
        settings.criticalThreshold = thresholds["critical"] | settings.criticalThreshold;
    }

    if (settings.warningThreshold <= 0.0f || settings.criticalThreshold <= 0.0f) {
        error = "Thresholds must be positive";
        return false;
    }
    if (settings.warningThreshold >= settings.criticalThreshold) {
        error = "Critical threshold must be greater than warning threshold";
        return false;
    }

    JsonObjectConst dashboard = obj["dashboard"].as<JsonObjectConst>();
    if (!dashboard.isNull()) {
        settings.webBroadcastIntervalMs = dashboard["broadcastIntervalMs"] | settings.webBroadcastIntervalMs;
        if (settings.webBroadcastIntervalMs < 100) {
            settings.webBroadcastIntervalMs = 100;
        }
        if (settings.webBroadcastIntervalMs > 5000) {
            settings.webBroadcastIntervalMs = 5000;
        }
    }

    JsonObjectConst wifi = obj["wifi"].as<JsonObjectConst>();
    if (!wifi.isNull()) {
        bool newEnabled = wifi["enabled"] | settings.wifiEnabled;
        if (newEnabled != settings.wifiEnabled) {
            settings.wifiEnabled = newEnabled;
            restartRequired = true;
        }

        updateStringField(wifi, "ssid", settings.wifiSsid, sizeof(settings.wifiSsid), restartRequired, true);
        if (wifi.containsKey("password")) {
            const char* password = wifi["password"] | "";
            if (password[0] != '\0' && strncmp(settings.wifiPassword, password, sizeof(settings.wifiPassword)) != 0) {
                copyString(settings.wifiPassword, sizeof(settings.wifiPassword), password);
                restartRequired = true;
            }
        }
    }

    JsonObjectConst mqtt = obj["mqtt"].as<JsonObjectConst>();
    if (!mqtt.isNull()) {
        bool newEnabled = mqtt["enabled"] | settings.mqttEnabled;
        if (newEnabled != settings.mqttEnabled) {
            settings.mqttEnabled = newEnabled;
            restartRequired = true;
        }

        updateStringField(mqtt, "broker", settings.mqttBroker, sizeof(settings.mqttBroker), restartRequired, true);
        updateStringField(mqtt, "user", settings.mqttUser, sizeof(settings.mqttUser), restartRequired, true);

        uint16_t newPort = mqtt["port"] | settings.mqttPort;
        if (newPort == 0) {
            error = "MQTT port must be greater than zero";
            return false;
        }
        if (newPort != settings.mqttPort) {
            settings.mqttPort = newPort;
            restartRequired = true;
        }

        if (mqtt.containsKey("password")) {
            const char* password = mqtt["password"] | "";
            if (password[0] != '\0' && strncmp(settings.mqttPassword, password, sizeof(settings.mqttPassword)) != 0) {
                copyString(settings.mqttPassword, sizeof(settings.mqttPassword), password);
                restartRequired = true;
            }
        }
    }

    JsonObjectConst ota = obj["ota"].as<JsonObjectConst>();
    if (!ota.isNull()) {
        bool newEnabled = ota["enabled"] | settings.otaEnabled;
        if (newEnabled != settings.otaEnabled) {
            settings.otaEnabled = newEnabled;
            restartRequired = true;
        }

        if (ota.containsKey("password")) {
            const char* password = ota["password"] | "";
            if (password[0] != '\0' && strncmp(settings.otaPassword, password, sizeof(settings.otaPassword)) != 0) {
                copyString(settings.otaPassword, sizeof(settings.otaPassword), password);
                restartRequired = true;
            }
        }
    }

    return true;
}

void RuntimeConfigManager::copyString(char* dest, size_t destSize, const char* value) {
    if (!dest || destSize == 0) {
        return;
    }

    if (!value) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, value, destSize - 1);
    dest[destSize - 1] = '\0';
}

bool RuntimeConfigManager::updateStringField(JsonObjectConst obj, const char* key,
                                             char* dest, size_t destSize,
                                             bool& restartRequired,
                                             bool triggersRestart) {
    if (!obj.containsKey(key)) {
        return false;
    }

    const char* value = obj[key] | "";
    if (strncmp(dest, value, destSize) == 0) {
        return false;
    }

    copyString(dest, destSize, value);
    if (triggersRestart) {
        restartRequired = true;
    }
    return true;
}
