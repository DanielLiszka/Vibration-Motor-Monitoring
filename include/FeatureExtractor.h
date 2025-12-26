#ifndef FEATURE_EXTRACTOR_H
#define FEATURE_EXTRACTOR_H

#include <Arduino.h>
#include "Config.h"
#include "SignalProcessor.h"

struct FeatureVector {

    float rms;
    float peakToPeak;
    float kurtosis;
    float skewness;
    float crestFactor;
    float variance;

    float spectralCentroid;
    float spectralSpread;
    float bandPowerRatio;
    float dominantFrequency;

    uint32_t timestamp;

    void reset() {
        rms = peakToPeak = kurtosis = skewness = 0.0f;
        crestFactor = variance = spectralCentroid = 0.0f;
        spectralSpread = bandPowerRatio = dominantFrequency = 0.0f;
        timestamp = 0;
    }

    void toArray(float* arr) const {
        arr[0] = rms;
        arr[1] = peakToPeak;
        arr[2] = kurtosis;
        arr[3] = skewness;
        arr[4] = crestFactor;
        arr[5] = variance;
        arr[6] = spectralCentroid;
        arr[7] = spectralSpread;
        arr[8] = bandPowerRatio;
        arr[9] = dominantFrequency;
    }

    void print() const {
        Serial.println("=== Feature Vector ===");
        Serial.printf("RMS: %.4f\n", rms);
        Serial.printf("Peak-to-Peak: %.4f\n", peakToPeak);
        Serial.printf("Kurtosis: %.4f\n", kurtosis);
        Serial.printf("Skewness: %.4f\n", skewness);
        Serial.printf("Crest Factor: %.4f\n", crestFactor);
        Serial.printf("Variance: %.4f\n", variance);
        Serial.printf("Spectral Centroid: %.2f Hz\n", spectralCentroid);
        Serial.printf("Spectral Spread: %.2f\n", spectralSpread);
        Serial.printf("Band Power Ratio: %.4f\n", bandPowerRatio);
        Serial.printf("Dominant Freq: %.2f Hz\n", dominantFrequency);
    }
};

class FeatureExtractor {
public:

    FeatureExtractor();

    ~FeatureExtractor();

    bool extractTimeFeatures(const float* signal, size_t length, FeatureVector& features);

    bool extractFreqFeatures(const float* spectrum, size_t length,
                            SignalProcessor* processor, FeatureVector& features);

    bool extractAllFeatures(const float* signal, size_t signalLength,
                           const float* spectrum, size_t spectrumLength,
                           SignalProcessor* processor, FeatureVector& features);

    static float calculateRMS(const float* signal, size_t length);

    static float calculatePeakToPeak(const float* signal, size_t length);

    static float calculateMean(const float* signal, size_t length);

    static float calculateVariance(const float* signal, size_t length, float mean = 0.0f);

    static float calculateStdDev(const float* signal, size_t length, float mean = 0.0f);

    static float calculateSkewness(const float* signal, size_t length);

    static float calculateKurtosis(const float* signal, size_t length);

    static float calculateCrestFactor(const float* signal, size_t length);

    static float calculateSpectralCentroid(const float* spectrum, size_t length,
                                          SignalProcessor* processor);

    static float calculateSpectralSpread(const float* spectrum, size_t length,
                                        SignalProcessor* processor, float centroid);

private:

};

#endif
