#ifndef TREND_ANALYZER_H
#define TREND_ANALYZER_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"

#define TREND_BUFFER_SIZE 100
#define TREND_SHORT_WINDOW 10
#define TREND_LONG_WINDOW 50

enum TrendDirection {
    TREND_STABLE = 0,
    TREND_INCREASING,
    TREND_DECREASING,
    TREND_VOLATILE
};

struct TrendData {
    float current;
    float shortTermAvg;
    float longTermAvg;
    float slope;
    float volatility;
    TrendDirection direction;
};

struct TrendAnalysis {
    TrendData rms;
    TrendData kurtosis;
    TrendData crestFactor;
    TrendData dominantFreq;
    TrendData anomalyScore;

    uint32_t timestamp;
    uint32_t sampleCount;

    bool isDeterioration;
    bool isImprovement;
    bool isAnomalous;
};

class TrendAnalyzer {
public:
    TrendAnalyzer();
    ~TrendAnalyzer();

    bool begin();

    void addSample(const FeatureVector& features, float anomalyScore);

    TrendAnalysis getAnalysis();

    bool isDeterioration();
    bool isImprovement();
    bool isAnomalous();

    float predictNextValue(const TrendData& trend, uint32_t stepsAhead = 1);

    void reset();

private:
    float rmsBuffer[TREND_BUFFER_SIZE];
    float kurtosisBuffer[TREND_BUFFER_SIZE];
    float crestFactorBuffer[TREND_BUFFER_SIZE];
    float dominantFreqBuffer[TREND_BUFFER_SIZE];
    float anomalyScoreBuffer[TREND_BUFFER_SIZE];

    uint32_t bufferIndex;
    uint32_t sampleCount;
    bool bufferFull;

    void analyzeTrend(const float* buffer, TrendData& trend);
    float calculateAverage(const float* buffer, size_t start, size_t window);
    float calculateSlope(const float* buffer, size_t window);
    float calculateVolatility(const float* buffer, size_t window);
    TrendDirection determineTrend(float slope, float volatility);

    void addToBuffer(float* buffer, float value);
};

#endif
