#include "SignalProcessor.h"
#include <math.h>

SignalBuffer::SignalBuffer(size_t size)
    : size(size)
    , count(0)
{
    buffer = new float[size];
    clear();
}

SignalBuffer::~SignalBuffer() {
    delete[] buffer;
}

bool SignalBuffer::push(float value) {
    if (count < size) {
        buffer[count++] = value;
        return (count == size);  
    }
    return false;
}

void SignalBuffer::clear() {
    count = 0;
    memset(buffer, 0, size * sizeof(float));
}

SignalProcessor::SignalProcessor()
    : fftReady(false)
{
     
    for (int i = 0; i < 3; i++) {
        buffers[i] = new SignalBuffer(WINDOW_SIZE);
    }

    fftOutput = new Complex[FFT_SIZE];
    magnitudeSpectrum = new float[FFT_OUTPUT_SIZE];
}

SignalProcessor::~SignalProcessor() {
    for (int i = 0; i < 3; i++) {
        delete buffers[i];
    }
    delete[] fftOutput;
    delete[] magnitudeSpectrum;
}

bool SignalProcessor::begin() {
    DEBUG_PRINTLN("Initializing Signal Processor...");
    reset();
    DEBUG_PRINTLN("Signal Processor initialized");
    return true;
}

bool SignalProcessor::addSample(float value, uint8_t axis) {
    if (axis >= 3) {
        DEBUG_PRINTLN("Invalid axis");
        return false;
    }

    return buffers[axis]->push(value);
}

bool SignalProcessor::performFFT(uint8_t axis) {
    if (axis >= 3) {
        DEBUG_PRINTLN("Invalid axis");
        return false;
    }

    if (!buffers[axis]->isFull()) {
        DEBUG_PRINTLN("Buffer not full, cannot perform FFT");
        return false;
    }

    float* inputData = buffers[axis]->getData();
    size_t dataSize = buffers[axis]->getSize();

    float* windowedData = new float[dataSize];
    memcpy(windowedData, inputData, dataSize * sizeof(float));
    hanningWindow(windowedData, dataSize);

    fft(windowedData, fftOutput, dataSize);

    for (size_t i = 0; i < FFT_OUTPUT_SIZE; i++) {
        magnitudeSpectrum[i] = fftOutput[i].magnitude();
    }

    delete[] windowedData;

    fftReady = true;
    return true;
}

bool SignalProcessor::getMagnitudeSpectrum(float* spectrum, size_t size) {
    if (!fftReady) {
        DEBUG_PRINTLN("FFT not ready");
        return false;
    }

    if (size != FFT_OUTPUT_SIZE) {
        DEBUG_PRINTLN("Invalid spectrum size");
        return false;
    }

    memcpy(spectrum, magnitudeSpectrum, size * sizeof(float));
    return true;
}

bool SignalProcessor::getPowerSpectrum(float* spectrum, size_t size) {
    if (!fftReady) {
        DEBUG_PRINTLN("FFT not ready");
        return false;
    }

    if (size != FFT_OUTPUT_SIZE) {
        DEBUG_PRINTLN("Invalid spectrum size");
        return false;
    }

    for (size_t i = 0; i < size; i++) {
        spectrum[i] = magnitudeSpectrum[i] * magnitudeSpectrum[i];
    }

    return true;
}

float SignalProcessor::getDominantFrequency() {
    if (!fftReady) {
        return 0.0f;
    }

    size_t maxBin = 1;
    float maxMagnitude = magnitudeSpectrum[1];

    for (size_t i = 2; i < FFT_OUTPUT_SIZE; i++) {
        if (magnitudeSpectrum[i] > maxMagnitude) {
            maxMagnitude = magnitudeSpectrum[i];
            maxBin = i;
        }
    }

    return getFrequencyAtBin(maxBin);
}

float SignalProcessor::getFrequencyAtBin(size_t bin) {
     
    float freqResolution = (float)SAMPLING_FREQUENCY_HZ / (float)FFT_SIZE;
    return bin * freqResolution;
}

float SignalProcessor::getBandPower(float minFreq, float maxFreq) {
    if (!fftReady) {
        return 0.0f;
    }

    float freqResolution = (float)SAMPLING_FREQUENCY_HZ / (float)FFT_SIZE;
    size_t minBin = (size_t)(minFreq / freqResolution);
    size_t maxBin = (size_t)(maxFreq / freqResolution);

    if (minBin >= FFT_OUTPUT_SIZE) minBin = FFT_OUTPUT_SIZE - 1;
    if (maxBin >= FFT_OUTPUT_SIZE) maxBin = FFT_OUTPUT_SIZE - 1;
    if (minBin > maxBin) {
        size_t temp = minBin;
        minBin = maxBin;
        maxBin = temp;
    }

    float bandPower = 0.0f;
    for (size_t i = minBin; i <= maxBin; i++) {
        bandPower += magnitudeSpectrum[i] * magnitudeSpectrum[i];
    }

    return bandPower;
}

void SignalProcessor::applyWindow(uint8_t windowType) {
    float* data = buffers[0]->getData();
    size_t n = buffers[0]->getSize();

    switch (windowType) {
        case 0:
            hanningWindow(data, n);
            break;
        case 1:
            hammingWindow(data, n);
            break;
        case 2:
            blackmanWindow(data, n);
            break;
        default:
            hanningWindow(data, n);
            break;
    }
}

void SignalProcessor::reset() {
    for (int i = 0; i < 3; i++) {
        buffers[i]->clear();
    }
    fftReady = false;
}

uint8_t SignalProcessor::getBufferFillLevel() const {
     
    return (uint8_t)((buffers[0]->getCount() * 100) / buffers[0]->getSize());
}

const float* SignalProcessor::getBufferData(uint8_t axis) const {
    if (axis >= 3) {
        return nullptr;
    }
    return buffers[axis]->getData();
}

float* SignalProcessor::getBufferData(uint8_t axis) {
    if (axis >= 3) {
        return nullptr;
    }
    return buffers[axis]->getData();
}

size_t SignalProcessor::getBufferSize(uint8_t axis) const {
    if (axis >= 3) {
        return 0;
    }
    return buffers[axis]->getSize();
}

void SignalProcessor::fft(const float* input, Complex* output, size_t n) {
     
    for (size_t i = 0; i < n; i++) {
        output[i].real = input[i];
        output[i].imag = 0.0f;
    }

    bitReverse(output, n);

    for (size_t s = 1; s <= log2(n); s++) {
        size_t m = 1 << s;  
        size_t m2 = m >> 1;  

        Complex w(1.0f, 0.0f);
        float angle = -M_PI / m2;
        Complex wm(cos(angle), sin(angle));

        for (size_t j = 0; j < m2; j++) {
            for (size_t k = j; k < n; k += m) {
                 
                size_t t_idx = k + m2;

                Complex t(
                    output[t_idx].real * w.real - output[t_idx].imag * w.imag,
                    output[t_idx].real * w.imag + output[t_idx].imag * w.real
                );

                Complex u = output[k];

                output[k].real = u.real + t.real;
                output[k].imag = u.imag + t.imag;

                output[t_idx].real = u.real - t.real;
                output[t_idx].imag = u.imag - t.imag;
            }

            Complex temp = w;
            w.real = temp.real * wm.real - temp.imag * wm.imag;
            w.imag = temp.real * wm.imag + temp.imag * wm.real;
        }
    }
}

void SignalProcessor::bitReverse(Complex* data, size_t n) {
    size_t j = 0;
    for (size_t i = 0; i < n - 1; i++) {
        if (i < j) {
             
            Complex temp = data[i];
            data[i] = data[j];
            data[j] = temp;
        }

        size_t k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

void SignalProcessor::hanningWindow(float* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (n - 1)));
        data[i] *= window;
    }
}

void SignalProcessor::hammingWindow(float* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float window = 0.54f - 0.46f * cos(2.0f * M_PI * i / (n - 1));
        data[i] *= window;
    }
}

void SignalProcessor::blackmanWindow(float* data, size_t n) {
    const float a0 = 0.42f;
    const float a1 = 0.5f;
    const float a2 = 0.08f;

    for (size_t i = 0; i < n; i++) {
        float window = a0
                     - a1 * cos(2.0f * M_PI * i / (n - 1))
                     + a2 * cos(4.0f * M_PI * i / (n - 1));
        data[i] *= window;
    }
}

size_t SignalProcessor::nextPowerOfTwo(size_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}
