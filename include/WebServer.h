#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"
#include "PerformanceMonitor.h"

#define WEB_SERVER_PORT 80
#define WEB_SOCKET_PORT 81
#define WEB_MAX_CLIENTS 4

class MotorWebServer {
public:
    MotorWebServer();
    ~MotorWebServer();

    bool begin();
    void loop();

    void updateFeatures(const FeatureVector& features);
    void updateFault(const FaultResult& fault);
    void updatePerformance(const PerformanceMetrics& metrics);
    void updateSpectrum(const float* spectrum, size_t length);

    void broadcastData();
    void broadcastMessage(const String& message);
    void broadcastAlert(const String& alertJson);

    bool isClientConnected() const { return clientCount > 0; }
    uint8_t getClientCount() const { return clientCount; }
    AsyncWebServer* getServer() const { return server; }

    bool calibrationRequested;
    bool resetRequested;

private:
    AsyncWebServer* server;
    AsyncWebSocket* ws;

    FeatureVector latestFeatures;
    FaultResult latestFault;
    PerformanceMetrics latestMetrics;
    float* latestSpectrum;
    size_t spectrumLength;

    uint8_t clientCount;
    uint32_t lastBroadcast;
    uint32_t broadcastInterval;

    void setupRoutes();
    void handleRoot(AsyncWebServerRequest* request);
    void handleConfig(AsyncWebServerRequest* request);
    void handleAPI(AsyncWebServerRequest* request);
    void handleCalibrate(AsyncWebServerRequest* request);
    void handleReset(AsyncWebServerRequest* request);
    void handleExport(AsyncWebServerRequest* request);

    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg, uint8_t* data, size_t len);

    String generateDashboardHTML();
    String generateConfigHTML();
    String generateJSON();
    String generateSpectrumJSON();
};

#endif
