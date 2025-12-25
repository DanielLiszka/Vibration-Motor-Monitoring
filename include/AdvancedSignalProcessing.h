#ifndef ADVANCED_SIGNAL_PROCESSING_H
#define ADVANCED_SIGNAL_PROCESSING_H

#include <Arduino.h>
#include "Config.h"

class AdvancedSignalProcessing {
public:
    static void hilbertTransform(const float* input, float* output, size_t length);

    static void envelopeDetection(const float* signal, float* envelope, size_t length);

    static float calculateCepstrum(const float* signal, float* cepstrum, size_t length);

    static void detrend(float* signal, size_t length);

    static void applyHighPassFilter(float* signal, size_t length,
                                   float cutoffFreq, float sampleRate);

    static void applyLowPassFilter(float* signal, size_t length,
                                  float cutoffFreq, float sampleRate);

    static void applyBandPassFilter(float* signal, size_t length,
                                   float lowCutoff, float highCutoff, float sampleRate);

    static float calculateCoherence(const float* signal1, const float* signal2,
                                   size_t length);

    static void autocorrelation(const float* signal, float* result, size_t length);

    static void medianFilter(float* signal, size_t length, size_t windowSize);

    static void kalmanFilter(float* signal, size_t length,
                            float processNoise, float measurementNoise);

    static void waveletDecompose(const float* signal, float* approximation,
                                float* detail, size_t length);

    static float calculateSNR(const float* signal, size_t signalStart, size_t signalEnd,
                             size_t noiseStart, size_t noiseEnd);

private:
    static void convolve(const float* signal, const float* kernel,
                        float* output, size_t signalLen, size_t kernelLen);
};

#endif
