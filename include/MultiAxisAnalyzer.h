#ifndef MULTI_AXIS_ANALYZER_H
#define MULTI_AXIS_ANALYZER_H

#include <Arduino.h>
#include "Config.h"
#include "SignalProcessor.h"
#include "FeatureExtractor.h"

struct AxisFeatures {
    FeatureVector features;
    float magnitude;
    float dominantFreq;
    float energy;
};

struct MultiAxisAnalysis {
    AxisFeatures xAxis;
    AxisFeatures yAxis;
    AxisFeatures zAxis;
    AxisFeatures combined;

    float totalEnergy;
    float radialComponent;
    float axialComponent;
    float crossCorrelationXY;
    float crossCorrelationXZ;
    float crossCorrelationYZ;

    uint32_t timestamp;
};

class MultiAxisAnalyzer {
public:
    MultiAxisAnalyzer();
    ~MultiAxisAnalyzer();

    bool begin();

    void addSample(float x, float y, float z);

    MultiAxisAnalysis analyze();

    void reset();

    bool isReady() const;

private:
    SignalProcessor* procX;
    SignalProcessor* procY;
    SignalProcessor* procZ;

    FeatureExtractor extractor;

    uint32_t sampleCount;

    void analyzeAxis(SignalProcessor* proc, AxisFeatures& features);
    float calculateCrossCorrelation(const float* signal1, const float* signal2, size_t length);
    void calculateRadialAxial(const AxisFeatures& x, const AxisFeatures& y,
                              float& radial, float& axial);
};

#endif
