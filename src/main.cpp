#include <Arduino.h>
#include "Config.h"
#include "MPU6050Driver.h"
#include "SignalProcessor.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"
#include "DataLogger.h"
#include "WiFiManager.h"
#include "WebServer.h"
#include "MQTTManager.h"
#include "OTAUpdater.h"
#include "DataBuffer.h"
#include "PerformanceMonitor.h"
#include "TrendAnalyzer.h"
#include "SelfDiagnostic.h"
#include "AlertManager.h"
#include "MultiAxisAnalyzer.h"
#include "EdgeML.h"
#include "MaintenanceScheduler.h"
#include "EnergyMonitor.h"
#include "StorageManager.h"
#include "SystemHardening.h"
#include "RestAPI.h"
#include "ModbusServer.h"
#include "CloudConnector.h"
#include "ExtendedFeatureExtractor.h"
#include "EnsembleClassifier.h"
#include "TFLiteEngine.h"
#include "ModelManager.h"
#include "SecurityManager.h"
#include "ContinuousLearningManager.h"
#include "OnlineLearner.h"
#include "DriftDetector.h"
#include "SelfCalibratingModel.h"
#include "RuntimeConfig.h"
#include <ArduinoJson.h>

MPU6050Driver sensor;
SignalProcessor signalProc;
FeatureExtractor featureExt;
FaultDetector faultDetect;
DataLogger logger;
WiFiManager wifiMgr;
MotorWebServer webServer;
MQTTManager mqttMgr;
OTAUpdater otaUpdater;
DataBuffer dataBuffer(500);
PerformanceMonitor perfMon;
TrendAnalyzer trendAnalyzer;
SelfDiagnostic selfDiag;
AlertManager alertMgr;
MultiAxisAnalyzer multiAxis;
EdgeML edgeML;
MaintenanceScheduler maintScheduler;
EnergyMonitor energyMon;
StorageManager storageMgr;
SystemHardening sysHardening;
RestAPI* restApi = nullptr;
ModbusServer modbusServer;
GenericMQTTConnector cloudConnector;
ExtendedFeatureExtractor extFeatureExt;
EnsembleClassifier ensembleClassifier;
TFLiteEngine tfliteEngine;
ModelManager modelManager;
SecurityManager securityMgr;
OnlineLearner onlineLearner;
DriftDetector driftDetector;
SelfCalibratingModel selfCalibModel;
ContinuousLearningManager clManager;
RuntimeConfigManager runtimeConfig;

enum SystemState {
    STATE_INIT,
    STATE_CALIBRATING,
    STATE_MONITORING,
    STATE_ERROR
};

SystemState currentState = STATE_INIT;
uint32_t samplesCollected = 0;
uint32_t lastSampleTime = 0;

uint32_t totalSamples = 0;
uint32_t faultsDetected = 0;
uint32_t loopCounter = 0;
volatile bool calibrationRequestPending = false;

static const RuntimeSettings& getRuntimeSettings() {
    return runtimeConfig.getSettings();
}

static bool isNetworkEnabled() {
    return getRuntimeSettings().wifiEnabled;
}

static bool isMqttEnabled() {
    const RuntimeSettings& settings = getRuntimeSettings();
    return settings.wifiEnabled && settings.mqttEnabled;
}

static bool isOtaEnabled() {
    const RuntimeSettings& settings = getRuntimeSettings();
    return settings.wifiEnabled && settings.otaEnabled;
}

void setup();
void loop();
void printWelcomeBanner();
void initializeSystem();
void performCalibration();
void monitorVibration();
void processVibrationData();
void handleError(const String& errorMsg);
void printSystemStatus();
void blinkLED(uint8_t times, uint16_t delayMs = 200);
void handleCloudMessage(const char* topic, const uint8_t* payload, size_t length);

void setup() {

    Serial.begin(DEBUG_BAUD_RATE);
    delay(1000);

    printWelcomeBanner();

    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);

    initializeSystem();
}

void loop() {
    loopCounter++;

    switch (currentState) {
        case STATE_INIT:
            if (faultDetect.isCalibrated()) {
                DEBUG_PRINTLN("Baseline found in storage. Skipping calibration.");
                currentState = STATE_MONITORING;
            } else {
                currentState = STATE_CALIBRATING;
                performCalibration();
            }
            break;

        case STATE_CALIBRATING:

            break;

        case STATE_MONITORING:

            if (webServer.resetRequested) {
                webServer.resetRequested = false;
                ESP.restart();
            }
            if (webServer.calibrationRequested) {
                webServer.calibrationRequested = false;
                calibrationRequestPending = true;
            }
            if (calibrationRequestPending) {
                calibrationRequestPending = false;
                currentState = STATE_CALIBRATING;
                performCalibration();
                break;
            }

            perfMon.startLoop();

            monitorVibration();

            if (isNetworkEnabled()) {
                wifiMgr.loop();
                webServer.loop();
                mqttMgr.loop();
                cloudConnector.loop();
                if (isOtaEnabled()) {
                    otaUpdater.loop();
                }
            }

            sysHardening.loop();
            modbusServer.loop();
            modelManager.loop();
            clManager.update();

            perfMon.endLoop();

            if (loopCounter % 1000 == 0) {
                printSystemStatus();
            }

            if (loopCounter % 10000 == 0 && selfDiag.shouldRunDiagnostics()) {
                DiagnosticResult diagResult = selfDiag.runDiagnostics(&sensor);
                if (diagResult.overall != DIAG_OK) {
                    String summary;
                    if (!diagResult.sensorOK) summary += "Sensor: " + diagResult.sensorMessage + "; ";
                    if (!diagResult.memoryOK) summary += "Memory: " + diagResult.memoryMessage + "; ";
                    if (!diagResult.wifiOK) summary += "WiFi: " + diagResult.wifiMessage + "; ";
                    if (!diagResult.storageOK) summary += "Storage: " + diagResult.storageMessage + "; ";
                    if (!diagResult.temperatureOK) summary += "Temp: " + diagResult.temperatureMessage + "; ";
                    if (summary.length() == 0) summary = "Diagnostics reported non-OK status";
                    alertMgr.raiseAlert(ALERT_SYSTEM, SEVERITY_WARNING,
                                       "System diagnostic warning", summary);
                }
            }

            if (dataBuffer.shouldAutoExport()) {
                dataBuffer.saveToFile("/data_export.csv");
                dataBuffer.updateLastExportTime();
                DEBUG_PRINTLN("Data buffer auto-exported");
            }
            break;

        case STATE_ERROR:

            blinkLED(3, 100);
            delay(1000);
            break;
    }

    yield();
}

void initializeSystem() {
    DEBUG_PRINTLN("\n=== Initializing System ===\n");

    DEBUG_PRINTLN("0. Initializing Storage Manager...");
    if (storageMgr.begin()) {
        DEBUG_PRINTLN("   [OK] Storage Manager\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Storage Manager failed\n");
    }

    DEBUG_PRINTLN("0b. Loading Runtime Configuration...");
    runtimeConfig.begin();
    DEBUG_PRINTF("   [OK] Device ID: %s\n\n", getRuntimeSettings().deviceId);

    DEBUG_PRINTLN("1. Initializing MPU6050 sensor...");
    if (!sensor.begin()) {
        handleError("MPU6050 initialization failed: " + sensor.getLastError());
        return;
    }
    DEBUG_PRINTLN("   [OK] MPU6050\n");
    blinkLED(1);

    DEBUG_PRINTLN("2. Initializing Signal Processor...");
    if (!signalProc.begin()) {
        handleError("Signal processor initialization failed");
        return;
    }
    DEBUG_PRINTLN("   [OK] Signal Processor\n");
    blinkLED(1);

    DEBUG_PRINTLN("3. Initializing Fault Detector...");
    if (!faultDetect.begin()) {
        handleError("Fault detector initialization failed");
        return;
    }
    faultDetect.setThresholds(getRuntimeSettings().warningThreshold,
                              getRuntimeSettings().criticalThreshold);
    DEBUG_PRINTLN("   [OK] Fault Detector\n");
    blinkLED(1);

    DEBUG_PRINTLN("4. Initializing Data Logger...");
    if (!logger.begin()) {
        handleError("Data logger initialization failed");
        return;
    }
    DEBUG_PRINTLN("   [OK] Data Logger\n");
    blinkLED(1);

    if (isNetworkEnabled()) {
        const RuntimeSettings& settings = getRuntimeSettings();
        DEBUG_PRINTLN("5. Initializing WiFi...");
        if (wifiMgr.begin(settings.wifiSsid, settings.wifiPassword)) {
            if (wifiMgr.isWiFiConnected()) {
                DEBUG_PRINTLN("   [OK] WiFi\n");
            } else if (wifiMgr.isProvisioningActive()) {
                DEBUG_PRINTF("   [OK] Provisioning AP active: %s (%s)\n\n",
                             wifiMgr.getProvisioningSSID().c_str(),
                             wifiMgr.getProvisioningIP().c_str());
            }
            blinkLED(2);

            DEBUG_PRINTLN("6. Initializing Web Server...");
            if (webServer.begin()) {
                webServer.setBroadcastInterval(settings.webBroadcastIntervalMs);
                DEBUG_PRINTLN("   [OK] Web Server\n");
                blinkLED(1);
            } else {
                DEBUG_PRINTLN("   ⚠ Web Server failed\n");
            }

            if (wifiMgr.isWiFiConnected() && settings.mqttEnabled) {
                DEBUG_PRINTLN("7. Initializing MQTT...");
                if (mqttMgr.begin(settings.mqttBroker,
                                  settings.mqttPort,
                                  settings.deviceId,
                                  settings.mqttUser,
                                  settings.mqttPassword)) {
                    DEBUG_PRINTLN("   [OK] MQTT\n");
                    blinkLED(1);
                } else {
                    DEBUG_PRINTLN("   ⚠ MQTT failed\n");
                }

                DEBUG_PRINTLN("7b. Initializing Cloud Connector...");
                CloudConfig cloudConfig;
                memset(&cloudConfig, 0, sizeof(cloudConfig));
                cloudConfig.provider = CLOUD_GENERIC_MQTT;
                strncpy(cloudConfig.endpoint, settings.mqttBroker, sizeof(cloudConfig.endpoint) - 1);
                cloudConfig.port = settings.mqttPort;

                String cloudClientId = String(settings.deviceId) + "-cloud";
                strncpy(cloudConfig.deviceId, cloudClientId.c_str(), sizeof(cloudConfig.deviceId) - 1);
                strncpy(cloudConfig.username, settings.mqttUser, sizeof(cloudConfig.username) - 1);
                strncpy(cloudConfig.password, settings.mqttPassword, sizeof(cloudConfig.password) - 1);
                cloudConfig.useTLS = false;

                cloudConnector.setWillMessage(
                    (String("motor-vibration-monitor/") + cloudClientId + "/status").c_str(),
                    "offline"
                );

                if (cloudConnector.begin(cloudConfig)) {
                    cloudConnector.setMessageCallback(handleCloudMessage);
                    cloudConnector.subscribeModelUpdates();
                    cloudConnector.subscribeLabelResponses();
                    DEBUG_PRINTLN("   [OK] Cloud Connector\n");
                } else {
                    DEBUG_PRINTLN("   ⚠ Cloud Connector failed\n");
                }
            } else if (settings.mqttEnabled) {
                DEBUG_PRINTLN("7. MQTT deferred until WiFi station connection is available\n");
            }

            if (wifiMgr.isWiFiConnected() && settings.otaEnabled) {
                DEBUG_PRINTLN("8. Initializing OTA Updater...");
                if (otaUpdater.begin(settings.deviceId, settings.otaPassword)) {
                    DEBUG_PRINTLN("   [OK] OTA Updater\n");
                    blinkLED(1);
                } else {
                    DEBUG_PRINTLN("   ⚠ OTA Updater failed\n");
                }
            } else if (settings.otaEnabled) {
                DEBUG_PRINTLN("8. OTA deferred until WiFi station connection is available\n");
            } else {
                DEBUG_PRINTLN("8. OTA Updater disabled in runtime settings\n");
            }
        } else {
            DEBUG_PRINTLN("   ⚠ WiFi initialization failed (continuing without WiFi)\n");
        }
    }

    DEBUG_PRINTLN("9. Initializing Performance Monitor...");
    perfMon.begin();
    DEBUG_PRINTLN("   [OK] Performance Monitor\n");

    DEBUG_PRINTLN("10. Initializing Data Buffer...");
    dataBuffer.setAutoExport(true);
    dataBuffer.setAutoExportInterval(300000);
    dataBuffer.loadFromFile("/data_export.csv");
    DEBUG_PRINTLN("   [OK] Data Buffer\n");

    DEBUG_PRINTLN("12. Initializing Trend Analyzer...");
    trendAnalyzer.begin();
    DEBUG_PRINTLN("   [OK] Trend Analyzer\n");

    DEBUG_PRINTLN("13. Initializing Alert Manager...");
    alertMgr.begin();
    DEBUG_PRINTLN("   [OK] Alert Manager\n");

    DEBUG_PRINTLN("14. Initializing Multi-Axis Analyzer...");
    multiAxis.begin();
    DEBUG_PRINTLN("   [OK] Multi-Axis Analyzer\n");

    DEBUG_PRINTLN("15. Initializing Edge ML...");
    if (edgeML.begin()) {
        DEBUG_PRINTLN("   [OK] Edge ML\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Edge ML failed\n");
    }

    DEBUG_PRINTLN("16. Initializing Online Learner...");
    if (onlineLearner.begin()) {
        DEBUG_PRINTLN("   [OK] Online Learner\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Online Learner failed\n");
    }

    DEBUG_PRINTLN("17. Initializing Drift Detector...");
    if (driftDetector.begin()) {
        DEBUG_PRINTLN("   [OK] Drift Detector\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Drift Detector failed\n");
    }

    DEBUG_PRINTLN("18. Initializing Self-Calibrating Model...");
    if (selfCalibModel.begin()) {
        DEBUG_PRINTLN("   [OK] Self-Calibrating Model\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Self-Calibrating Model failed\n");
    }

    DEBUG_PRINTLN("19. Initializing Maintenance Scheduler...");
    if (maintScheduler.begin()) {
        DEBUG_PRINTLN("   [OK] Maintenance Scheduler\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Maintenance Scheduler failed\n");
    }

    DEBUG_PRINTLN("20. Initializing Energy Monitor...");
    if (energyMon.begin()) {
        energyMon.setEnergyRate(0.12);
        DEBUG_PRINTLN("   [OK] Energy Monitor\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Energy Monitor failed\n");
    }

    DEBUG_PRINTLN("21. Initializing Self-Diagnostic...");
    selfDiag.begin();
    DEBUG_PRINTLN("   [OK] Self-Diagnostic\n");

    DEBUG_PRINTLN("22. Initializing System Hardening...");
    if (sysHardening.begin()) {
        DEBUG_PRINTLN("   [OK] System Hardening\n");
    } else {
        DEBUG_PRINTLN("   ⚠ System Hardening failed\n");
    }

    DEBUG_PRINTLN("23. Initializing Security Manager...");
    if (securityMgr.begin()) {
        DEBUG_PRINTLN("   [OK] Security Manager\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Security Manager failed\n");
    }

    DEBUG_PRINTLN("24. Initializing Extended Feature Extractor...");
    if (extFeatureExt.begin()) {
        DEBUG_PRINTLN("   [OK] Extended Feature Extractor\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Extended Feature Extractor failed\n");
    }

    DEBUG_PRINTLN("25. Initializing Ensemble Classifier...");
    if (ensembleClassifier.begin()) {
        ensembleClassifier.setCustomNN(&edgeML);
        ensembleClassifier.setTFLiteEngine(&tfliteEngine);
        ensembleClassifier.setOnlineLearner(&onlineLearner);
        ensembleClassifier.setMethod(ENSEMBLE_WEIGHTED);
        ensembleClassifier.addModel("CustomNN", BACKEND_CUSTOM_NN, 0.8f);
        ensembleClassifier.addModel("OnlineLearner", BACKEND_ONLINE_LEARNER, 0.6f);
        DEBUG_PRINTLN("   [OK] Ensemble Classifier\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Ensemble Classifier failed\n");
    }

#ifdef USE_TFLITE
    DEBUG_PRINTLN("26. Initializing TFLite Engine...");
    if (tfliteEngine.begin()) {
        DEBUG_PRINTLN("   [OK] TFLite Engine\n");
    } else {
        DEBUG_PRINTLN("   ⚠ TFLite Engine failed\n");
    }
#endif

    DEBUG_PRINTLN("27. Initializing Model Manager...");
    if (modelManager.begin()) {
#ifdef USE_TFLITE
        modelManager.setTFLiteEngine(&tfliteEngine);
#endif
        modelManager.setEdgeML(&edgeML);
        DEBUG_PRINTLN("   [OK] Model Manager\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Model Manager failed\n");
    }

    DEBUG_PRINTLN("28. Initializing Continuous Learning Manager...");
    clManager.setOnlineLearner(&onlineLearner);
    clManager.setDriftDetector(&driftDetector);
    clManager.setCalibrationModel(&selfCalibModel);
    clManager.setDataLogger(&logger);
    clManager.setCloudConnector(&cloudConnector);
    clManager.setModelManager(&modelManager);
    if (clManager.begin()) {
        clManager.setActive(true);
        DEBUG_PRINTLN("   [OK] Continuous Learning Manager\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Continuous Learning Manager failed\n");
    }

    if (isNetworkEnabled() && webServer.getServer()) {
        DEBUG_PRINTLN("29. Initializing REST API...");
            restApi = new RestAPI(webServer.getServer());
            if (restApi->begin()) {
                restApi->setFeatureExtractor(&featureExt);
                restApi->setFaultDetector(&faultDetect);
                restApi->setTrendAnalyzer(&trendAnalyzer);
                restApi->setAlertManager(&alertMgr);
                restApi->setEdgeML(&edgeML);
                restApi->setPerformanceMonitor(&perfMon);
                restApi->setDataBuffer(&dataBuffer);
                restApi->setRuntimeConfig(&runtimeConfig);
                restApi->setWebServerController(&webServer);
                restApi->setMQTTManager(&mqttMgr);
                restApi->setWiFiManager(&wifiMgr);
                restApi->setCalibrationCallback([]() { calibrationRequestPending = true; });
                DEBUG_PRINTLN("   [OK] REST API\n");
            } else {
            DEBUG_PRINTLN("   ⚠ REST API failed\n");
        }
    }

    DEBUG_PRINTLN("30. Initializing Modbus Server...");
    if (modbusServer.begin(isNetworkEnabled(), false)) {
        modbusServer.setCalibrationCallback([]() {
            calibrationRequestPending = true;
        });
        modbusServer.setResetCallback([]() {
            ESP.restart();
        });
        DEBUG_PRINTLN("   [OK] Modbus Server\n");
    } else {
        DEBUG_PRINTLN("   ⚠ Modbus Server failed\n");
    }

    DEBUG_PRINTLN("\n=== System Initialization Complete ===");
    DEBUG_PRINTLN("Core monitoring pipeline initialized\n");
    blinkLED(3, 100);
}

void performCalibration() {
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN("  CALIBRATION MODE");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("Please ensure:");
    DEBUG_PRINTLN("  1. Motor is running normally");
    DEBUG_PRINTLN("  2. No abnormal vibrations present");
    DEBUG_PRINTLN("  3. Sensor is securely mounted");
    DEBUG_PRINTLN("");
    DEBUG_PRINTF("Collecting %d baseline samples...\n", CALIBRATION_SAMPLES);
    DEBUG_PRINTLN("========================================\n");

    faultDetect.startCalibration(CALIBRATION_SAMPLES);

    uint32_t samplesCollected = 0;
    uint32_t lastProgressUpdate = 0;

    while (samplesCollected < CALIBRATION_SAMPLES) {

        AccelData accelData;
        if (!sensor.readAcceleration(accelData)) {
            DEBUG_PRINTLN("Failed to read sensor during calibration");
            delay(SAMPLING_PERIOD_MS);
            continue;
        }

        float magnitude = sqrt(accelData.x * accelData.x +
                             accelData.y * accelData.y +
                             accelData.z * accelData.z);

        if (signalProc.addSample(magnitude, 0)) {

            signalProc.performFFT(0);

            FeatureVector features;
            float spectrum[FFT_OUTPUT_SIZE];
            signalProc.getMagnitudeSpectrum(spectrum, FFT_OUTPUT_SIZE);

            featureExt.extractAllFeatures(
                signalProc.getBufferData(0),
                WINDOW_SIZE,
                spectrum,
                FFT_OUTPUT_SIZE,
                &signalProc,
                features
            );

            if (faultDetect.addCalibrationSample(features)) {

                DEBUG_PRINTLN("\nCalibration complete!");
                DEBUG_PRINTLN("========================================\n");
                currentState = STATE_MONITORING;
                blinkLED(5, 100);
                return;
            }

            samplesCollected++;
            signalProc.reset();

            if (millis() - lastProgressUpdate > 1000) {
                uint8_t progress = (samplesCollected * 100) / CALIBRATION_SAMPLES;
                DEBUG_PRINTF("Progress: %d%% (%d/%d)\n",
                           progress, samplesCollected, CALIBRATION_SAMPLES);
                lastProgressUpdate = millis();
                digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN));
            }
        }

        delay(SAMPLING_PERIOD_MS);
    }
}

void monitorVibration() {
    uint32_t currentTime = millis();

    if (currentTime - lastSampleTime < SAMPLING_PERIOD_MS) {
        return;
    }

    lastSampleTime = currentTime;

    AccelData accelData;
    ProfileTimer sensorTimer;
    sensorTimer.start();
    bool readOk = sensor.readAcceleration(accelData);
    perfMon.recordSensorRead(sensorTimer.stop());
    if (!readOk) {
        perfMon.recordMissedSample();
        DEBUG_PRINTLN("Failed to read sensor");
        return;
    }
    perfMon.incrementSamples();

    multiAxis.addSample(accelData.x, accelData.y, accelData.z);

    energyMon.updatePowerMetrics(VOLTAGE_NOMINAL, 5.0f);

    float magnitude = sqrt(accelData.x * accelData.x +
                         accelData.y * accelData.y +
                         accelData.z * accelData.z);

    if (signalProc.addSample(magnitude, 0)) {

        processVibrationData();
        signalProc.advanceWindow(0);
    }

    totalSamples++;

    if (totalSamples % 100 == 0) {
        digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN));
    }
}

void processVibrationData() {
    ProfileTimer fftTimer;
    fftTimer.start();
    if (!signalProc.performFFT(0)) {
        perfMon.recordFFT(fftTimer.stop());
        DEBUG_PRINTLN("FFT failed");
        return;
    }
    perfMon.recordFFT(fftTimer.stop());
    perfMon.incrementFFTs();

    float spectrum[FFT_OUTPUT_SIZE];
    if (!signalProc.getMagnitudeSpectrum(spectrum, FFT_OUTPUT_SIZE)) {
        DEBUG_PRINTLN("Failed to get spectrum");
        return;
    }

    FeatureVector features;
    ProfileTimer featureTimer;
    featureTimer.start();
    if (!featureExt.extractAllFeatures(
            signalProc.getBufferData(0),
            WINDOW_SIZE,
            spectrum,
            FFT_OUTPUT_SIZE,
            &signalProc,
            features)) {
        perfMon.recordFeatureExtract(featureTimer.stop());
        DEBUG_PRINTLN("Feature extraction failed");
        return;
    }
    perfMon.recordFeatureExtract(featureTimer.stop());

    FaultResult faultResult;
    ProfileTimer detectTimer;
    detectTimer.start();
    bool faultDetected = faultDetect.detectFault(features, faultResult);
    perfMon.recordFaultDetect(detectTimer.stop());
    perfMon.incrementDetections();

    float temperature = sensor.getTemperature();

    logger.log(features, faultResult, temperature);

    dataBuffer.addRecord(features, faultDetected ? &faultResult : nullptr);

    float clFeatures[10];
    features.toArray(clFeatures);
    FaultResult clPrediction = faultResult;
    EnsemblePrediction ensemblePred = ensembleClassifier.predict(features);
    if (ensemblePred.valid) {
        clPrediction.type = static_cast<FaultType>(ensemblePred.predictedClass);
        clPrediction.confidence = ensemblePred.confidence;
    }
    clManager.processSample(clFeatures, 10, clPrediction);

    trendAnalyzer.addSample(features, faultResult.anomalyScore);
    TrendAnalysis trends = trendAnalyzer.getAnalysis();

    maintScheduler.update(features, faultResult, trends);

    MLPrediction mlPrediction = edgeML.predict(features);
    if (mlPrediction.confidence > 0.7f && mlPrediction.predictedClass != ML_NORMAL) {
        DEBUG_PRINT("ML Prediction: Class ");
        DEBUG_PRINT(mlPrediction.predictedClass);
        DEBUG_PRINT(" Confidence: ");
        DEBUG_PRINTLN(mlPrediction.confidence);
    }

    float vibrationPowerLoss = energyMon.estimateVibrationPowerLoss(features);

    if (isNetworkEnabled()) {
        webServer.updateFeatures(features);
        webServer.updateFault(faultResult);
        webServer.updateSpectrum(spectrum, FFT_OUTPUT_SIZE);

        PerformanceMetrics metrics = perfMon.getMetrics();
        webServer.updatePerformance(metrics);

        if (webServer.isClientConnected()) {
            webServer.broadcastData();
        }

        if (restApi) {
            restApi->updateFeatures(features);
            restApi->updateFault(faultResult);
            restApi->updateSpectrum(spectrum, FFT_OUTPUT_SIZE);
        }
    }

    modbusServer.updateFeatures(features);
    modbusServer.updateFault(faultResult);
    modbusServer.updateSystemStatus(
        (uint8_t)perfMon.getMetrics().cpuUsage,
        ESP.getFreeHeap(),
        wifiMgr.getWiFiRSSI()
    );

    if (faultDetected) {
        faultsDetected++;
        logger.logAlert(faultResult);

        alertMgr.raiseAlert(ALERT_FAULT, faultResult.severity,
                           faultResult.description,
                           "Anomaly score: " + String(faultResult.anomalyScore, 2));

        if (isMqttEnabled() && mqttMgr.isConnected()) {
            mqttMgr.publishFault(faultResult);
            mqttMgr.publishAlert(faultResult.description.c_str(), faultResult.severity);
        }

        blinkLED(10, 50);
    }

    if (trendAnalyzer.isDeterioration()) {
        alertMgr.raiseAlert(ALERT_TREND, SEVERITY_WARNING,
                           "Deteriorating trend detected",
                           "Slope: " + String(trends.rms.slope, 4));
    }

    static uint32_t lastMqttPublish = 0;
    if (isMqttEnabled() && mqttMgr.isConnected() &&
        (millis() - lastMqttPublish > 5000)) {
        mqttMgr.publishFeatures(features);
        mqttMgr.publishSpectrum(spectrum, FFT_OUTPUT_SIZE);
        mqttMgr.publishPerformance(perfMon.getMetrics());
        lastMqttPublish = millis();
    }
}

void printWelcomeBanner() {
    Serial.println("\n");
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║                                                        ║");
    Serial.println("║   Motor Vibration Monitor                             ║");
    Serial.println("║   Version " FIRMWARE_VERSION "                                     ║");
    Serial.println("║                                                        ║");
    Serial.println("║   Powered by ESP32 + MPU6050                          ║");
    Serial.println("║   Real-time FFT Analysis & Anomaly Detection          ║");
    Serial.println("║                                                        ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.println("");
}

void handleError(const String& errorMsg) {
    Serial.println("\n========================================");
    Serial.println("         SYSTEM ERROR!");
    Serial.println("========================================");
    Serial.println(errorMsg);
    Serial.println("========================================\n");

    currentState = STATE_ERROR;

    while (true) {
        blinkLED(5, 100);
        delay(1000);
    }
}

void printSystemStatus() {
    Serial.println("\n--- System Status ---");
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.printf("State: %s\n",
                  currentState == STATE_MONITORING ? "MONITORING" :
                  currentState == STATE_CALIBRATING ? "CALIBRATING" :
                  currentState == STATE_ERROR ? "ERROR" : "INIT");
    Serial.printf("Total Samples: %lu\n", totalSamples);
    Serial.printf("Faults Detected: %lu\n", faultsDetected);
    Serial.printf("Buffer Fill: %d%%\n", signalProc.getBufferFillLevel());

    if (isNetworkEnabled()) {
        Serial.printf("WiFi: %s (RSSI: %d dBm)\n",
                      wifiMgr.isWiFiConnected() ? "Connected" : "Disconnected",
                      wifiMgr.getWiFiRSSI());
        if (wifiMgr.isProvisioningActive()) {
            Serial.printf("Provisioning AP: %s (%s)\n",
                          wifiMgr.getProvisioningSSID().c_str(),
                          wifiMgr.getProvisioningIP().c_str());
        }
        Serial.printf("MQTT: %s (msgs: %lu)\n",
                      mqttMgr.isConnected() ? "Connected" : "Disconnected",
                      mqttMgr.getMessageCount());
        Serial.printf("Web Clients: %d\n", webServer.getClientCount());
        Serial.printf("OTA: %s\n",
                      otaUpdater.isUpdating() ? "UPDATING" : "Ready");
    }

    Serial.printf("Data Buffer: %d/%d (%.1f%%)\n",
                  dataBuffer.getSize(), dataBuffer.getMaxSize(),
                  dataBuffer.getUsagePercent());
    Serial.printf("Fault Rate: %.2f%%\n", dataBuffer.getAverageFaultRate());

    PerformanceMetrics metrics = perfMon.getMetrics();
    Serial.printf("Loop Time: %lu ms\n", metrics.loopTime / 1000);
    Serial.printf("CPU Usage: %.1f%%\n", metrics.cpuUsage);

    Serial.printf("Temperature: %.1f °C\n", sensor.getTemperature());
    Serial.printf("Free Heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Modbus Requests: %lu\n", modbusServer.getRequestCount());
    Serial.printf("Safe Mode: %s\n", sysHardening.isInSafeMode() ? "Yes" : "No");
    Serial.printf("Watchdog: %s\n", sysHardening.isWatchdogEnabled() ? "Enabled" : "Disabled");
    Serial.printf("Thresholds: %.2f / %.2f\n",
                  faultDetect.getWarningThreshold(),
                  faultDetect.getCriticalThreshold());
    Serial.println("--------------------\n");
}

void blinkLED(uint8_t times, uint16_t delayMs) {
    for (uint8_t i = 0; i < times; i++) {
        digitalWrite(LED_STATUS_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_STATUS_PIN, LOW);
        delay(delayMs);
    }
}

void handleCloudMessage(const char* topic, const uint8_t* payload, size_t length) {
    if (!topic || !payload || length == 0) return;

    const String topicStr(topic);
    if (!topicStr.endsWith("/labels/response")) return;

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) return;

    auto handleResponseObject = [](JsonObject obj) {
        CloudLabelResponse resp;
        resp.sampleId = obj["sampleId"] | 0;
        if (resp.sampleId == 0) resp.sampleId = obj["sample_id"] | 0;
        if (resp.sampleId == 0) resp.sampleId = obj["timestamp"] | 0;

        resp.assignedLabel = obj["assignedLabel"] | 0;
        if (resp.assignedLabel == 0) resp.assignedLabel = obj["label"] | 0;

        resp.labelConfidence = obj["labelConfidence"] | 0.0f;
        if (resp.labelConfidence == 0.0f) resp.labelConfidence = obj["confidence"] | 0.0f;

        resp.requiresRetraining = obj["requiresRetraining"] | false;
        if (!resp.requiresRetraining) resp.requiresRetraining = obj["retrain"] | false;
        clManager.processLabelResponse(resp);
    };

    if (doc.is<JsonArray>()) {
        for (JsonVariant v : doc.as<JsonArray>()) {
            if (v.is<JsonObject>()) {
                handleResponseObject(v.as<JsonObject>());
            }
        }
        return;
    }

    JsonArray responses = doc["responses"].as<JsonArray>();
    if (!responses.isNull()) {
        for (JsonVariant v : responses) {
            if (v.is<JsonObject>()) {
                handleResponseObject(v.as<JsonObject>());
            }
        }
        return;
    }

    JsonArray labels = doc["labels"].as<JsonArray>();
    if (!labels.isNull()) {
        for (JsonVariant v : labels) {
            if (v.is<JsonObject>()) {
                handleResponseObject(v.as<JsonObject>());
            }
        }
        return;
    }

    JsonObject single = doc.as<JsonObject>();
    if (!single.isNull()) {
        handleResponseObject(single);
    }
}
