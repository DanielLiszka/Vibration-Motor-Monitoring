#include "EdgeFeatureSelector.h"
#include "StorageManager.h"
#include <math.h>

const char* EdgeFeatureSelector::featureNames[TOTAL_FEATURE_POOL] = {
    "rms", "peakToPeak", "kurtosis", "skewness", "crestFactor",
    "variance", "spectralCentroid", "spectralSpread", "bandPowerRatio", "dominantFrequency",
    "zeroCrossingRate", "spectralFlux", "spectralRolloff", "spectralFlatness", "harmonicRatio",
    "impulseFactor", "shapeFactor", "clearanceFactor", "totalEnergy", "entropyValue",
    "autocorrPeak", "mfcc0", "mfcc1", "mfcc2", "mfcc3",
    "mfcc4", "mfcc5", "mfcc6", "waveletE0", "waveletE1",
    "waveletE2"
};

const FeatureGroup EdgeFeatureSelector::featureGroups[] = {
    {"time_basic", {0, 1, 4, 5, 0, 0, 0, 0}, 4, 1.0f},
    {"time_stats", {2, 3, 15, 16, 17, 0, 0, 0}, 5, 0.9f},
    {"freq_basic", {6, 7, 8, 9, 0, 0, 0, 0}, 4, 1.0f},
    {"freq_adv", {10, 11, 12, 13, 14, 0, 0, 0}, 5, 0.8f},
    {"mfcc", {21, 22, 23, 24, 25, 26, 0, 0}, 6, 0.7f},
    {"wavelet", {28, 29, 30, 0, 0, 0, 0, 0}, 3, 0.6f}
};

const uint8_t EdgeFeatureSelector::numFeatureGroups = 6;

EdgeFeatureSelector::EdgeFeatureSelector()
    : sampleCount(0)
    , mandatoryCount(0)
    , labelBuffer(nullptr)
    , labelCount(0)
    , autoUpdate(true)
    , lastSelectionSample(0)
    , selectionValid(false)
    , selectionCallback(nullptr)
{
    config.maxFeatures = MAX_SELECTED_FEATURES;
    config.correlationThreshold = CORRELATION_THRESHOLD;
    config.varianceThreshold = MIN_VARIANCE_THRESHOLD;
    config.useVarianceFilter = true;
    config.useCorrelationFilter = true;
    config.useMutualInfo = false;
    config.method = SELECT_HYBRID;

    memset(&result, 0, sizeof(result));
    memset(featureMeans, 0, sizeof(featureMeans));
    memset(featureVariances, 0, sizeof(featureVariances));
    memset(featureM2, 0, sizeof(featureM2));
    memset(correlationMatrix, 0, sizeof(correlationMatrix));
    memset(mandatoryFeatures, 0, sizeof(mandatoryFeatures));

    initializeImportances();
}

EdgeFeatureSelector::~EdgeFeatureSelector() {
    if (labelBuffer) {
        free(labelBuffer);
    }
}

bool EdgeFeatureSelector::begin() {
    reset();

    result.selectedIndices[0] = 0;
    result.selectedIndices[1] = 1;
    result.selectedIndices[2] = 2;
    result.selectedIndices[3] = 4;
    result.selectedIndices[4] = 5;
    result.selectedIndices[5] = 6;
    result.selectedIndices[6] = 7;
    result.selectedIndices[7] = 8;
    result.selectedIndices[8] = 9;
    result.selectedIndices[9] = 10;
    result.selectedIndices[10] = 13;
    result.selectedIndices[11] = 15;
    result.selectedIndices[12] = 21;
    result.selectedIndices[13] = 22;
    result.selectedIndices[14] = 23;
    result.selectedIndices[15] = 28;
    result.selectedCount = MAX_SELECTED_FEATURES;
    selectionValid = true;

    DEBUG_PRINTLN("EdgeFeatureSelector initialized");
    return true;
}

void EdgeFeatureSelector::reset() {
    sampleCount = 0;
    selectionValid = false;
    lastSelectionSample = 0;

    memset(featureMeans, 0, sizeof(featureMeans));
    memset(featureVariances, 0, sizeof(featureVariances));
    memset(featureM2, 0, sizeof(featureM2));

    initializeImportances();
}

void EdgeFeatureSelector::initializeImportances() {
    for (int i = 0; i < TOTAL_FEATURE_POOL; i++) {
        importances[i].featureIndex = i;
        importances[i].importance = 0.0f;
        importances[i].variance = 0.0f;
        importances[i].correlation = 0.0f;
        importances[i].selected = false;
        importances[i].name = featureNames[i];
    }

    importances[0].importance = 1.0f;
    importances[2].importance = 0.95f;
    importances[4].importance = 0.9f;
    importances[6].importance = 0.85f;
    importances[9].importance = 0.85f;
    importances[15].importance = 0.8f;
}

uint8_t EdgeFeatureSelector::selectFeatures(const ExtendedFeatureVector& features,
                                            float* selectedValues,
                                            uint8_t* selectedIndices) {
    float allFeatures[TOTAL_FEATURE_POOL];
    extractAllFeatures(features, allFeatures);

    return selectFeatures(allFeatures, TOTAL_FEATURE_POOL, selectedValues, selectedIndices);
}

uint8_t EdgeFeatureSelector::selectFeatures(const float* allFeatures, size_t featureCount,
                                            float* selectedValues,
                                            uint8_t* selectedIndices) {
    if (autoUpdate) {
        updateStatistics(allFeatures, featureCount);
    }

    if (!selectionValid || needsReselection()) {
        triggerReselection();
    }

    for (int i = 0; i < result.selectedCount; i++) {
        uint8_t idx = result.selectedIndices[i];
        if (idx < featureCount) {
            selectedValues[i] = allFeatures[idx];
            selectedIndices[i] = idx;
        } else {
            selectedValues[i] = 0.0f;
            selectedIndices[i] = 0;
        }
    }

    return result.selectedCount;
}

void EdgeFeatureSelector::updateStatistics(const ExtendedFeatureVector& features) {
    float allFeatures[TOTAL_FEATURE_POOL];
    extractAllFeatures(features, allFeatures);
    updateStatistics(allFeatures, TOTAL_FEATURE_POOL);
}

void EdgeFeatureSelector::updateStatistics(const float* features, size_t count) {
    sampleCount++;

    for (size_t i = 0; i < count && i < TOTAL_FEATURE_POOL; i++) {
        float delta = features[i] - featureMeans[i];
        featureMeans[i] += delta / sampleCount;
        float delta2 = features[i] - featureMeans[i];
        featureM2[i] += delta * delta2;

        if (sampleCount > 1) {
            featureVariances[i] = featureM2[i] / (sampleCount - 1);
        }
    }
}

void EdgeFeatureSelector::updateVariances(const float* features, size_t count) {
    for (size_t i = 0; i < count && i < TOTAL_FEATURE_POOL; i++) {
        importances[i].variance = featureVariances[i];
    }
}

void EdgeFeatureSelector::computeImportances() {
    float maxVariance = 0.0f;
    for (int i = 0; i < TOTAL_FEATURE_POOL; i++) {
        if (featureVariances[i] > maxVariance) {
            maxVariance = featureVariances[i];
        }
    }

    if (maxVariance < 0.0001f) maxVariance = 1.0f;

    for (int i = 0; i < TOTAL_FEATURE_POOL; i++) {
        float normalizedVariance = featureVariances[i] / maxVariance;
        importances[i].variance = featureVariances[i];

        float baseImportance = importances[i].importance;
        if (baseImportance < 0.1f) baseImportance = 0.5f;

        importances[i].importance = 0.6f * baseImportance + 0.4f * normalizedVariance;
    }
}

void EdgeFeatureSelector::triggerReselection() {
    if (sampleCount < 10) return;

    computeImportances();

    switch (config.method) {
        case SELECT_VARIANCE:
            selectByVariance();
            break;
        case SELECT_CORRELATION:
            selectByCorrelation();
            break;
        case SELECT_MUTUAL_INFO:
            selectByMutualInfo();
            break;
        case SELECT_IMPORTANCE:
            selectByImportance();
            break;
        case SELECT_HYBRID:
        default:
            selectHybrid();
            break;
    }

    finalizeSelection();
    selectionValid = true;
    lastSelectionSample = sampleCount;

    if (selectionCallback) {
        selectionCallback(result.selectedIndices, result.selectedCount);
    }
}

void EdgeFeatureSelector::selectByVariance() {
    filterLowVariance();

    FeatureImportance sorted[TOTAL_FEATURE_POOL];
    memcpy(sorted, importances, sizeof(importances));
    sortByImportance(sorted, TOTAL_FEATURE_POOL);

    result.selectedCount = 0;
    for (int i = 0; i < TOTAL_FEATURE_POOL && result.selectedCount < config.maxFeatures; i++) {
        if (sorted[i].variance >= config.varianceThreshold) {
            result.selectedIndices[result.selectedCount++] = sorted[i].featureIndex;
        }
    }
}

void EdgeFeatureSelector::selectByImportance() {
    FeatureImportance sorted[TOTAL_FEATURE_POOL];
    memcpy(sorted, importances, sizeof(importances));
    sortByImportance(sorted, TOTAL_FEATURE_POOL);

    result.selectedCount = 0;
    for (int i = 0; i < TOTAL_FEATURE_POOL && result.selectedCount < config.maxFeatures; i++) {
        result.selectedIndices[result.selectedCount++] = sorted[i].featureIndex;
    }
}

void EdgeFeatureSelector::selectHybrid() {
    filterLowVariance();

    computeImportances();

    FeatureImportance sorted[TOTAL_FEATURE_POOL];
    memcpy(sorted, importances, sizeof(importances));
    sortByImportance(sorted, TOTAL_FEATURE_POOL);

    result.selectedCount = 0;

    for (int i = 0; i < mandatoryCount && result.selectedCount < config.maxFeatures; i++) {
        result.selectedIndices[result.selectedCount++] = mandatoryFeatures[i];
        importances[mandatoryFeatures[i]].selected = true;
    }

    for (int i = 0; i < TOTAL_FEATURE_POOL && result.selectedCount < config.maxFeatures; i++) {
        uint8_t idx = sorted[i].featureIndex;

        if (importances[idx].selected) continue;
        if (importances[idx].variance < config.varianceThreshold) continue;

        bool tooCorrelated = false;
        for (int j = 0; j < result.selectedCount; j++) {
            float corr = computeCorrelation(idx, result.selectedIndices[j]);
            if (fabs(corr) > config.correlationThreshold) {
                tooCorrelated = true;
                break;
            }
        }

        if (!tooCorrelated) {
            result.selectedIndices[result.selectedCount++] = idx;
            importances[idx].selected = true;
        }
    }
}

void EdgeFeatureSelector::selectByCorrelation() {
    selectHybrid();
}

void EdgeFeatureSelector::selectByMutualInfo() {
    selectHybrid();
}

void EdgeFeatureSelector::filterLowVariance() {
    for (int i = 0; i < TOTAL_FEATURE_POOL; i++) {
        if (importances[i].variance < config.varianceThreshold) {
            importances[i].importance *= 0.1f;
        }
    }
}

void EdgeFeatureSelector::filterHighCorrelation() {
}

void EdgeFeatureSelector::finalizeSelection() {
    result.totalVarianceExplained = 0.0f;
    float totalVariance = 0.0f;

    for (int i = 0; i < TOTAL_FEATURE_POOL; i++) {
        totalVariance += featureVariances[i];
    }

    for (int i = 0; i < result.selectedCount; i++) {
        result.totalVarianceExplained += featureVariances[result.selectedIndices[i]];
    }

    if (totalVariance > 0.0f) {
        result.totalVarianceExplained /= totalVariance;
    }

    result.lastUpdateSample = sampleCount;
}

float EdgeFeatureSelector::computeCorrelation(uint8_t feature1, uint8_t feature2) {
    return 0.0f;
}

float EdgeFeatureSelector::computeMutualInfo(uint8_t featureIndex) {
    return 0.0f;
}

void EdgeFeatureSelector::sortByImportance(FeatureImportance* arr, uint8_t count) const {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (arr[j].importance < arr[j + 1].importance) {
                FeatureImportance temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

void EdgeFeatureSelector::setConfig(const FeatureSelectionConfig& newConfig) {
    config = newConfig;
    selectionValid = false;
}

void EdgeFeatureSelector::setMaxFeatures(uint8_t max) {
    if (max <= MAX_SELECTED_FEATURES) {
        config.maxFeatures = max;
        selectionValid = false;
    }
}

FeatureImportance EdgeFeatureSelector::getFeatureImportance(uint8_t featureIndex) const {
    if (featureIndex < TOTAL_FEATURE_POOL) {
        return importances[featureIndex];
    }
    FeatureImportance empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

void EdgeFeatureSelector::getAllImportances(FeatureImportance* out, uint8_t& count) const {
    count = TOTAL_FEATURE_POOL;
    memcpy(out, importances, sizeof(importances));
}

void EdgeFeatureSelector::forceSelection(const uint8_t* indices, uint8_t count) {
    result.selectedCount = (count < MAX_SELECTED_FEATURES) ? count : MAX_SELECTED_FEATURES;
    memcpy(result.selectedIndices, indices, result.selectedCount);
    selectionValid = true;
}

void EdgeFeatureSelector::addMandatoryFeature(uint8_t featureIndex) {
    if (mandatoryCount < MAX_SELECTED_FEATURES && featureIndex < TOTAL_FEATURE_POOL) {
        mandatoryFeatures[mandatoryCount++] = featureIndex;
        selectionValid = false;
    }
}

void EdgeFeatureSelector::removeMandatoryFeature(uint8_t featureIndex) {
    for (int i = 0; i < mandatoryCount; i++) {
        if (mandatoryFeatures[i] == featureIndex) {
            for (int j = i; j < mandatoryCount - 1; j++) {
                mandatoryFeatures[j] = mandatoryFeatures[j + 1];
            }
            mandatoryCount--;
            selectionValid = false;
            break;
        }
    }
}

bool EdgeFeatureSelector::needsReselection() const {
    return (sampleCount - lastSelectionSample) >= SELECTION_UPDATE_INTERVAL;
}

void EdgeFeatureSelector::setFeatureLabels(const float* labels, size_t count) {
    if (labelBuffer) {
        free(labelBuffer);
    }
    labelBuffer = (float*)malloc(count * sizeof(float));
    if (labelBuffer) {
        memcpy(labelBuffer, labels, count * sizeof(float));
        labelCount = count;
    }
}

void EdgeFeatureSelector::clearLabels() {
    if (labelBuffer) {
        free(labelBuffer);
        labelBuffer = nullptr;
    }
    labelCount = 0;
}

const FeatureGroup* EdgeFeatureSelector::getFeatureGroups(uint8_t& count) const {
    count = numFeatureGroups;
    return featureGroups;
}

void EdgeFeatureSelector::selectFromGroup(const char* groupName, uint8_t maxFromGroup) {
    for (int g = 0; g < numFeatureGroups; g++) {
        if (strcmp(featureGroups[g].name, groupName) == 0) {
            uint8_t count = (featureGroups[g].count < maxFromGroup) ?
                           featureGroups[g].count : maxFromGroup;
            for (int i = 0; i < count; i++) {
                addMandatoryFeature(featureGroups[g].indices[i]);
            }
            break;
        }
    }
}

String EdgeFeatureSelector::getFeatureName(uint8_t featureIndex) const {
    if (featureIndex < TOTAL_FEATURE_POOL) {
        return String(featureNames[featureIndex]);
    }
    return "unknown";
}

void EdgeFeatureSelector::printSelectionReport() const {
    Serial.println("\n=== Feature Selection Report ===");
    Serial.printf("Samples processed: %lu\n", sampleCount);
    Serial.printf("Selected features: %d/%d\n", result.selectedCount, config.maxFeatures);
    Serial.printf("Variance explained: %.1f%%\n", result.totalVarianceExplained * 100.0f);

    Serial.println("\nSelected features:");
    for (int i = 0; i < result.selectedCount; i++) {
        uint8_t idx = result.selectedIndices[i];
        Serial.printf("  [%d] %s (importance: %.3f, variance: %.4f)\n",
                     idx, featureNames[idx],
                     importances[idx].importance, importances[idx].variance);
    }
}

String EdgeFeatureSelector::generateSelectionJSON() const {
    String json = "{\"selection\":{";
    json += "\"count\":" + String(result.selectedCount) + ",";
    json += "\"varianceExplained\":" + String(result.totalVarianceExplained, 4) + ",";
    json += "\"features\":[";

    for (int i = 0; i < result.selectedCount; i++) {
        if (i > 0) json += ",";
        uint8_t idx = result.selectedIndices[i];
        json += "{\"index\":" + String(idx);
        json += ",\"name\":\"" + String(featureNames[idx]) + "\"";
        json += ",\"importance\":" + String(importances[idx].importance, 4) + "}";
    }

    json += "]}}";
    return json;
}

void EdgeFeatureSelector::extractAllFeatures(const ExtendedFeatureVector& features, float* output) {
    output[0] = features.rms;
    output[1] = features.peakToPeak;
    output[2] = features.kurtosis;
    output[3] = features.skewness;
    output[4] = features.crestFactor;
    output[5] = features.variance;
    output[6] = features.spectralCentroid;
    output[7] = features.spectralSpread;
    output[8] = features.bandPowerRatio;
    output[9] = features.dominantFrequency;
    output[10] = features.zeroCrossingRate;
    output[11] = features.spectralFlux;
    output[12] = features.spectralRolloff;
    output[13] = features.spectralFlatness;
    output[14] = features.harmonicRatio;
    output[15] = features.impulseFactor;
    output[16] = features.shapeFactor;
    output[17] = features.clearanceFactor;
    output[18] = features.totalEnergy;
    output[19] = features.entropyValue;
    output[20] = features.autocorrPeak;

    for (int i = 0; i < MFCC_COUNT && i < 7; i++) {
        output[21 + i] = features.mfcc[i];
    }

    for (int i = 0; i < WAVELET_LEVELS && i < 3; i++) {
        output[28 + i] = features.waveletEnergy[i];
    }
}

float EdgeFeatureSelector::extractFeature(const ExtendedFeatureVector& features, uint8_t index) {
    float all[TOTAL_FEATURE_POOL];
    extractAllFeatures(features, all);
    return (index < TOTAL_FEATURE_POOL) ? all[index] : 0.0f;
}

bool EdgeFeatureSelector::saveSelection(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = "FSEL";
    data += String(result.selectedCount) + ",";
    for (int i = 0; i < result.selectedCount; i++) {
        data += String(result.selectedIndices[i]) + ",";
    }

    return storage.saveLog(filename, data);
}

bool EdgeFeatureSelector::loadSelection(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = storage.readLog(filename);
    if (data.length() < 5 || !data.startsWith("FSEL")) return false;

    return true;
}
