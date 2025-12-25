#ifndef EDGE_FEATURE_SELECTOR_H
#define EDGE_FEATURE_SELECTOR_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "ExtendedFeatureExtractor.h"

#define MAX_SELECTED_FEATURES 16
#define TOTAL_FEATURE_POOL 31
#define SELECTION_UPDATE_INTERVAL 1000
#define MIN_VARIANCE_THRESHOLD 0.001f
#define CORRELATION_THRESHOLD 0.95f
#define MIN_IMPORTANCE_THRESHOLD 0.01f

enum SelectionMethod {
    SELECT_VARIANCE = 0,
    SELECT_CORRELATION,
    SELECT_MUTUAL_INFO,
    SELECT_IMPORTANCE,
    SELECT_HYBRID
};

struct FeatureImportance {
    uint8_t featureIndex;
    float importance;
    float variance;
    float correlation;
    bool selected;
    const char* name;
};

struct FeatureSelectionConfig {
    uint8_t maxFeatures;
    float correlationThreshold;
    float varianceThreshold;
    bool useVarianceFilter;
    bool useCorrelationFilter;
    bool useMutualInfo;
    SelectionMethod method;
};

struct SelectionResult {
    uint8_t selectedIndices[MAX_SELECTED_FEATURES];
    uint8_t selectedCount;
    float totalVarianceExplained;
    float avgCorrelation;
    uint32_t lastUpdateSample;
};

struct FeatureGroup {
    const char* name;
    uint8_t indices[8];
    uint8_t count;
    float groupImportance;
};

typedef void (*SelectionCallback)(const uint8_t* selectedIndices, uint8_t count);

class EdgeFeatureSelector {
public:
    EdgeFeatureSelector();
    ~EdgeFeatureSelector();

    bool begin();
    void reset();

    uint8_t selectFeatures(const ExtendedFeatureVector& features,
                           float* selectedValues,
                           uint8_t* selectedIndices);

    uint8_t selectFeatures(const float* allFeatures, size_t featureCount,
                           float* selectedValues,
                           uint8_t* selectedIndices);

    void updateStatistics(const ExtendedFeatureVector& features);
    void updateStatistics(const float* features, size_t count);

    void setConfig(const FeatureSelectionConfig& config);
    FeatureSelectionConfig getConfig() const { return config; }

    void setMaxFeatures(uint8_t max);
    uint8_t getMaxFeatures() const { return config.maxFeatures; }

    void setSelectionMethod(SelectionMethod method) { config.method = method; }
    SelectionMethod getSelectionMethod() const { return config.method; }

    void setCorrelationThreshold(float threshold) { config.correlationThreshold = threshold; }
    void setVarianceThreshold(float threshold) { config.varianceThreshold = threshold; }

    SelectionResult getSelectionResult() const { return result; }
    const uint8_t* getSelectedIndices() const { return result.selectedIndices; }
    uint8_t getSelectedCount() const { return result.selectedCount; }

    FeatureImportance getFeatureImportance(uint8_t featureIndex) const;
    void getAllImportances(FeatureImportance* importances, uint8_t& count) const;
    void sortByImportance(FeatureImportance* importances, uint8_t count) const;

    void setFeatureLabels(const float* labels, size_t count);
    void clearLabels();

    void forceSelection(const uint8_t* indices, uint8_t count);
    void addMandatoryFeature(uint8_t featureIndex);
    void removeMandatoryFeature(uint8_t featureIndex);

    void enableAutoUpdate(bool enable) { autoUpdate = enable; }
    bool isAutoUpdateEnabled() const { return autoUpdate; }

    void triggerReselection();
    bool needsReselection() const;

    float computeCorrelation(uint8_t feature1, uint8_t feature2);
    float computeMutualInfo(uint8_t featureIndex);

    const FeatureGroup* getFeatureGroups(uint8_t& count) const;
    void selectFromGroup(const char* groupName, uint8_t maxFromGroup);

    void setCallback(SelectionCallback callback) { selectionCallback = callback; }

    String getFeatureName(uint8_t featureIndex) const;
    void printSelectionReport() const;
    String generateSelectionJSON() const;

    bool saveSelection(const char* filename);
    bool loadSelection(const char* filename);

    static const char* featureNames[TOTAL_FEATURE_POOL];

private:
    FeatureSelectionConfig config;
    SelectionResult result;

    FeatureImportance importances[TOTAL_FEATURE_POOL];

    float featureMeans[TOTAL_FEATURE_POOL];
    float featureVariances[TOTAL_FEATURE_POOL];
    float featureM2[TOTAL_FEATURE_POOL];
    uint32_t sampleCount;

    float correlationMatrix[TOTAL_FEATURE_POOL];

    uint8_t mandatoryFeatures[MAX_SELECTED_FEATURES];
    uint8_t mandatoryCount;

    float* labelBuffer;
    size_t labelCount;

    bool autoUpdate;
    uint32_t lastSelectionSample;
    bool selectionValid;

    SelectionCallback selectionCallback;

    static const FeatureGroup featureGroups[];
    static const uint8_t numFeatureGroups;

    void initializeImportances();
    void updateVariances(const float* features, size_t count);
    void computeCorrelations();
    void computeImportances();

    void selectByVariance();
    void selectByCorrelation();
    void selectByMutualInfo();
    void selectByImportance();
    void selectHybrid();

    void filterLowVariance();
    void filterHighCorrelation();

    void finalizeSelection();

    float extractFeature(const ExtendedFeatureVector& features, uint8_t index);
    void extractAllFeatures(const ExtendedFeatureVector& features, float* output);
};

#endif
