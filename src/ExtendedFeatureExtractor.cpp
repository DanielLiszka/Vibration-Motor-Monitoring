#include "ExtendedFeatureExtractor.h"
#include <math.h>

#define MEL_FILTER_COUNT 26
#define MIN_FREQ 20.0f
#define MAX_FREQ 4000.0f

ExtendedFeatureExtractor::ExtendedFeatureExtractor()
    : sampleRate(SAMPLING_FREQUENCY_HZ)
    , melFilterbank(nullptr)
    , melFilterCount(MEL_FILTER_COUNT)
    , dctMatrix(nullptr)
    , previousSpectrum(nullptr)
    , previousSpectrumSize(0)
{
}

ExtendedFeatureExtractor::~ExtendedFeatureExtractor() {
    if (melFilterbank) delete[] melFilterbank;
    if (dctMatrix) delete[] dctMatrix;
    if (previousSpectrum) delete[] previousSpectrum;
}

bool ExtendedFeatureExtractor::begin() {
    DEBUG_PRINTLN("Initializing Extended Feature Extractor...");

    initMelFilterbank(FFT_OUTPUT_SIZE);
    initDCTMatrix(melFilterCount);

    previousSpectrum = new float[FFT_OUTPUT_SIZE];
    previousSpectrumSize = FFT_OUTPUT_SIZE;
    memset(previousSpectrum, 0, FFT_OUTPUT_SIZE * sizeof(float));

    DEBUG_PRINTLN("Extended Feature Extractor initialized");
    return true;
}

ExtendedFeatureVector ExtendedFeatureExtractor::extract(const float* signal, size_t length,
                                                         const float* spectrum, size_t spectrumLength) {
    ExtendedFeatureVector features;
    memset(&features, 0, sizeof(features));
    features.timestamp = millis();

    extractBasicFeatures(signal, length, features);
    extractSpectralFeatures(spectrum, spectrumLength, features);
    extractAdvancedFeatures(signal, length, features);

    if (spectrum && spectrumLength <= previousSpectrumSize) {
        memcpy(previousSpectrum, spectrum, spectrumLength * sizeof(float));
    }

    return features;
}

ExtendedFeatureVector ExtendedFeatureExtractor::extractFromSignalProcessor(SignalProcessor& processor, uint8_t axis) {
    const float* signal = processor.getBufferData(axis);
    size_t signalLength = processor.getBufferSize(axis);

    float spectrum[FFT_OUTPUT_SIZE];
    processor.getMagnitudeSpectrum(spectrum, FFT_OUTPUT_SIZE);

    return extract(signal, signalLength, spectrum, FFT_OUTPUT_SIZE);
}

void ExtendedFeatureExtractor::extractBasicFeatures(const float* signal, size_t length, ExtendedFeatureVector& features) {
    if (!signal || length == 0) return;

    float sum = 0.0f;
    float sumSq = 0.0f;
    float minVal = signal[0];
    float maxVal = signal[0];

    for (size_t i = 0; i < length; i++) {
        sum += signal[i];
        sumSq += signal[i] * signal[i];
        if (signal[i] < minVal) minVal = signal[i];
        if (signal[i] > maxVal) maxVal = signal[i];
    }

    float mean = sum / length;
    features.variance = (sumSq / length) - (mean * mean);
    features.rms = sqrt(sumSq / length);
    features.peakToPeak = maxVal - minVal;

    float peak = fabs(maxVal) > fabs(minVal) ? fabs(maxVal) : fabs(minVal);
    features.crestFactor = (features.rms > 0.0001f) ? (peak / features.rms) : 0.0f;

    float sum3 = 0.0f;
    float sum4 = 0.0f;
    float stddev = sqrt(features.variance);

    if (stddev > 0.0001f) {
        for (size_t i = 0; i < length; i++) {
            float normalized = (signal[i] - mean) / stddev;
            float n2 = normalized * normalized;
            sum3 += normalized * n2;
            sum4 += n2 * n2;
        }
        features.skewness = sum3 / length;
        features.kurtosis = (sum4 / length) - 3.0f;
    }

    features.zeroCrossingRate = computeZeroCrossingRate(signal, length);
    features.impulseFactor = computeImpulseFactor(signal, length);
    features.shapeFactor = computeShapeFactor(signal, length);
    features.clearanceFactor = computeClearanceFactor(signal, length);

    features.totalEnergy = sumSq;
    features.entropyValue = computeEntropy(signal, length);
    features.autocorrPeak = computeAutocorrelationPeak(signal, length);
}

void ExtendedFeatureExtractor::extractSpectralFeatures(const float* spectrum, size_t length, ExtendedFeatureVector& features) {
    if (!spectrum || length == 0) return;

    float freqResolution = sampleRate / (2.0f * length);

    float totalPower = 0.0f;
    float weightedSum = 0.0f;

    for (size_t i = 0; i < length; i++) {
        float power = spectrum[i] * spectrum[i];
        totalPower += power;
        weightedSum += i * freqResolution * power;
    }

    features.spectralCentroid = (totalPower > 0.0001f) ? (weightedSum / totalPower) : 0.0f;

    float spreadSum = 0.0f;
    for (size_t i = 0; i < length; i++) {
        float freq = i * freqResolution;
        float power = spectrum[i] * spectrum[i];
        float diff = freq - features.spectralCentroid;
        spreadSum += diff * diff * power;
    }
    features.spectralSpread = (totalPower > 0.0001f) ? sqrt(spreadSum / totalPower) : 0.0f;

    float lowBand = 0.0f;
    float highBand = 0.0f;
    size_t midBin = length / 2;
    for (size_t i = 0; i < midBin; i++) {
        lowBand += spectrum[i] * spectrum[i];
    }
    for (size_t i = midBin; i < length; i++) {
        highBand += spectrum[i] * spectrum[i];
    }
    features.bandPowerRatio = (lowBand > 0.0001f) ? (highBand / lowBand) : 0.0f;

    size_t maxBin = 1;
    float maxMag = spectrum[1];
    for (size_t i = 2; i < length; i++) {
        if (spectrum[i] > maxMag) {
            maxMag = spectrum[i];
            maxBin = i;
        }
    }
    features.dominantFrequency = maxBin * freqResolution;

    features.spectralFlux = computeSpectralFlux(spectrum, length);
    features.spectralRolloff = computeSpectralRolloff(spectrum, length, 0.85f);
    features.spectralFlatness = computeSpectralFlatness(spectrum, length);
    features.harmonicRatio = computeHarmonicContent(spectrum, length, features.dominantFrequency);

    computeMFCC(spectrum, length, features.mfcc);
}

void ExtendedFeatureExtractor::extractAdvancedFeatures(const float* signal, size_t length, ExtendedFeatureVector& features) {
    if (!signal || length == 0) return;

    computeWaveletEnergy(signal, length, features.waveletEnergy);
    computeEnvelopeSpectrum(signal, length, features.envelopeBandPower);
}

float ExtendedFeatureExtractor::computeZeroCrossingRate(const float* signal, size_t length) {
    if (length < 2) return 0.0f;

    uint32_t crossings = 0;
    for (size_t i = 1; i < length; i++) {
        if ((signal[i] >= 0 && signal[i-1] < 0) || (signal[i] < 0 && signal[i-1] >= 0)) {
            crossings++;
        }
    }
    return (float)crossings / (length - 1);
}

float ExtendedFeatureExtractor::computeSpectralFlux(const float* spectrum, size_t length) {
    if (!previousSpectrum || previousSpectrumSize != length) return 0.0f;

    float flux = 0.0f;
    for (size_t i = 0; i < length; i++) {
        float diff = spectrum[i] - previousSpectrum[i];
        flux += diff * diff;
    }
    return sqrt(flux);
}

float ExtendedFeatureExtractor::computeSpectralRolloff(const float* spectrum, size_t length, float percentile) {
    float totalEnergy = 0.0f;
    for (size_t i = 0; i < length; i++) {
        totalEnergy += spectrum[i] * spectrum[i];
    }

    float threshold = percentile * totalEnergy;
    float cumEnergy = 0.0f;
    size_t rolloffBin = 0;

    for (size_t i = 0; i < length; i++) {
        cumEnergy += spectrum[i] * spectrum[i];
        if (cumEnergy >= threshold) {
            rolloffBin = i;
            break;
        }
    }

    float freqResolution = sampleRate / (2.0f * length);
    return rolloffBin * freqResolution;
}

float ExtendedFeatureExtractor::computeSpectralFlatness(const float* spectrum, size_t length) {
    float logSum = 0.0f;
    float linearSum = 0.0f;
    size_t validBins = 0;

    for (size_t i = 1; i < length; i++) {
        if (spectrum[i] > 0.0001f) {
            logSum += log(spectrum[i]);
            linearSum += spectrum[i];
            validBins++;
        }
    }

    if (validBins == 0 || linearSum < 0.0001f) return 0.0f;

    float geometricMean = exp(logSum / validBins);
    float arithmeticMean = linearSum / validBins;

    return geometricMean / arithmeticMean;
}

float ExtendedFeatureExtractor::computeImpulseFactor(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float peak = 0.0f;
    float absSum = 0.0f;

    for (size_t i = 0; i < length; i++) {
        float absVal = fabs(signal[i]);
        if (absVal > peak) peak = absVal;
        absSum += absVal;
    }

    float meanAbs = absSum / length;
    return (meanAbs > 0.0001f) ? (peak / meanAbs) : 0.0f;
}

float ExtendedFeatureExtractor::computeShapeFactor(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float sumSq = 0.0f;
    float absSum = 0.0f;

    for (size_t i = 0; i < length; i++) {
        sumSq += signal[i] * signal[i];
        absSum += fabs(signal[i]);
    }

    float rms = sqrt(sumSq / length);
    float meanAbs = absSum / length;

    return (meanAbs > 0.0001f) ? (rms / meanAbs) : 0.0f;
}

float ExtendedFeatureExtractor::computeClearanceFactor(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float peak = 0.0f;
    float sqrtSum = 0.0f;

    for (size_t i = 0; i < length; i++) {
        float absVal = fabs(signal[i]);
        if (absVal > peak) peak = absVal;
        sqrtSum += sqrt(absVal);
    }

    float meanSqrt = sqrtSum / length;
    float sqrMeanSqrt = meanSqrt * meanSqrt;

    return (sqrMeanSqrt > 0.0001f) ? (peak / sqrMeanSqrt) : 0.0f;
}

float ExtendedFeatureExtractor::computeEntropy(const float* signal, size_t length) {
    if (length == 0) return 0.0f;

    float totalEnergy = 0.0f;
    for (size_t i = 0; i < length; i++) {
        totalEnergy += signal[i] * signal[i];
    }

    if (totalEnergy < 0.0001f) return 0.0f;

    float entropy = 0.0f;
    for (size_t i = 0; i < length; i++) {
        float prob = (signal[i] * signal[i]) / totalEnergy;
        if (prob > 0.0001f) {
            entropy -= prob * log(prob);
        }
    }

    return entropy;
}

float ExtendedFeatureExtractor::computeAutocorrelationPeak(const float* signal, size_t length) {
    if (length < 10) return 0.0f;

    size_t maxLag = length / 4;
    float maxCorr = 0.0f;
    size_t peakLag = 0;

    float energy = 0.0f;
    for (size_t i = 0; i < length; i++) {
        energy += signal[i] * signal[i];
    }

    if (energy < 0.0001f) return 0.0f;

    for (size_t lag = 1; lag < maxLag; lag++) {
        float corr = 0.0f;
        for (size_t i = 0; i < length - lag; i++) {
            corr += signal[i] * signal[i + lag];
        }
        corr /= energy;

        if (corr > maxCorr) {
            maxCorr = corr;
            peakLag = lag;
        }
    }

    return sampleRate / peakLag;
}

bool ExtendedFeatureExtractor::computeMFCC(const float* spectrum, size_t length, float* mfcc) {
    if (!spectrum || !mfcc || !melFilterbank || !dctMatrix) return false;

    float melEnergies[MEL_FILTER_COUNT];
    memset(melEnergies, 0, sizeof(melEnergies));

    size_t filterSize = length / melFilterCount;
    for (size_t f = 0; f < melFilterCount; f++) {
        size_t startBin = f * filterSize;
        size_t endBin = (f + 1) * filterSize;
        if (endBin > length) endBin = length;

        for (size_t i = startBin; i < endBin; i++) {
            melEnergies[f] += spectrum[i] * spectrum[i];
        }
        melEnergies[f] = (melEnergies[f] > 0.0001f) ? log(melEnergies[f]) : -10.0f;
    }

    for (size_t i = 0; i < MFCC_COUNT; i++) {
        mfcc[i] = 0.0f;
        for (size_t j = 0; j < melFilterCount; j++) {
            float angle = M_PI * i * (j + 0.5f) / melFilterCount;
            mfcc[i] += melEnergies[j] * cos(angle);
        }
        mfcc[i] *= sqrt(2.0f / melFilterCount);
    }

    return true;
}

bool ExtendedFeatureExtractor::computeWaveletEnergy(const float* signal, size_t length, float* energy) {
    if (!signal || !energy) return false;

    float* buffer = new float[length];
    float* detail = new float[length];

    memcpy(buffer, signal, length * sizeof(float));

    for (int level = 0; level < WAVELET_LEVELS; level++) {
        size_t levelLen = length >> level;
        if (levelLen < 2) break;

        haarWaveletDecompose(buffer, detail, levelLen, level);

        energy[level] = 0.0f;
        size_t detailLen = levelLen / 2;
        for (size_t i = 0; i < detailLen; i++) {
            energy[level] += detail[i] * detail[i];
        }
    }

    delete[] buffer;
    delete[] detail;
    return true;
}

bool ExtendedFeatureExtractor::computeEnvelopeSpectrum(const float* signal, size_t length, float* envelope) {
    if (!signal || !envelope) return false;

    float* envSignal = new float[length];
    hilbertEnvelope(signal, envSignal, length);

    size_t bandSize = length / ENVELOPE_BANDS;
    for (int band = 0; band < ENVELOPE_BANDS; band++) {
        envelope[band] = 0.0f;
        size_t startIdx = band * bandSize;
        size_t endIdx = (band + 1) * bandSize;
        if (endIdx > length) endIdx = length;

        for (size_t i = startIdx; i < endIdx; i++) {
            envelope[band] += envSignal[i] * envSignal[i];
        }
    }

    delete[] envSignal;
    return true;
}

SpectralPeaks ExtendedFeatureExtractor::findSpectralPeaks(const float* spectrum, size_t length, float minMagnitude) {
    SpectralPeaks peaks;
    peaks.count = 0;

    float freqResolution = sampleRate / (2.0f * length);

    for (size_t i = 1; i < length - 1 && peaks.count < 10; i++) {
        if (spectrum[i] > minMagnitude &&
            spectrum[i] > spectrum[i-1] &&
            spectrum[i] > spectrum[i+1]) {

            peaks.frequencies[peaks.count] = i * freqResolution;
            peaks.magnitudes[peaks.count] = spectrum[i];
            peaks.count++;
        }
    }

    for (int i = 0; i < peaks.count - 1; i++) {
        for (int j = i + 1; j < peaks.count; j++) {
            if (peaks.magnitudes[j] > peaks.magnitudes[i]) {
                float tempF = peaks.frequencies[i];
                float tempM = peaks.magnitudes[i];
                peaks.frequencies[i] = peaks.frequencies[j];
                peaks.magnitudes[i] = peaks.magnitudes[j];
                peaks.frequencies[j] = tempF;
                peaks.magnitudes[j] = tempM;
            }
        }
    }

    return peaks;
}

float ExtendedFeatureExtractor::computeHarmonicContent(const float* spectrum, size_t length, float fundamentalFreq) {
    if (fundamentalFreq < 1.0f) return 0.0f;

    float freqResolution = sampleRate / (2.0f * length);
    float harmonicPower = 0.0f;
    float totalPower = 0.0f;

    for (size_t i = 0; i < length; i++) {
        totalPower += spectrum[i] * spectrum[i];
    }

    for (int harmonic = 1; harmonic <= 10; harmonic++) {
        float targetFreq = fundamentalFreq * harmonic;
        size_t targetBin = (size_t)(targetFreq / freqResolution);

        if (targetBin >= length) break;

        int searchStart = (targetBin > 2) ? targetBin - 2 : 0;
        int searchEnd = (targetBin + 2 < length) ? targetBin + 2 : length - 1;

        for (int i = searchStart; i <= searchEnd; i++) {
            harmonicPower += spectrum[i] * spectrum[i];
        }
    }

    return (totalPower > 0.0001f) ? (harmonicPower / totalPower) : 0.0f;
}

void ExtendedFeatureExtractor::toFloatArray(const ExtendedFeatureVector& features, float* output, size_t maxSize) {
    size_t idx = 0;

    if (idx < maxSize) output[idx++] = features.rms;
    if (idx < maxSize) output[idx++] = features.peakToPeak;
    if (idx < maxSize) output[idx++] = features.kurtosis;
    if (idx < maxSize) output[idx++] = features.skewness;
    if (idx < maxSize) output[idx++] = features.crestFactor;
    if (idx < maxSize) output[idx++] = features.variance;
    if (idx < maxSize) output[idx++] = features.spectralCentroid / 100.0f;
    if (idx < maxSize) output[idx++] = features.spectralSpread / 100.0f;
    if (idx < maxSize) output[idx++] = features.bandPowerRatio;
    if (idx < maxSize) output[idx++] = features.dominantFrequency / 100.0f;

    if (idx < maxSize) output[idx++] = features.zeroCrossingRate;
    if (idx < maxSize) output[idx++] = features.spectralFlux;
    if (idx < maxSize) output[idx++] = features.spectralRolloff / 1000.0f;
    if (idx < maxSize) output[idx++] = features.spectralFlatness;
    if (idx < maxSize) output[idx++] = features.harmonicRatio;
    if (idx < maxSize) output[idx++] = features.impulseFactor;
    if (idx < maxSize) output[idx++] = features.shapeFactor;
    if (idx < maxSize) output[idx++] = features.clearanceFactor;

    for (int i = 0; i < MFCC_COUNT && idx < maxSize; i++) {
        output[idx++] = features.mfcc[i];
    }
}

FeatureVector ExtendedFeatureExtractor::toBasicFeatureVector(const ExtendedFeatureVector& extended) {
    FeatureVector basic;
    basic.rms = extended.rms;
    basic.peakToPeak = extended.peakToPeak;
    basic.kurtosis = extended.kurtosis;
    basic.skewness = extended.skewness;
    basic.crestFactor = extended.crestFactor;
    basic.variance = extended.variance;
    basic.spectralCentroid = extended.spectralCentroid;
    basic.spectralSpread = extended.spectralSpread;
    basic.bandPowerRatio = extended.bandPowerRatio;
    basic.dominantFrequency = extended.dominantFrequency;
    basic.timestamp = extended.timestamp;
    return basic;
}

void ExtendedFeatureExtractor::initMelFilterbank(size_t fftSize) {
    melFilterbank = new float[melFilterCount * fftSize];

    float melMin = hzToMel(MIN_FREQ);
    float melMax = hzToMel(MAX_FREQ);
    float melStep = (melMax - melMin) / (melFilterCount + 1);

    float* melPoints = new float[melFilterCount + 2];
    for (size_t i = 0; i < melFilterCount + 2; i++) {
        melPoints[i] = melToHz(melMin + i * melStep);
    }

    float freqRes = sampleRate / (2.0f * fftSize);
    for (size_t f = 0; f < melFilterCount; f++) {
        float lower = melPoints[f];
        float center = melPoints[f + 1];
        float upper = melPoints[f + 2];

        for (size_t bin = 0; bin < fftSize; bin++) {
            float freq = bin * freqRes;
            size_t idx = f * fftSize + bin;

            if (freq >= lower && freq < center) {
                melFilterbank[idx] = (freq - lower) / (center - lower);
            } else if (freq >= center && freq < upper) {
                melFilterbank[idx] = (upper - freq) / (upper - center);
            } else {
                melFilterbank[idx] = 0.0f;
            }
        }
    }

    delete[] melPoints;
}

float ExtendedFeatureExtractor::hzToMel(float hz) {
    return 2595.0f * log10(1.0f + hz / 700.0f);
}

float ExtendedFeatureExtractor::melToHz(float mel) {
    return 700.0f * (pow(10.0f, mel / 2595.0f) - 1.0f);
}

void ExtendedFeatureExtractor::initDCTMatrix(size_t size) {
    dctMatrix = new float[MFCC_COUNT * size];

    for (size_t i = 0; i < MFCC_COUNT; i++) {
        for (size_t j = 0; j < size; j++) {
            dctMatrix[i * size + j] = cos(M_PI * i * (j + 0.5f) / size);
        }
    }
}

void ExtendedFeatureExtractor::haarWaveletDecompose(const float* signal, float* output, size_t length, int level) {
    size_t halfLen = length / 2;
    float invSqrt2 = 1.0f / sqrt(2.0f);

    for (size_t i = 0; i < halfLen; i++) {
        output[i] = (signal[2*i] - signal[2*i + 1]) * invSqrt2;
    }
}

void ExtendedFeatureExtractor::hilbertEnvelope(const float* signal, float* envelope, size_t length) {
    for (size_t i = 0; i < length; i++) {
        float sum = 0.0f;
        for (size_t k = 0; k < length; k++) {
            if (k != i) {
                sum += signal[k] / (M_PI * (float)(i - k));
            }
        }
        envelope[i] = sqrt(signal[i] * signal[i] + sum * sum);
    }
}
