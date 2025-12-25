#include "TrendAnalyzer.h"
#include <math.h>

TrendAnalyzer::TrendAnalyzer()
    : bufferIndex(0)
    , sampleCount(0)
    , bufferFull(false)
{
    memset(rmsBuffer, 0, sizeof(rmsBuffer));
    memset(kurtosisBuffer, 0, sizeof(kurtosisBuffer));
    memset(crestFactorBuffer, 0, sizeof(crestFactorBuffer));
    memset(dominantFreqBuffer, 0, sizeof(dominantFreqBuffer));
    memset(anomalyScoreBuffer, 0, sizeof(anomalyScoreBuffer));
}

TrendAnalyzer::~TrendAnalyzer() {
}

bool TrendAnalyzer::begin() {
    DEBUG_PRINTLN("Initializing Trend Analyzer...");
    reset();
    return true;
}

void TrendAnalyzer::addSample(const FeatureVector& features, float anomalyScore) {
    addToBuffer(rmsBuffer, features.rms);
    addToBuffer(kurtosisBuffer, features.kurtosis);
    addToBuffer(crestFactorBuffer, features.crestFactor);
    addToBuffer(dominantFreqBuffer, features.dominantFrequency);
    addToBuffer(anomalyScoreBuffer, anomalyScore);

    sampleCount++;

    bufferIndex = (bufferIndex + 1) % TREND_BUFFER_SIZE;
    if (bufferIndex == 0) {
        bufferFull = true;
    }
}

TrendAnalysis TrendAnalyzer::getAnalysis() {
    TrendAnalysis analysis;

    analyzeTrend(rmsBuffer, analysis.rms);
    analyzeTrend(kurtosisBuffer, analysis.kurtosis);
    analyzeTrend(crestFactorBuffer, analysis.crestFactor);
    analyzeTrend(dominantFreqBuffer, analysis.dominantFreq);
    analyzeTrend(anomalyScoreBuffer, analysis.anomalyScore);

    analysis.timestamp = millis();
    analysis.sampleCount = sampleCount;

    analysis.isDeterioration = isDeterioration();
    analysis.isImprovement = isImprovement();
    analysis.isAnomalous = isAnomalous();

    return analysis;
}

bool TrendAnalyzer::isDeterioration() {
    if (!bufferFull) return false;

    TrendData rms, anomaly;
    analyzeTrend(rmsBuffer, rms);
    analyzeTrend(anomalyScoreBuffer, anomaly);

    return (rms.direction == TREND_INCREASING && rms.slope > 0.1) ||
           (anomaly.direction == TREND_INCREASING && anomaly.slope > 0.05);
}

bool TrendAnalyzer::isImprovement() {
    if (!bufferFull) return false;

    TrendData anomaly;
    analyzeTrend(anomalyScoreBuffer, anomaly);

    return (anomaly.direction == TREND_DECREASING && anomaly.slope < -0.05);
}

bool TrendAnalyzer::isAnomalous() {
    if (!bufferFull) return false;

    TrendData rms, kurtosis;
    analyzeTrend(rmsBuffer, rms);
    analyzeTrend(kurtosisBuffer, kurtosis);

    return (rms.direction == TREND_VOLATILE || kurtosis.direction == TREND_VOLATILE);
}

float TrendAnalyzer::predictNextValue(const TrendData& trend, uint32_t stepsAhead) {
    return trend.current + (trend.slope * stepsAhead);
}

void TrendAnalyzer::reset() {
    bufferIndex = 0;
    sampleCount = 0;
    bufferFull = false;
    memset(rmsBuffer, 0, sizeof(rmsBuffer));
    memset(kurtosisBuffer, 0, sizeof(kurtosisBuffer));
    memset(crestFactorBuffer, 0, sizeof(crestFactorBuffer));
    memset(dominantFreqBuffer, 0, sizeof(dominantFreqBuffer));
    memset(anomalyScoreBuffer, 0, sizeof(anomalyScoreBuffer));
}

void TrendAnalyzer::analyzeTrend(const float* buffer, TrendData& trend) {
    size_t effectiveSize = bufferFull ? TREND_BUFFER_SIZE : bufferIndex;

    if (effectiveSize == 0) {
        trend.current = 0;
        trend.shortTermAvg = 0;
        trend.longTermAvg = 0;
        trend.slope = 0;
        trend.volatility = 0;
        trend.direction = TREND_STABLE;
        return;
    }

    size_t currentIdx = (bufferIndex - 1 + TREND_BUFFER_SIZE) % TREND_BUFFER_SIZE;
    trend.current = buffer[currentIdx];

    size_t shortWindow = (effectiveSize < (size_t)TREND_SHORT_WINDOW)
                             ? effectiveSize
                             : (size_t)TREND_SHORT_WINDOW;
    size_t longWindow = (effectiveSize < (size_t)TREND_LONG_WINDOW)
                            ? effectiveSize
                            : (size_t)TREND_LONG_WINDOW;

    trend.shortTermAvg = calculateAverage(buffer, bufferIndex, shortWindow);
    trend.longTermAvg = calculateAverage(buffer, bufferIndex, longWindow);

    trend.slope = calculateSlope(buffer, longWindow);
    trend.volatility = calculateVolatility(buffer, longWindow);

    trend.direction = determineTrend(trend.slope, trend.volatility);
}

float TrendAnalyzer::calculateAverage(const float* buffer, size_t start, size_t window) {
    float sum = 0;
    for (size_t i = 0; i < window; i++) {
        size_t idx = (start - 1 - i + TREND_BUFFER_SIZE) % TREND_BUFFER_SIZE;
        sum += buffer[idx];
    }
    return sum / window;
}

float TrendAnalyzer::calculateSlope(const float* buffer, size_t window) {
    if (window < 2) return 0;

    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;

    for (size_t i = 0; i < window; i++) {
        size_t idx = (bufferIndex - 1 - i + TREND_BUFFER_SIZE) % TREND_BUFFER_SIZE;
        float x = i;
        float y = buffer[idx];

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }

    float n = window;
    float slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);

    return slope;
}

float TrendAnalyzer::calculateVolatility(const float* buffer, size_t window) {
    if (window < 2) return 0;

    float avg = calculateAverage(buffer, bufferIndex, window);
    float sumSquares = 0;

    for (size_t i = 0; i < window; i++) {
        size_t idx = (bufferIndex - 1 - i + TREND_BUFFER_SIZE) % TREND_BUFFER_SIZE;
        float diff = buffer[idx] - avg;
        sumSquares += diff * diff;
    }

    return sqrt(sumSquares / window);
}

TrendDirection TrendAnalyzer::determineTrend(float slope, float volatility) {
    const float STABLE_THRESHOLD = 0.01;
    const float VOLATILE_THRESHOLD = 0.5;

    if (volatility > VOLATILE_THRESHOLD) {
        return TREND_VOLATILE;
    }

    if (fabs(slope) < STABLE_THRESHOLD) {
        return TREND_STABLE;
    }

    return (slope > 0) ? TREND_INCREASING : TREND_DECREASING;
}

void TrendAnalyzer::addToBuffer(float* buffer, float value) {
    buffer[bufferIndex] = value;
}
