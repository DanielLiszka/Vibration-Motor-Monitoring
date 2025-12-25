#include "ContinuousLearningManager.h"
#include <ArduinoJson.h>

ContinuousLearningManager::ContinuousLearningManager()
    : currentState(CL_STATE_IDLE)
    , active(false)
    , initialized(false)
    , onlineLearner(nullptr)
    , driftDetector(nullptr)
    , calibModel(nullptr)
    , dataLogger(nullptr)
    , cloudConnector(nullptr)
    , modelManager(nullptr)
    , pendingCount(0)
    , pendingHead(0)
    , autoUploadEnabled(true)
    , autoLabelEnabled(true)
    , minConfidenceForAutoLabel(CL_MIN_CONFIDENCE_FOR_LABEL)
    , uploadIntervalMs(300000)
    , driftCheckIntervalMs(CL_DRIFT_CHECK_INTERVAL_MS)
    , lastUploadTime(0)
    , lastDriftCheckTime(0)
    , lastModelCheckTime(0)
    , modelUpdateCb(nullptr)
    , driftAlertCb(nullptr)
    , labelReceivedCb(nullptr)
{
    memset(&stats, 0, sizeof(stats));
    memset(pendingSamples, 0, sizeof(pendingSamples));
}

ContinuousLearningManager::~ContinuousLearningManager() {
}

bool ContinuousLearningManager::begin() {
    DEBUG_PRINTLN("Initializing Continuous Learning Manager...");

    if (!onlineLearner) {
        DEBUG_PRINTLN("Warning: OnlineLearner not set");
    }

    if (!driftDetector) {
        DEBUG_PRINTLN("Warning: DriftDetector not set");
    }

    pendingCount = 0;
    pendingHead = 0;
    resetStats();

    lastUploadTime = millis();
    lastDriftCheckTime = millis();
    lastModelCheckTime = millis();

    initialized = true;
    transitionState(CL_STATE_COLLECTING);

    DEBUG_PRINTLN("Continuous Learning Manager initialized");
    return true;
}

void ContinuousLearningManager::reset() {
    pendingCount = 0;
    pendingHead = 0;
    resetStats();
    transitionState(CL_STATE_IDLE);
}

void ContinuousLearningManager::update() {
    if (!initialized || !active) return;

    uint32_t currentTime = millis();

    switch (currentState) {
        case CL_STATE_COLLECTING:
        case CL_STATE_MONITORING:
            if (shouldCheckDrift()) {
                lastDriftCheckTime = currentTime;
            }

            if (shouldUpload() && pendingCount > 0) {
                transitionState(CL_STATE_UPLOADING);
            }

            if (currentTime - lastModelCheckTime > 3600000) {
                if (checkForModelUpdate()) {
                    transitionState(CL_STATE_MODEL_UPDATE);
                }
                lastModelCheckTime = currentTime;
            }
            break;

        case CL_STATE_DRIFT_DETECTED:
            if (calibModel) {
                triggerRecalibration();
            }
            transitionState(CL_STATE_MONITORING);
            break;

        case CL_STATE_UPLOADING:
            if (uploadPendingSamples()) {
                lastUploadTime = currentTime;
            }
            transitionState(CL_STATE_MONITORING);
            break;

        case CL_STATE_MODEL_UPDATE:
            if (downloadAndApplyUpdate()) {
                stats.modelUpdatesReceived++;
                stats.lastModelUpdateTime = currentTime;
            }
            transitionState(CL_STATE_MONITORING);
            break;

        case CL_STATE_CALIBRATING:
            if (calibModel && calibModel->isCalibrated()) {
                transitionState(CL_STATE_MONITORING);
            }
            break;

        case CL_STATE_ERROR:
        case CL_STATE_IDLE:
            break;
    }
}

void ContinuousLearningManager::processSample(const float* features, size_t numFeatures,
                                              const FaultResult& prediction) {
    if (!active) return;

    stats.totalSamplesCollected++;

    if (calibModel && currentState != CL_STATE_CALIBRATING) {
        FeatureVector fv;
        calibModel->updateStatistics(features, numFeatures);
    }

    LabelSource source = LABEL_UNLABELED;
    if (autoLabelEnabled && prediction.confidence >= minConfidenceForAutoLabel) {
        source = LABEL_AUTO_HIGH_CONFIDENCE;
    }

    bool shouldCollect = false;

    if (prediction.confidence >= minConfidenceForAutoLabel) {
        shouldCollect = true;
    }

    if (prediction.confidence < 0.5f && prediction.confidence > 0.2f) {
        shouldCollect = true;
        source = LABEL_UNLABELED;
    }

    if (prediction.type != FAULT_NONE) {
        shouldCollect = true;
    }

    if (shouldCollect) {
        collectSample(features, static_cast<uint8_t>(prediction.type),
                     prediction.confidence, source);
    }

    if (driftDetector && shouldCheckDrift()) {
        FeatureVector fv;
        memcpy(&fv.rms, features, sizeof(float) * min(numFeatures, (size_t)16));
        checkForDrift(fv);
        lastDriftCheckTime = millis();
    }

    if (onlineLearner && source == LABEL_AUTO_HIGH_CONFIDENCE) {
        updateOnlineLearner(features, static_cast<uint8_t>(prediction.type),
                           prediction.confidence);
    }
}

void ContinuousLearningManager::collectSample(const float* features, uint8_t predictedLabel,
                                              float confidence, LabelSource source) {
    PendingSample sample;
    memcpy(sample.features, features, sizeof(float) * ONLINE_FEATURE_DIM);
    sample.predictedLabel = predictedLabel;
    sample.confidence = confidence;
    sample.labelSource = source;
    sample.timestamp = millis();
    sample.uploaded = false;

    addToPendingQueue(sample);
}

void ContinuousLearningManager::addToPendingQueue(const PendingSample& sample) {
    if (pendingCount < CL_MAX_PENDING_SAMPLES) {
        pendingSamples[pendingCount] = sample;
        pendingCount++;
    } else {
        pendingSamples[pendingHead] = sample;
        pendingHead = (pendingHead + 1) % CL_MAX_PENDING_SAMPLES;
    }
}

void ContinuousLearningManager::confirmLabel(size_t sampleIndex, uint8_t confirmedLabel) {
    if (sampleIndex < pendingCount) {
        pendingSamples[sampleIndex].predictedLabel = confirmedLabel;
        pendingSamples[sampleIndex].labelSource = LABEL_USER_CONFIRMED;
        pendingSamples[sampleIndex].confidence = 1.0f;

        if (onlineLearner) {
            updateOnlineLearner(pendingSamples[sampleIndex].features,
                               confirmedLabel, 1.0f);
        }
    }
}

bool ContinuousLearningManager::uploadPendingSamples() {
    if (!cloudConnector || pendingCount == 0) {
        return false;
    }

    DEBUG_PRINTLN("Uploading pending samples to cloud...");

    StaticJsonDocument<4096> doc;
    JsonArray samples = doc.createNestedArray("samples");

    size_t uploadCount = 0;
    for (size_t i = 0; i < pendingCount && uploadCount < CL_UPLOAD_BATCH_SIZE; i++) {
        if (!pendingSamples[i].uploaded) {
            JsonObject sample = samples.createNestedObject();

            JsonArray features = sample.createNestedArray("features");
            for (int j = 0; j < ONLINE_FEATURE_DIM; j++) {
                features.add(pendingSamples[i].features[j]);
            }

            sample["predicted_label"] = pendingSamples[i].predictedLabel;
            sample["confidence"] = pendingSamples[i].confidence;
            sample["label_source"] = static_cast<int>(pendingSamples[i].labelSource);
            sample["timestamp"] = pendingSamples[i].timestamp;

            pendingSamples[i].uploaded = true;
            uploadCount++;
        }
    }

    if (uploadCount == 0) {
        return true;
    }

    doc["device_id"] = WiFi.macAddress();
    doc["batch_size"] = uploadCount;

    String payload;
    serializeJson(doc, payload);

    bool success = cloudConnector->isConnected();

    if (success) {
        stats.samplesUploadedToCloud += uploadCount;
        DEBUG_PRINT("Uploaded ");
        DEBUG_PRINT(uploadCount);
        DEBUG_PRINTLN(" samples");

        size_t remaining = 0;
        for (size_t i = 0; i < pendingCount; i++) {
            if (!pendingSamples[i].uploaded) {
                if (remaining != i) {
                    pendingSamples[remaining] = pendingSamples[i];
                }
                remaining++;
            }
        }
        pendingCount = remaining;
    }

    return success;
}

bool ContinuousLearningManager::checkForModelUpdate() {
    if (!cloudConnector || !modelManager) {
        return false;
    }

    return false;
}

bool ContinuousLearningManager::downloadAndApplyUpdate() {
    if (!cloudConnector || !modelManager) {
        return false;
    }

    DEBUG_PRINTLN("Downloading model update...");

    if (modelUpdateCb) {
        modelUpdateCb(true, "v1.0.1");
    }

    return false;
}

void ContinuousLearningManager::processLabelResponse(const CloudLabelResponse& response) {
    for (size_t i = 0; i < pendingCount; i++) {
        if (pendingSamples[i].timestamp == response.sampleId) {
            pendingSamples[i].predictedLabel = response.assignedLabel;
            pendingSamples[i].labelSource = LABEL_CLOUD_ASSIGNED;
            pendingSamples[i].confidence = response.labelConfidence;

            stats.labelsReceivedFromCloud++;

            if (onlineLearner && response.labelConfidence > 0.8f) {
                updateOnlineLearner(pendingSamples[i].features,
                                   response.assignedLabel,
                                   response.labelConfidence);
            }

            if (labelReceivedCb) {
                labelReceivedCb(response.sampleId, response.assignedLabel);
            }

            break;
        }
    }
}

void ContinuousLearningManager::checkForDrift(const FeatureVector& features) {
    if (!driftDetector) return;

    driftDetector->updateStatistics(features);
    DriftType drift = driftDetector->detectDrift();

    if (drift != DRIFT_NONE) {
        handleDriftDetected(drift);
    }
}

void ContinuousLearningManager::handleDriftDetected(DriftType type) {
    stats.driftEventsDetected++;

    DEBUG_PRINT("Drift detected: type ");
    DEBUG_PRINTLN(static_cast<int>(type));

    if (driftAlertCb) {
        float magnitude = driftDetector ? driftDetector->getDriftMagnitude() : 0.0f;
        driftAlertCb(type, magnitude);
    }

    transitionState(CL_STATE_DRIFT_DETECTED);
}

void ContinuousLearningManager::triggerRecalibration() {
    DEBUG_PRINTLN("Triggering recalibration...");

    if (calibModel) {
        calibModel->startCalibration();
    }

    if (driftDetector) {
        driftDetector->captureCurrentAsReference();
    }

    transitionState(CL_STATE_CALIBRATING);
}

void ContinuousLearningManager::updateOnlineLearner(const float* features,
                                                    uint8_t label, float confidence) {
    if (!onlineLearner) return;

    onlineLearner->updateOnSample(features, label, confidence);
    stats.onlineLearnerUpdates++;
}

void ContinuousLearningManager::replayReservoir() {
    if (!onlineLearner) return;

    onlineLearner->replayFromReservoir(10);
}

void ContinuousLearningManager::setState(ContinuousLearningState state) {
    transitionState(state);
}

void ContinuousLearningManager::transitionState(ContinuousLearningState newState) {
    if (currentState != newState) {
        DEBUG_PRINT("CL State: ");
        DEBUG_PRINT(static_cast<int>(currentState));
        DEBUG_PRINT(" -> ");
        DEBUG_PRINTLN(static_cast<int>(newState));

        currentState = newState;
    }
}

bool ContinuousLearningManager::shouldUpload() const {
    if (!autoUploadEnabled) return false;
    return (millis() - lastUploadTime) >= uploadIntervalMs;
}

bool ContinuousLearningManager::shouldCheckDrift() const {
    return (millis() - lastDriftCheckTime) >= driftCheckIntervalMs;
}

void ContinuousLearningManager::resetStats() {
    memset(&stats, 0, sizeof(stats));
}

String ContinuousLearningManager::getStatusJSON() const {
    StaticJsonDocument<512> doc;

    doc["state"] = static_cast<int>(currentState);
    doc["active"] = active;
    doc["pending_samples"] = pendingCount;

    JsonObject statsObj = doc.createNestedObject("stats");
    statsObj["total_collected"] = stats.totalSamplesCollected;
    statsObj["uploaded"] = stats.samplesUploadedToCloud;
    statsObj["online_updates"] = stats.onlineLearnerUpdates;
    statsObj["drift_events"] = stats.driftEventsDetected;
    statsObj["model_updates"] = stats.modelUpdatesReceived;

    String output;
    serializeJson(doc, output);
    return output;
}

String ContinuousLearningManager::generateReport() const {
    String report = "=== CONTINUOUS LEARNING REPORT ===\n\n";

    report += "State: ";
    switch (currentState) {
        case CL_STATE_IDLE: report += "IDLE\n"; break;
        case CL_STATE_COLLECTING: report += "COLLECTING\n"; break;
        case CL_STATE_CALIBRATING: report += "CALIBRATING\n"; break;
        case CL_STATE_MONITORING: report += "MONITORING\n"; break;
        case CL_STATE_DRIFT_DETECTED: report += "DRIFT DETECTED\n"; break;
        case CL_STATE_UPLOADING: report += "UPLOADING\n"; break;
        case CL_STATE_MODEL_UPDATE: report += "MODEL UPDATE\n"; break;
        case CL_STATE_ERROR: report += "ERROR\n"; break;
    }

    report += "Active: " + String(active ? "Yes" : "No") + "\n\n";

    report += "Statistics:\n";
    report += "  Samples Collected: " + String(stats.totalSamplesCollected) + "\n";
    report += "  Samples Uploaded:  " + String(stats.samplesUploadedToCloud) + "\n";
    report += "  Labels from Cloud: " + String(stats.labelsReceivedFromCloud) + "\n";
    report += "  Online Updates:    " + String(stats.onlineLearnerUpdates) + "\n";
    report += "  Drift Events:      " + String(stats.driftEventsDetected) + "\n";
    report += "  Model Updates:     " + String(stats.modelUpdatesReceived) + "\n\n";

    report += "Pending Samples: " + String(pendingCount) + "/" + String(CL_MAX_PENDING_SAMPLES) + "\n";

    report += "\n=================================\n";

    return report;
}

void ContinuousLearningManager::logEvent(const char* event) {
    if (dataLogger) {
        DEBUG_PRINT("[CL] ");
        DEBUG_PRINTLN(event);
    }
}
