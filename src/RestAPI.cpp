#include "RestAPI.h"
#include "MQTTManager.h"
#include "WebServer.h"
#include "WiFiManager.h"

RestAPI::RestAPI(AsyncWebServer* server)
    : webServer(server)
    , featureExtractor(nullptr)
    , faultDetector(nullptr)
    , trendAnalyzer(nullptr)
    , alertManager(nullptr)
    , edgeML(nullptr)
    , performanceMonitor(nullptr)
    , dataBuffer(nullptr)
    , runtimeConfig(nullptr)
    , dashboardController(nullptr)
    , mqttManager(nullptr)
    , wifiManager(nullptr)
    , latestSpectrum(nullptr)
    , spectrumLength(0)
    , calibrationCallback(nullptr)
    , authRequired(false)
    , requestCount(0)
    , errorCount(0)
{
    memset(&latestFeatures, 0, sizeof(latestFeatures));
    memset(&latestFault, 0, sizeof(latestFault));
}

RestAPI::~RestAPI() {
    if (latestSpectrum) {
        delete[] latestSpectrum;
    }
}

bool RestAPI::begin() {
    DEBUG_PRINTLN("Initializing REST API...");

    latestSpectrum = new float[FFT_OUTPUT_SIZE];
    spectrumLength = FFT_OUTPUT_SIZE;
    memset(latestSpectrum, 0, FFT_OUTPUT_SIZE * sizeof(float));

    setupRoutes();

    DEBUG_PRINTLN("REST API initialized");
    return true;
}

void RestAPI::setupRoutes() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", REST_CORS_ORIGIN);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");

    webServer->on("/api/v1/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });

    webServer->on("/api/v1/features", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetFeatures(request);
    });

    webServer->on("/api/v1/fault", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetFault(request);
    });

    webServer->on("/api/v1/spectrum", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetSpectrum(request);
    });

    webServer->on("/api/v1/trends", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetTrends(request);
    });

    webServer->on("/api/v1/alerts", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetAlerts(request);
    });

    webServer->on("/api/v1/alerts", HTTP_DELETE, [this](AsyncWebServerRequest* request) {
        handleDeleteAlerts(request);
    });

    webServer->on("/api/v1/alerts/ack", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handlePostAcknowledgeAlerts(request);
    });

    webServer->on("/api/v1/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetConfig(request);
    });

    webServer->on("/api/v1/system", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetSystemInfo(request);
    });

    webServer->on("/api/v1/health", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetHealth(request);
    });

    webServer->on("/api/v1/metrics", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetMetrics(request);
    });

    webServer->on("/api/v1/history", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetHistory(request);
    });

    webServer->on("/api/v1/baseline", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetBaseline(request);
    });

    webServer->on("/api/v1/model", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetMLModel(request);
    });

    webServer->on("/api/v1/export", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetExport(request);
    });

    webServer->on("/api/v1/calibrate", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handlePostCalibrate(request);
    });

    webServer->on("/api/v1/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handlePostReset(request);
    });

    webServer->on("/api/v1/baseline", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handlePostBaseline(request);
    });

    AsyncCallbackJsonWebHandler* configHandler = new AsyncCallbackJsonWebHandler("/api/v1/config",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostConfig(request, (uint8_t*)body.c_str(), body.length());
        });
    webServer->addHandler(configHandler);

    AsyncCallbackJsonWebHandler* thresholdHandler = new AsyncCallbackJsonWebHandler("/api/v1/thresholds",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostThresholds(request, (uint8_t*)body.c_str(), body.length());
        });
    webServer->addHandler(thresholdHandler);

    AsyncCallbackJsonWebHandler* mlTrainHandler = new AsyncCallbackJsonWebHandler("/api/v1/ml/train",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostMLTrain(request, (uint8_t*)body.c_str(), body.length());
        });
    webServer->addHandler(mlTrainHandler);

    AsyncCallbackJsonWebHandler* mlPredictHandler = new AsyncCallbackJsonWebHandler("/api/v1/ml/predict",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String body;
            serializeJson(json, body);
            handlePostMLPredict(request, (uint8_t*)body.c_str(), body.length());
        });
    webServer->addHandler(mlPredictHandler);

    webServer->on("/api/v1", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        request->send(200);
    });
}

bool RestAPI::authenticateRequest(AsyncWebServerRequest* request) {
    if (!authRequired) return true;

    if (request->hasHeader("X-API-Key")) {
        String key = request->header("X-API-Key");
        return key == apiKey;
    }

    if (request->hasHeader("Authorization")) {
        String auth = request->header("Authorization");
        if (auth.startsWith("Bearer ")) {
            return auth.substring(7) == apiKey;
        }
    }

    return false;
}

void RestAPI::sendJsonResponse(AsyncWebServerRequest* request, int code, const String& json) {
    request->send(code, "application/json", json);
    requestCount++;
}

void RestAPI::sendErrorResponse(AsyncWebServerRequest* request, int code, const String& message) {
    String json = "{\"error\":\"" + message + "\",\"code\":" + String(code) + "}";
    request->send(code, "application/json", json);
    errorCount++;
}

void RestAPI::updateFeatures(const FeatureVector& features) {
    latestFeatures = features;
}

void RestAPI::updateFault(const FaultResult& fault) {
    latestFault = fault;
}

void RestAPI::updateSpectrum(const float* spectrum, size_t length) {
    if (!latestSpectrum || !spectrum) {
        return;
    }
    if (length > spectrumLength) length = spectrumLength;
    memcpy(latestSpectrum, spectrum, length * sizeof(float));
}

void RestAPI::handleGetStatus(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateStatusJson());
}

void RestAPI::handleGetFeatures(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateFeaturesJson());
}

void RestAPI::handleGetFault(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateFaultJson());
}

void RestAPI::handleGetSpectrum(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateSpectrumJson());
}

void RestAPI::handleGetTrends(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateTrendsJson());
}

void RestAPI::handleGetAlerts(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateAlertsJson());
}

void RestAPI::handleGetConfig(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateConfigJson());
}

void RestAPI::handleGetSystemInfo(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateSystemInfoJson());
}

void RestAPI::handleGetHealth(AsyncWebServerRequest* request) {
    sendJsonResponse(request, 200, generateHealthJson());
}

void RestAPI::handleGetMetrics(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }
    sendJsonResponse(request, 200, generateMetricsJson());
}

void RestAPI::handlePostCalibrate(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!calibrationCallback) {
        sendErrorResponse(request, 500, "Calibration callback not configured");
        return;
    }

    calibrationCallback();
    sendJsonResponse(request, 202, "{\"success\":true,\"message\":\"Calibration scheduled\"}");
}

void RestAPI::handlePostReset(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Reset scheduled\"}");

    delay(100);
    ESP.restart();
}

void RestAPI::handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!runtimeConfig) {
        sendErrorResponse(request, 500, "Runtime config not available");
        return;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        sendErrorResponse(request, 400, "Invalid JSON payload");
        return;
    }

    bool restartRequired = false;
    String configError;
    if (!runtimeConfig->updateFromJson(doc.as<JsonVariantConst>(), restartRequired, configError)) {
        sendErrorResponse(request, 400, configError);
        return;
    }

    const RuntimeSettings& settings = runtimeConfig->getSettings();
    if (faultDetector) {
        faultDetector->setThresholds(settings.warningThreshold, settings.criticalThreshold);
    }
    if (dashboardController) {
        dashboardController->setBroadcastInterval(settings.webBroadcastIntervalMs);
    }

    if (!runtimeConfig->save()) {
        sendErrorResponse(request, 500, "Failed to persist configuration");
        return;
    }

    String json = "{\"success\":true,\"message\":\"Configuration updated\",";
    json += "\"restartRequired\":" + String(restartRequired ? "true" : "false");
    json += "}";
    sendJsonResponse(request, 200, json);
}

void RestAPI::handlePostAlert(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    sendJsonResponse(request, 200, "{\"success\":true}");
}

void RestAPI::handlePostThresholds(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!runtimeConfig || !faultDetector) {
        sendErrorResponse(request, 500, "Threshold configuration not available");
        return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        sendErrorResponse(request, 400, "Invalid JSON payload");
        return;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    JsonObjectConst thresholds = obj["thresholds"].as<JsonObjectConst>();
    if (!thresholds.isNull()) {
        obj = thresholds;
    }

    RuntimeSettings& settings = runtimeConfig->getMutableSettings();
    settings.warningThreshold = obj["warning"] | settings.warningThreshold;
    settings.criticalThreshold = obj["critical"] | settings.criticalThreshold;

    if (settings.warningThreshold <= 0.0f || settings.criticalThreshold <= 0.0f) {
        sendErrorResponse(request, 400, "Thresholds must be positive");
        return;
    }

    if (settings.warningThreshold >= settings.criticalThreshold) {
        sendErrorResponse(request, 400, "Critical threshold must be greater than warning threshold");
        return;
    }

    faultDetector->setThresholds(settings.warningThreshold, settings.criticalThreshold);
    if (!runtimeConfig->save()) {
        sendErrorResponse(request, 500, "Failed to persist thresholds");
        return;
    }

    String json = "{\"success\":true,\"message\":\"Thresholds updated\",";
    json += "\"warning\":" + String(settings.warningThreshold, 2) + ",";
    json += "\"critical\":" + String(settings.criticalThreshold, 2);
    json += "}";
    sendJsonResponse(request, 200, json);
}

void RestAPI::handlePostMLTrain(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (edgeML) {
        edgeML->startTraining();
        sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Training started\"}");
    } else {
        sendErrorResponse(request, 500, "ML engine not available");
    }
}

void RestAPI::handlePostMLPredict(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (edgeML) {
        MLPrediction prediction = edgeML->predict(latestFeatures);
        String json = "{\"predictedClass\":" + String((int)prediction.predictedClass);
        json += ",\"confidence\":" + String(prediction.confidence, 4);
        json += ",\"probabilities\":[";
        for (int i = 0; i < ML_OUTPUT_CLASSES; i++) {
            if (i > 0) json += ",";
            json += String(prediction.probabilities[i], 4);
        }
        json += "]}";
        sendJsonResponse(request, 200, json);
    } else {
        sendErrorResponse(request, 500, "ML engine not available");
    }
}

void RestAPI::handlePostAcknowledgeAlerts(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!alertManager) {
        sendErrorResponse(request, 500, "Alert manager not available");
        return;
    }

    if (request->hasParam("id")) {
        int id = request->getParam("id")->value().toInt();
        alertManager->acknowledgealert(id);
        sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Alert acknowledged\"}");
        return;
    }

    alertManager->acknowledgeAll();
    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"All alerts acknowledged\"}");
}

void RestAPI::handleDeleteAlert(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (request->hasParam("id")) {
        int id = request->getParam("id")->value().toInt();
        if (alertManager) {
            alertManager->clearAlert(id);
        }
        sendJsonResponse(request, 200, "{\"success\":true}");
    } else {
        sendErrorResponse(request, 400, "Missing alert ID");
    }
}

void RestAPI::handleDeleteAlerts(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (alertManager) {
        alertManager->clearAll();
    }
    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"All alerts cleared\"}");
}

void RestAPI::handleDeleteLogs(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Logs cleared\"}");
}

void RestAPI::handleGetHistory(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!dataBuffer) {
        sendErrorResponse(request, 500, "History buffer not available");
        return;
    }

    int limit = 100;
    if (request->hasParam("limit")) {
        limit = request->getParam("limit")->value().toInt();
    }
    if (limit < 1) limit = 1;
    if (limit > (int)dataBuffer->getMaxSize()) {
        limit = (int)dataBuffer->getMaxSize();
    }

    String json = dataBuffer->exportToJSON(dataBuffer->getSize() > (size_t)limit ? dataBuffer->getSize() - limit : 0, limit);
    sendJsonResponse(request, 200, json);
}

void RestAPI::handleGetBaseline(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!faultDetector) {
        sendErrorResponse(request, 500, "Fault detector not available");
        return;
    }

    const BaselineStats& baseline = faultDetector->getBaseline();
    String json = "{\"calibrated\":" + String(baseline.isCalibrated ? "true" : "false");
    json += ",\"sampleCount\":" + String(baseline.sampleCount);
    json += ",\"thresholds\":{";
    json += "\"warning\":" + String(faultDetector->getWarningThreshold(), 2) + ",";
    json += "\"critical\":" + String(faultDetector->getCriticalThreshold(), 2);
    json += "},\"baseline\":{";

    const char* keys[NUM_TOTAL_FEATURES] = {
        "rms",
        "peakToPeak",
        "kurtosis",
        "skewness",
        "crestFactor",
        "variance",
        "spectralCentroid",
        "spectralSpread",
        "bandPowerRatio",
        "dominantFreq"
    };

    json += "\"mean\":{";
    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        if (i > 0) json += ",";
        json += "\"" + String(keys[i]) + "\":" + String(baseline.mean[i], 4);
    }
    json += "},\"stdDev\":{";
    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        if (i > 0) json += ",";
        json += "\"" + String(keys[i]) + "\":" + String(baseline.stdDev[i], 4);
    }
    json += "},\"min\":{";
    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        if (i > 0) json += ",";
        json += "\"" + String(keys[i]) + "\":" + String(baseline.min[i], 4);
    }
    json += "},\"max\":{";
    for (int i = 0; i < NUM_TOTAL_FEATURES; i++) {
        if (i > 0) json += ",";
        json += "\"" + String(keys[i]) + "\":" + String(baseline.max[i], 4);
    }
    json += "}}";
    sendJsonResponse(request, 200, json);
}

void RestAPI::handlePostBaseline(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (faultDetector) {
        faultDetector->saveBaseline();
    }
    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Baseline saved\"}");
}

void RestAPI::handleGetMLModel(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    String json = "{\"model\":{";
    json += "\"type\":\"neural_network\",";
    json += "\"inputFeatures\":" + String(ML_INPUT_FEATURES) + ",";
    json += "\"hiddenNeurons\":" + String(ML_HIDDEN_NEURONS) + ",";
    json += "\"outputClasses\":" + String(ML_OUTPUT_CLASSES);
    if (edgeML) {
        json += ",\"accuracy\":" + String(edgeML->getAccuracy(), 2);
        json += ",\"trainingMode\":" + String(edgeML->isTraining() ? "true" : "false");
    }
    json += "}}";
    sendJsonResponse(request, 200, json);
}

void RestAPI::handlePostMLModel(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Model updated\"}");
}

void RestAPI::handleGetExport(AsyncWebServerRequest* request) {
    if (!authenticateRequest(request)) {
        sendErrorResponse(request, 401, "Unauthorized");
        return;
    }

    if (!dataBuffer) {
        sendErrorResponse(request, 500, "Export buffer not available");
        return;
    }

    String format = "json";
    if (request->hasParam("format")) {
        format = request->getParam("format")->value();
    }

    if (format == "csv") {
        request->send(200, "text/csv", dataBuffer->exportToCSV());
        requestCount++;
        return;
    }

    if (format == "json") {
        sendJsonResponse(request, 200, dataBuffer->exportToJSON());
        return;
    }

    sendErrorResponse(request, 400, "Unsupported export format");
}

String RestAPI::generateStatusJson() {
    String json = "{";
    json += "\"status\":\"running\",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"faultDetected\":" + String(latestFault.type != FAULT_NONE ? "true" : "false") + ",";
    json += "\"faultType\":" + String((int)latestFault.type) + ",";
    json += "\"severity\":" + String((int)latestFault.severity) + ",";
    json += "\"calibrated\":" + String(faultDetector && faultDetector->isCalibrated() ? "true" : "false") + ",";
    json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"provisioningActive\":" + String(wifiManager && wifiManager->isProvisioningActive() ? "true" : "false") + ",";
    json += "\"provisioningSsid\":\"" + String(wifiManager ? wifiManager->getProvisioningSSID() : "") + "\",";
    json += "\"networkIp\":\"" + String(wifiManager ? wifiManager->getIPAddress() : "") + "\",";
    json += "\"mqttConnected\":" + String(mqttManager && mqttManager->isConnected() ? "true" : "false") + ",";
    json += "\"alertsActive\":" + String(alertManager ? alertManager->getActiveAlertCount() : 0) + ",";
    json += "\"historyDepth\":" + String(dataBuffer ? dataBuffer->getSize() : 0) + ",";
    json += "\"apiVersion\":\"" + String(REST_API_VERSION) + "\"";
    json += "}";
    return json;
}

String RestAPI::generateFeaturesJson() {
    String json = "{\"features\":{";
    json += "\"rms\":" + String(latestFeatures.rms, 4) + ",";
    json += "\"peakToPeak\":" + String(latestFeatures.peakToPeak, 4) + ",";
    json += "\"kurtosis\":" + String(latestFeatures.kurtosis, 4) + ",";
    json += "\"skewness\":" + String(latestFeatures.skewness, 4) + ",";
    json += "\"crestFactor\":" + String(latestFeatures.crestFactor, 4) + ",";
    json += "\"variance\":" + String(latestFeatures.variance, 4) + ",";
    json += "\"spectralCentroid\":" + String(latestFeatures.spectralCentroid, 2) + ",";
    json += "\"spectralSpread\":" + String(latestFeatures.spectralSpread, 2) + ",";
    json += "\"bandPowerRatio\":" + String(latestFeatures.bandPowerRatio, 4) + ",";
    json += "\"dominantFrequency\":" + String(latestFeatures.dominantFrequency, 2);
    json += "},\"timestamp\":" + String(latestFeatures.timestamp) + "}";
    return json;
}

String RestAPI::generateFaultJson() {
    String json = "{\"fault\":{";
    json += "\"type\":" + String((int)latestFault.type) + ",";
    json += "\"severity\":" + String((int)latestFault.severity) + ",";
    json += "\"confidence\":" + String(latestFault.confidence, 4) + ",";
    json += "\"description\":\"" + latestFault.description + "\"";
    json += "},\"timestamp\":" + String(millis()) + "}";
    return json;
}

String RestAPI::generateSpectrumJson() {
    String json = "{\"spectrum\":{\"data\":[";
    for (size_t i = 0; i < spectrumLength; i++) {
        if (i > 0) json += ",";
        json += String(latestSpectrum[i], 4);
    }
    json += "],\"length\":" + String(spectrumLength);
    json += ",\"sampleRate\":" + String(SAMPLING_FREQUENCY_HZ);
    json += ",\"fftSize\":" + String(FFT_SIZE) + "}}";
    return json;
}

String RestAPI::generateTrendsJson() {
    String json = "{\"trends\":{";
    if (trendAnalyzer) {
        TrendAnalysis analysis = trendAnalyzer->getAnalysis();
        json += "\"rms\":{\"slope\":" + String(analysis.rms.slope, 6) + ",\"trend\":\"" + String((int)analysis.rms.direction) + "\"},";
        json += "\"kurtosis\":{\"slope\":" + String(analysis.kurtosis.slope, 6) + ",\"trend\":\"" + String((int)analysis.kurtosis.direction) + "\"},";
        json += "\"isDeterioration\":" + String(analysis.isDeterioration ? "true" : "false");
    } else {
        json += "\"available\":false";
    }
    json += "}}";
    return json;
}

String RestAPI::generateAlertsJson() {
    String json = "{\"alerts\":[";
    if (alertManager) {
        std::vector<Alert> alerts = alertManager->getAllAlerts();
        for (size_t i = 0; i < alerts.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"id\":" + String(i);
            json += ",\"type\":" + String((int)alerts[i].type);
            json += ",\"severity\":" + String((int)alerts[i].severity);
            json += ",\"message\":\"" + alerts[i].message + "\"";
            json += ",\"timestamp\":" + String(alerts[i].timestamp);
            json += ",\"acknowledged\":" + String(alerts[i].acknowledged ? "true" : "false") + "}";
        }
    }
    json += "],\"count\":" + String(alertManager ? alertManager->getActiveAlertCount() : 0) + "}";
    return json;
}

String RestAPI::generateConfigJson() {
    DynamicJsonDocument doc(2048);

    JsonObject config = doc.createNestedObject("config");
    if (runtimeConfig) {
        runtimeConfig->populateJson(config, false);
    }

    JsonObject sampling = config.createNestedObject("sampling");
    sampling["sampleRateHz"] = SAMPLING_FREQUENCY_HZ;
    sampling["windowSize"] = WINDOW_SIZE;
    sampling["fftSize"] = FFT_SIZE;
    sampling["overlapPercent"] = OVERLAP_PERCENTAGE;

    JsonObject provisioning = config.createNestedObject("provisioning");
    provisioning["active"] = wifiManager && wifiManager->isProvisioningActive();
    provisioning["ssid"] = wifiManager ? wifiManager->getProvisioningSSID() : "";
    provisioning["ip"] = wifiManager ? wifiManager->getProvisioningIP() : "";

    String output;
    serializeJson(doc, output);
    return output;
}

String RestAPI::generateSystemInfoJson() {
    String json = "{\"system\":{";
    if (runtimeConfig) {
        const RuntimeSettings& settings = runtimeConfig->getSettings();
        json += "\"deviceName\":\"" + String(settings.deviceName) + "\",";
        json += "\"deviceId\":\"" + String(settings.deviceId) + "\",";
    }
    json += "\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"chipModel\":\"" + String(ESP.getChipModel()) + "\",";
    json += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
    json += "\"cpuFreqMHz\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"heapSize\":" + String(ESP.getHeapSize()) + ",";
    json += "\"flashSize\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"sketchSize\":" + String(ESP.getSketchSize()) + ",";
    json += "\"freeSketchSpace\":" + String(ESP.getFreeSketchSpace()) + ",";
    json += "\"networkIp\":\"" + String(wifiManager ? wifiManager->getIPAddress() : "") + "\",";
    json += "\"provisioningSsid\":\"" + String(wifiManager ? wifiManager->getProvisioningSSID() : "") + "\",";
    json += "\"sdkVersion\":\"" + String(ESP.getSdkVersion()) + "\"";
    json += "}}";
    return json;
}

String RestAPI::generateHealthJson() {
    String json = "{\"health\":{";
    json += "\"status\":\"healthy\",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"provisioningActive\":" + String(wifiManager && wifiManager->isProvisioningActive() ? "true" : "false") + ",";
    json += "\"networkIp\":\"" + String(wifiManager ? wifiManager->getIPAddress() : "") + "\",";
    json += "\"mqttConnected\":" + String(mqttManager && mqttManager->isConnected() ? "true" : "false") + ",";
    json += "\"historyDepth\":" + String(dataBuffer ? dataBuffer->getSize() : 0) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}}";
    return json;
}

String RestAPI::generateMetricsJson() {
    String json = "{\"metrics\":{";
    json += "\"apiRequests\":" + String(requestCount) + ",";
    json += "\"apiErrors\":" + String(errorCount) + ",";
    if (performanceMonitor) {
        PerformanceMetrics metrics = performanceMonitor->getMetrics();
        json += "\"cpuUsage\":" + String(metrics.cpuUsage, 1) + ",";
        json += "\"loopTime\":" + String(metrics.avgLoopTime) + ",";
        json += "\"peakLoopTime\":" + String(metrics.maxLoopTime);
    } else {
        json += "\"cpuUsage\":0,";
        json += "\"loopTime\":0,";
        json += "\"peakLoopTime\":0";
    }
    json += "}}";
    return json;
}
