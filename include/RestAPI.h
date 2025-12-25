#ifndef REST_API_H
#define REST_API_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"
#include "TrendAnalyzer.h"
#include "AlertManager.h"
#include "EdgeML.h"
#include "PerformanceMonitor.h"

#define REST_API_VERSION "1.0.0"
#define REST_MAX_BODY_SIZE 8192
#define REST_CORS_ORIGIN "*"

struct APIResponse {
    int statusCode;
    String contentType;
    String body;
};

class RestAPI {
public:
    RestAPI(AsyncWebServer* server);
    ~RestAPI();

    bool begin();
    void setFeatureExtractor(FeatureExtractor* fe) { featureExtractor = fe; }
    void setFaultDetector(FaultDetector* fd) { faultDetector = fd; }
    void setTrendAnalyzer(TrendAnalyzer* ta) { trendAnalyzer = ta; }
    void setAlertManager(AlertManager* am) { alertManager = am; }
    void setEdgeML(EdgeML* ml) { edgeML = ml; }
    void setPerformanceMonitor(PerformanceMonitor* pm) { performanceMonitor = pm; }
    void setCalibrationCallback(void (*callback)()) { calibrationCallback = callback; }

    void updateFeatures(const FeatureVector& features);
    void updateFault(const FaultResult& fault);
    void updateSpectrum(const float* spectrum, size_t length);

    void setApiKey(const String& key) { apiKey = key; }
    void setAuthRequired(bool required) { authRequired = required; }

    uint32_t getRequestCount() const { return requestCount; }
    uint32_t getErrorCount() const { return errorCount; }

private:
    AsyncWebServer* webServer;

    FeatureExtractor* featureExtractor;
    FaultDetector* faultDetector;
    TrendAnalyzer* trendAnalyzer;
    AlertManager* alertManager;
    EdgeML* edgeML;
    PerformanceMonitor* performanceMonitor;

    FeatureVector latestFeatures;
    FaultResult latestFault;
    float* latestSpectrum;
    size_t spectrumLength;

    String apiKey;
    bool authRequired;

    uint32_t requestCount;
    uint32_t errorCount;

    void (*calibrationCallback)();

    void setupRoutes();
    bool authenticateRequest(AsyncWebServerRequest* request);
    void sendJsonResponse(AsyncWebServerRequest* request, int code, const String& json);
    void sendErrorResponse(AsyncWebServerRequest* request, int code, const String& message);

    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetFeatures(AsyncWebServerRequest* request);
    void handleGetFault(AsyncWebServerRequest* request);
    void handleGetSpectrum(AsyncWebServerRequest* request);
    void handleGetTrends(AsyncWebServerRequest* request);
    void handleGetAlerts(AsyncWebServerRequest* request);
    void handleGetConfig(AsyncWebServerRequest* request);
    void handleGetSystemInfo(AsyncWebServerRequest* request);
    void handleGetHealth(AsyncWebServerRequest* request);
    void handleGetMetrics(AsyncWebServerRequest* request);

    void handlePostCalibrate(AsyncWebServerRequest* request);
    void handlePostReset(AsyncWebServerRequest* request);
    void handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handlePostAlert(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handlePostThresholds(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handlePostMLTrain(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handlePostMLPredict(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    void handleDeleteAlert(AsyncWebServerRequest* request);
    void handleDeleteAlerts(AsyncWebServerRequest* request);
    void handleDeleteLogs(AsyncWebServerRequest* request);

    void handleGetHistory(AsyncWebServerRequest* request);
    void handleGetBaseline(AsyncWebServerRequest* request);
    void handlePostBaseline(AsyncWebServerRequest* request);
    void handleGetMLModel(AsyncWebServerRequest* request);
    void handlePostMLModel(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleGetExport(AsyncWebServerRequest* request);

    String generateStatusJson();
    String generateFeaturesJson();
    String generateFaultJson();
    String generateSpectrumJson();
    String generateTrendsJson();
    String generateAlertsJson();
    String generateConfigJson();
    String generateSystemInfoJson();
    String generateHealthJson();
    String generateMetricsJson();
};

#endif
