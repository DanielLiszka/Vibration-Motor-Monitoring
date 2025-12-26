#ifndef CONTINUOUS_LEARNING_MANAGER_H
#define CONTINUOUS_LEARNING_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "OnlineLearner.h"
#include "DriftDetector.h"
#include "SelfCalibratingModel.h"
#include "DataLogger.h"
#include "CloudConnector.h"
#include "ModelManager.h"
#include "FaultDetector.h"

#define CL_MAX_PENDING_SAMPLES 50
#define CL_UPLOAD_BATCH_SIZE 20
#define CL_MIN_CONFIDENCE_FOR_LABEL 0.85
#define CL_DRIFT_CHECK_INTERVAL_MS 60000
#define CL_CALIBRATION_SAMPLES 500

enum ContinuousLearningState {
    CL_STATE_IDLE = 0,
    CL_STATE_COLLECTING,
    CL_STATE_CALIBRATING,
    CL_STATE_MONITORING,
    CL_STATE_DRIFT_DETECTED,
    CL_STATE_UPLOADING,
    CL_STATE_MODEL_UPDATE,
    CL_STATE_ERROR
};

enum LabelSource {
    LABEL_AUTO_HIGH_CONFIDENCE = 0,
    LABEL_USER_CONFIRMED,
    LABEL_CLOUD_ASSIGNED,
    LABEL_UNLABELED
};

struct PendingSample {
    float features[ONLINE_FEATURE_DIM];
    uint8_t predictedLabel;
    float confidence;
    LabelSource labelSource;
    uint32_t timestamp;
    bool uploaded;
};

struct ContinuousLearningStats {
    uint32_t totalSamplesCollected;
    uint32_t samplesUploadedToCloud;
    uint32_t labelsReceivedFromCloud;
    uint32_t onlineLearnerUpdates;
    uint32_t driftEventsDetected;
    uint32_t modelUpdatesReceived;
    float currentAccuracyEstimate;
    uint32_t lastUploadTime;
    uint32_t lastModelUpdateTime;
};

struct CloudLabelResponse {
    uint32_t sampleId;
    uint8_t assignedLabel;
    float labelConfidence;
    bool requiresRetraining;
};

typedef void (*ModelUpdateCallback)(bool success, const char* version);
typedef void (*DriftAlertCallback)(DriftType driftType, float magnitude);
typedef void (*LabelReceivedCallback)(uint32_t sampleId, uint8_t label);

class ContinuousLearningManager {
public:
    ContinuousLearningManager();
    ~ContinuousLearningManager();

    bool begin();
    void reset();


    void setOnlineLearner(OnlineLearner* learner) { onlineLearner = learner; }
    void setDriftDetector(DriftDetector* detector) { driftDetector = detector; }
    void setCalibrationModel(SelfCalibratingModel* model) { calibModel = model; }
    void setDataLogger(DataLogger* logger) { dataLogger = logger; }
    void setCloudConnector(CloudConnector* connector) { cloudConnector = connector; }
    void setModelManager(ModelManager* manager) { modelManager = manager; }


    void update();
    void processSample(const float* features, size_t numFeatures,
                       const FaultResult& prediction);


    ContinuousLearningState getState() const { return currentState; }
    void setState(ContinuousLearningState state);
    bool isActive() const { return active; }
    void setActive(bool enabled) { active = enabled; }


    void collectSample(const float* features, uint8_t predictedLabel,
                       float confidence, LabelSource source = LABEL_AUTO_HIGH_CONFIDENCE);
    void confirmLabel(size_t sampleIndex, uint8_t confirmedLabel);
    size_t getPendingSampleCount() const { return pendingCount; }


    bool uploadPendingSamples();
    bool checkForModelUpdate();
    bool downloadAndApplyUpdate();
    void processLabelResponse(const CloudLabelResponse& response);


    void checkForDrift(const FeatureVector& features);
    void handleDriftDetected(DriftType type);
    void triggerRecalibration();


    void updateOnlineLearner(const float* features, uint8_t label, float confidence);
    void replayReservoir();


    ContinuousLearningStats getStats() const { return stats; }
    void resetStats();


    void setModelUpdateCallback(ModelUpdateCallback callback) { modelUpdateCb = callback; }
    void setDriftAlertCallback(DriftAlertCallback callback) { driftAlertCb = callback; }
    void setLabelReceivedCallback(LabelReceivedCallback callback) { labelReceivedCb = callback; }


    void setAutoUploadEnabled(bool enabled) { autoUploadEnabled = enabled; }
    void setAutoLabelEnabled(bool enabled) { autoLabelEnabled = enabled; }
    void setMinConfidenceForAutoLabel(float conf) { minConfidenceForAutoLabel = conf; }
    void setUploadInterval(uint32_t intervalMs) { uploadIntervalMs = intervalMs; }
    void setDriftCheckInterval(uint32_t intervalMs) { driftCheckIntervalMs = intervalMs; }


    String getStatusJSON() const;
    String generateReport() const;

private:

    ContinuousLearningState currentState;
    bool active;
    bool initialized;


    OnlineLearner* onlineLearner;
    DriftDetector* driftDetector;
    SelfCalibratingModel* calibModel;
    DataLogger* dataLogger;
    CloudConnector* cloudConnector;
    ModelManager* modelManager;


    PendingSample pendingSamples[CL_MAX_PENDING_SAMPLES];
    size_t pendingCount;
    size_t pendingHead;


    ContinuousLearningStats stats;


    bool autoUploadEnabled;
    bool autoLabelEnabled;
    float minConfidenceForAutoLabel;
    uint32_t uploadIntervalMs;
    uint32_t driftCheckIntervalMs;


    uint32_t lastUploadTime;
    uint32_t lastDriftCheckTime;
    uint32_t lastModelCheckTime;


    ModelUpdateCallback modelUpdateCb;
    DriftAlertCallback driftAlertCb;
    LabelReceivedCallback labelReceivedCb;


    void addToPendingQueue(const PendingSample& sample);
    bool uploadBatch(size_t batchSize);
    void processCloudResponse(const String& response);
    void transitionState(ContinuousLearningState newState);
    bool shouldUpload() const;
    bool shouldCheckDrift() const;
    void logEvent(const char* event);
};

#endif
