#include "OTAUpdater.h"

OTAUpdater::OTAUpdater()
    : state(OTAUpdateState::Idle)
    , errorMessage("")
    , updateProgress(0)
    , webUpdateEnabled(true)
    , onStartCallback(nullptr)
    , onEndCallback(nullptr)
    , onProgressCallback(nullptr)
    , onErrorCallback(nullptr)
{
}

OTAUpdater::~OTAUpdater() {
    ArduinoOTA.end();
}

bool OTAUpdater::begin(const char* hostname, const char* password, uint16_t port) {
    DEBUG_PRINTLN("Initializing OTA Updater...");

    ArduinoOTA.setPort(port);
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([this]() {
        this->handleStart();
    });

    ArduinoOTA.onEnd([this]() {
        this->handleEnd();
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        this->handleProgress(progress, total);
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        this->handleError(error);
    });

    ArduinoOTA.begin();

    state = OTAUpdateState::Ready;

    DEBUG_PRINT("OTA Update ready. Hostname: ");
    DEBUG_PRINT(hostname);
    DEBUG_PRINT(", Port: ");
    DEBUG_PRINTLN(port);

    return true;
}

void OTAUpdater::loop() {
    if (state == OTAUpdateState::Ready || state == OTAUpdateState::Updating) {
        ArduinoOTA.handle();
    }
}

void OTAUpdater::setOnStart(void (*callback)()) {
    onStartCallback = callback;
}

void OTAUpdater::setOnEnd(void (*callback)()) {
    onEndCallback = callback;
}

void OTAUpdater::setOnProgress(void (*callback)(unsigned int progress, unsigned int total)) {
    onProgressCallback = callback;
}

void OTAUpdater::setOnError(void (*callback)(ota_error_t error)) {
    onErrorCallback = callback;
}

void OTAUpdater::handleStart() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
    } else {
        type = "filesystem";
    }

    DEBUG_PRINT("OTA Update starting: ");
    DEBUG_PRINTLN(type);

    state = OTAUpdateState::Updating;
    updateProgress = 0;
    errorMessage = "";

    if (onStartCallback != nullptr) {
        onStartCallback();
    }
}

void OTAUpdater::handleEnd() {
    DEBUG_PRINTLN("\nOTA Update complete!");

    state = OTAUpdateState::Success;
    updateProgress = 100;

    if (onEndCallback != nullptr) {
        onEndCallback();
    }
}

void OTAUpdater::handleProgress(unsigned int progress, unsigned int total) {
    updateProgress = (progress / (total / 100));

    DEBUG_PRINT("Progress: ");
    DEBUG_PRINT(updateProgress);
    DEBUG_PRINTLN("%");

    if (onProgressCallback != nullptr) {
        onProgressCallback(progress, total);
    }
}

void OTAUpdater::handleError(ota_error_t error) {
    state = OTAUpdateState::Error;

    switch (error) {
        case OTA_AUTH_ERROR:
            errorMessage = "Auth Failed";
            break;
        case OTA_BEGIN_ERROR:
            errorMessage = "Begin Failed";
            break;
        case OTA_CONNECT_ERROR:
            errorMessage = "Connect Failed";
            break;
        case OTA_RECEIVE_ERROR:
            errorMessage = "Receive Failed";
            break;
        case OTA_END_ERROR:
            errorMessage = "End Failed";
            break;
        default:
            errorMessage = "Unknown Error";
            break;
    }

    DEBUG_PRINT("OTA Error: ");
    DEBUG_PRINTLN(errorMessage);

    if (onErrorCallback != nullptr) {
        onErrorCallback(error);
    }
}
