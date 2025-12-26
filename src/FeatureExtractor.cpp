#include "FeatureExtractor.h"
#include <math.h>

FeatureExtractor::FeatureExtractor() {
}

FeatureExtractor::~FeatureExtractor() {
}

bool FeatureExtractor::extractTimeFeatures(const float* signal, size_t length, FeatureVector& features) {
    if (signal == nullptr || length == 0) {
        DEBUG_PRINTLN("Invalid signal data");
        return false;
    }

    features.rms = calculateRMS(signal, length);
    features.peakToPeak = calculatePeakToPeak(signal, length);
    features.kurtosis = calculateKurtosis(signal, length);
    features.skewness = calculateSkewness(signal, length);
    features.crestFactor = calculateCrestFactor(signal, length);
    features.variance = calculateVariance(signal, length);
    features.timestamp = millis();

    return true;
}

bool FeatureExtractor::extractFreqFeatures(const float* spectrum, size_t length,
                                          SignalProcessor* processor, FeatureVector& features) {
    if (spectrum == nullptr || length == 0 || processor == nullptr) {
        DEBUG_PRINTLN("Invalid spectrum data");
        return false;
    }

    features.spectralCentroid = calculateSpectralCentroid(spectrum, length, processor);

    features.spectralSpread = calculateSpectralSpread(spectrum, length, processor,
                                                      features.spectralCentroid);

    float lowBandPower = processor->getBandPower(BAND_1_MIN, BAND_1_MAX);
    float highBandPower = processor->getBandPower(BAND_3_MIN, BAND_3_MAX);
    features.bandPowerRatio = (highBandPower > 0) ? (lowBandPower / highBandPower) : 0.0f;

    features.dominantFrequency = processor->getDominantFrequency();

    return true;
}

bool FeatureExtractor::extractAllFeatures(const float* signal, size_t signalLength,
                                         const float* spectrum, size_t spectrumLength,
                                         SignalProcessor* processor, FeatureVector& features) {

    if (!extractTimeFeatures(signal, signalLength, features)) {
        return false;
    }

    if (!extractFreqFeatures(spectrum, spectrumLength, processor, features)) {
        return false;
    }

    return true;
}

float FeatureExtractor::calculateRMS(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        sum += signal[i] * signal[i];
    }

    return sqrt(sum / length);
}

float FeatureExtractor::calculatePeakToPeak(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float minVal = signal[0];
    float maxVal = signal[0];

    for (size_t i = 1; i < length; i++) {
        if (signal[i] < minVal) minVal = signal[i];
        if (signal[i] > maxVal) maxVal = signal[i];
    }

    return maxVal - minVal;
}

float FeatureExtractor::calculateMean(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        sum += signal[i];
    }

    return sum / length;
}

float FeatureExtractor::calculateVariance(const float* signal, size_t length, float mean) {
    if (length == 0) return 0.0f;

    if (mean == 0.0f) {
        mean = calculateMean(signal, length);
    }

    float sum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        float diff = signal[i] - mean;
        sum += diff * diff;
    }

    return sum / length;
}

float FeatureExtractor::calculateStdDev(const float* signal, size_t length, float mean) {
    return sqrt(calculateVariance(signal, length, mean));
}

float FeatureExtractor::calculateSkewness(const float* signal, size_t length) {
    if (length < 3) return 0.0f;

    float mean = calculateMean(signal, length);
    float stdDev = calculateStdDev(signal, length, mean);

    if (stdDev == 0.0f) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        float normalized = (signal[i] - mean) / stdDev;
        sum += normalized * normalized * normalized;
    }

    return sum / length;
}

float FeatureExtractor::calculateKurtosis(const float* signal, size_t length) {
    if (length < 4) return 0.0f;

    float mean = calculateMean(signal, length);
    float stdDev = calculateStdDev(signal, length, mean);

    if (stdDev == 0.0f) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        float normalized = (signal[i] - mean) / stdDev;
        float squared = normalized * normalized;
        sum += squared * squared;
    }

    return (sum / length) - 3.0f;
}

float FeatureExtractor::calculateCrestFactor(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float rms = calculateRMS(signal, length);
    if (rms == 0.0f) return 0.0f;

    float peak = fabs(signal[0]);
    for (size_t i = 1; i < length; i++) {
        float absVal = fabs(signal[i]);
        if (absVal > peak) peak = absVal;
    }

    return peak / rms;
}

float FeatureExtractor::calculateSpectralCentroid(const float* spectrum, size_t length,
                                                 SignalProcessor* processor) {
    if (length == 0 || processor == nullptr) return 0.0f;

    float weightedSum = 0.0f;
    float totalMagnitude = 0.0f;

    for (size_t i = 0; i < length; i++) {
        float freq = processor->getFrequencyAtBin(i);
        float magnitude = spectrum[i];

        weightedSum += freq * magnitude;
        totalMagnitude += magnitude;
    }

    if (totalMagnitude == 0.0f) return 0.0f;

    return weightedSum / totalMagnitude;
}

float FeatureExtractor::calculateSpectralSpread(const float* spectrum, size_t length,
                                               SignalProcessor* processor, float centroid) {
    if (length == 0 || processor == nullptr) return 0.0f;

    if (centroid == 0.0f) {
        centroid = calculateSpectralCentroid(spectrum, length, processor);
    }

    float weightedSum = 0.0f;
    float totalMagnitude = 0.0f;

    for (size_t i = 0; i < length; i++) {
        float freq = processor->getFrequencyAtBin(i);
        float magnitude = spectrum[i];
        float diff = freq - centroid;

        weightedSum += diff * diff * magnitude;
        totalMagnitude += magnitude;
    }

    if (totalMagnitude == 0.0f) return 0.0f;

    return sqrt(weightedSum / totalMagnitude);
}
