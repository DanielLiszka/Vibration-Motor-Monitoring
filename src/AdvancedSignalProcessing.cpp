#include "AdvancedSignalProcessing.h"
#include <math.h>

void AdvancedSignalProcessing::hilbertTransform(const float* input, float* output, size_t length) {
    for (size_t i = 0; i < length; i++) {
        output[i] = 0;
        for (size_t j = 0; j < length; j++) {
            if (i != j) {
                output[i] += input[j] / (M_PI * (i - j));
            }
        }
    }
}

void AdvancedSignalProcessing::envelopeDetection(const float* signal, float* envelope, size_t length) {
    for (size_t i = 0; i < length; i++) {
        envelope[i] = fabs(signal[i]);
    }

    applyLowPassFilter(envelope, length, 10.0f, SAMPLING_FREQUENCY_HZ);
}

float AdvancedSignalProcessing::calculateCepstrum(const float* signal, float* cepstrum, size_t length) {
    for (size_t i = 0; i < length; i++) {
        cepstrum[i] = log(fabs(signal[i]) + 1e-10);
    }

    return 0;
}

void AdvancedSignalProcessing::detrend(float* signal, size_t length) {
    float mean = 0;
    for (size_t i = 0; i < length; i++) {
        mean += signal[i];
    }
    mean /= length;

    float slope = 0;
    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;

    for (size_t i = 0; i < length; i++) {
        float x = i;
        float y = signal[i];
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }

    slope = (length * sumXY - sumX * sumY) / (length * sumX2 - sumX * sumX);
    float intercept = (sumY - slope * sumX) / length;

    for (size_t i = 0; i < length; i++) {
        signal[i] -= (slope * i + intercept);
    }
}

void AdvancedSignalProcessing::applyHighPassFilter(float* signal, size_t length,
                                                   float cutoffFreq, float sampleRate) {
    float RC = 1.0 / (2.0 * M_PI * cutoffFreq);
    float dt = 1.0 / sampleRate;
    float alpha = RC / (RC + dt);

    float* filtered = new float[length];
    filtered[0] = signal[0];

    for (size_t i = 1; i < length; i++) {
        filtered[i] = alpha * (filtered[i-1] + signal[i] - signal[i-1]);
    }

    memcpy(signal, filtered, length * sizeof(float));
    delete[] filtered;
}

void AdvancedSignalProcessing::applyLowPassFilter(float* signal, size_t length,
                                                  float cutoffFreq, float sampleRate) {
    float RC = 1.0 / (2.0 * M_PI * cutoffFreq);
    float dt = 1.0 / sampleRate;
    float alpha = dt / (RC + dt);

    for (size_t i = 1; i < length; i++) {
        signal[i] = signal[i-1] + alpha * (signal[i] - signal[i-1]);
    }
}

void AdvancedSignalProcessing::applyBandPassFilter(float* signal, size_t length,
                                                   float lowCutoff, float highCutoff,
                                                   float sampleRate) {
    applyHighPassFilter(signal, length, lowCutoff, sampleRate);
    applyLowPassFilter(signal, length, highCutoff, sampleRate);
}

float AdvancedSignalProcessing::calculateCoherence(const float* signal1,
                                                   const float* signal2,
                                                   size_t length) {
    float correlation = 0;
    float energy1 = 0, energy2 = 0;

    for (size_t i = 0; i < length; i++) {
        correlation += signal1[i] * signal2[i];
        energy1 += signal1[i] * signal1[i];
        energy2 += signal2[i] * signal2[i];
    }

    float denominator = sqrt(energy1 * energy2);
    return (denominator != 0) ? (correlation / denominator) : 0;
}

void AdvancedSignalProcessing::autocorrelation(const float* signal, float* result, size_t length) {
    for (size_t lag = 0; lag < length; lag++) {
        result[lag] = 0;
        for (size_t i = 0; i < length - lag; i++) {
            result[lag] += signal[i] * signal[i + lag];
        }
        result[lag] /= (length - lag);
    }
}

void AdvancedSignalProcessing::medianFilter(float* signal, size_t length, size_t windowSize) {
    if (windowSize % 2 == 0) windowSize++;

    size_t halfWindow = windowSize / 2;
    float* filtered = new float[length];
    float* window = new float[windowSize];

    for (size_t i = 0; i < length; i++) {
        size_t count = 0;
        for (size_t j = 0; j < windowSize; j++) {
            int idx = i - halfWindow + j;
            if (idx >= 0 && idx < (int)length) {
                window[count++] = signal[idx];
            }
        }

        for (size_t j = 0; j < count - 1; j++) {
            for (size_t k = 0; k < count - j - 1; k++) {
                if (window[k] > window[k + 1]) {
                    float temp = window[k];
                    window[k] = window[k + 1];
                    window[k + 1] = temp;
                }
            }
        }

        filtered[i] = window[count / 2];
    }

    memcpy(signal, filtered, length * sizeof(float));
    delete[] filtered;
    delete[] window;
}

void AdvancedSignalProcessing::kalmanFilter(float* signal, size_t length,
                                           float processNoise, float measurementNoise) {
    float estimate = signal[0];
    float errorCovariance = 1.0;

    for (size_t i = 0; i < length; i++) {
        float prediction = estimate;
        float predictionError = errorCovariance + processNoise;

        float kalmanGain = predictionError / (predictionError + measurementNoise);

        estimate = prediction + kalmanGain * (signal[i] - prediction);
        errorCovariance = (1 - kalmanGain) * predictionError;

        signal[i] = estimate;
    }
}

void AdvancedSignalProcessing::waveletDecompose(const float* signal, float* approximation,
                                               float* detail, size_t length) {
    for (size_t i = 0; i < length / 2; i++) {
        approximation[i] = (signal[2*i] + signal[2*i + 1]) / sqrt(2.0);
        detail[i] = (signal[2*i] - signal[2*i + 1]) / sqrt(2.0);
    }
}

float AdvancedSignalProcessing::calculateSNR(const float* signal,
                                            size_t signalStart, size_t signalEnd,
                                            size_t noiseStart, size_t noiseEnd) {
    float signalPower = 0, noisePower = 0;

    for (size_t i = signalStart; i < signalEnd; i++) {
        signalPower += signal[i] * signal[i];
    }
    signalPower /= (signalEnd - signalStart);

    for (size_t i = noiseStart; i < noiseEnd; i++) {
        noisePower += signal[i] * signal[i];
    }
    noisePower /= (noiseEnd - noiseStart);

    return (noisePower != 0) ? (10 * log10(signalPower / noisePower)) : 0;
}

void AdvancedSignalProcessing::convolve(const float* signal, const float* kernel,
                                       float* output, size_t signalLen, size_t kernelLen) {
    for (size_t i = 0; i < signalLen; i++) {
        output[i] = 0;
        for (size_t j = 0; j < kernelLen; j++) {
            if (i >= j) {
                output[i] += signal[i - j] * kernel[j];
            }
        }
    }
}
