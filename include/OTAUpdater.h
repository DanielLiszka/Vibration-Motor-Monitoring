#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "Config.h"

#define OTA_DEFAULT_PORT 3232
#define OTA_PARTITION_LABEL "app0"

enum class OTAUpdateState {
    Idle,
    Ready,
    Updating,
    Success,
    Error
};

class OTAUpdater {
public:
    OTAUpdater();
    ~OTAUpdater();

    bool begin(const char* hostname = OTA_HOSTNAME,
               const char* password = OTA_PASSWORD,
               uint16_t port = OTA_DEFAULT_PORT);

    void loop();

    OTAUpdateState getState() const { return state; }
    uint8_t getProgress() const { return updateProgress; }
    String getError() const { return errorMessage; }

    bool isUpdating() const { return state == OTAUpdateState::Updating; }

    void setOnStart(void (*callback)());
    void setOnEnd(void (*callback)());
    void setOnProgress(void (*callback)(unsigned int progress, unsigned int total));
    void setOnError(void (*callback)(ota_error_t error));

    void enableWebUpdate(bool enable) { webUpdateEnabled = enable; }
    bool isWebUpdateEnabled() const { return webUpdateEnabled; }

private:
    OTAUpdateState state;
    String errorMessage;
    uint8_t updateProgress;
    bool webUpdateEnabled;

    void (*onStartCallback)();
    void (*onEndCallback)();
    void (*onProgressCallback)(unsigned int progress, unsigned int total);
    void (*onErrorCallback)(ota_error_t error);

    void handleStart();
    void handleEnd();
    void handleProgress(unsigned int progress, unsigned int total);
    void handleError(ota_error_t error);
};

#endif
