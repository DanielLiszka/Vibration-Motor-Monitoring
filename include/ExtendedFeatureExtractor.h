#ifndef EXTENDED_FEATURE_EXTRACTOR_H
#define EXTENDED_FEATURE_EXTRACTOR_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "SignalProcessor.h"

#define EXT_FEATURE_COUNT 24
#define MFCC_COUNT 13
#define WAVELET_LEVELS 4
#define ENVELOPE_BANDS 5

struct ExtendedFeatureVector {
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

    float zeroCrossingRate;
    float spectralFlux;
    float spectralRolloff;
    float spectralFlatness;
    float harmonicRatio;
    float impulseFactor;
    float shapeFactor;
    float clearanceFactor;

    float mfcc[MFCC_COUNT];
    float waveletEnergy[WAVELET_LEVELS];
    float envelopeBandPower[ENVELOPE_BANDS];

    float totalEnergy;
    float entropyValue;
    float autocorrPeak;

    uint32_t timestamp;
};

struct SpectralPeaks {
    float frequencies[10];
    float magnitudes[10];
    uint8_t count;
};

class ExtendedFeatureExtractor {
public:
    ExtendedFeatureExtractor();
    ~ExtendedFeatureExtractor();

    bool begin();
    ExtendedFeatureVector extract(const float* signal, size_t length, const float* spectrum, size_t spectrumLength);
    ExtendedFeatureVector extractFromSignalProcessor(SignalProcessor& processor, uint8_t axis = 0);

    void extractBasicFeatures(const float* signal, size_t length, ExtendedFeatureVector& features);
    void extractSpectralFeatures(const float* spectrum, size_t length, ExtendedFeatureVector& features);
    void extractAdvancedFeatures(const float* signal, size_t length, ExtendedFeatureVector& features);

    bool computeMFCC(const float* spectrum, size_t length, float* mfcc);
    bool computeWaveletEnergy(const float* signal, size_t length, float* energy);
    bool computeEnvelopeSpectrum(const float* signal, size_t length, float* envelope);

    SpectralPeaks findSpectralPeaks(const float* spectrum, size_t length, float minMagnitude = 0.1f);
    float computeHarmonicContent(const float* spectrum, size_t length, float fundamentalFreq);

    void toFloatArray(const ExtendedFeatureVector& features, float* output, size_t maxSize);
    FeatureVector toBasicFeatureVector(const ExtendedFeatureVector& extended);

private:
    float sampleRate;

    float* melFilterbank;
    size_t melFilterCount;
    float* dctMatrix;

    float computeZeroCrossingRate(const float* signal, size_t length);
    float computeSpectralFlux(const float* spectrum, size_t length);
    float computeSpectralRolloff(const float* spectrum, size_t length, float percentile = 0.85f);
    float computeSpectralFlatness(const float* spectrum, size_t length);
    float computeImpulseFactor(const float* signal, size_t length);
    float computeShapeFactor(const float* signal, size_t length);
    float computeClearanceFactor(const float* signal, size_t length);
    float computeEntropy(const float* signal, size_t length);
    float computeAutocorrelationPeak(const float* signal, size_t length);

    void initMelFilterbank(size_t fftSize);
    float hzToMel(float hz);
    float melToHz(float mel);
    void initDCTMatrix(size_t size);

    void haarWaveletDecompose(const float* signal, float* output, size_t length, int level);
    void hilbertEnvelope(const float* signal, float* envelope, size_t length);

    float* previousSpectrum;
    size_t previousSpectrumSize;
};

#endif
