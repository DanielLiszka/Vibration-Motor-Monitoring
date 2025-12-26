#ifndef SIGNAL_PROCESSOR_H
#define SIGNAL_PROCESSOR_H

#include <Arduino.h>
#include "Config.h"
#include "MPU6050Driver.h"

struct Complex {
    float real;
    float imag;

    Complex() : real(0), imag(0) {}
    Complex(float r, float i) : real(r), imag(i) {}

    float magnitude() const {
        return sqrt(real * real + imag * imag);
    }
};

class SignalBuffer {
public:
    SignalBuffer(size_t size);
    ~SignalBuffer();

    bool push(float value);
    bool isFull() const { return count >= size; }
    void clear();
    void discardPrefix(size_t n);

    float* getData() { return buffer; }
    const float* getData() const { return buffer; }
    size_t getSize() const { return size; }
    size_t getCount() const { return count; }

private:
    float* buffer;
    size_t size;
    size_t count;
};

class SignalProcessor {
public:

    SignalProcessor();

    ~SignalProcessor();

    bool begin();

    bool addSample(float value, uint8_t axis = 0);

    bool performFFT(uint8_t axis = 0);

    bool getMagnitudeSpectrum(float* spectrum, size_t size);

    bool getPowerSpectrum(float* spectrum, size_t size);

    float getDominantFrequency();

    float getFrequencyAtBin(size_t bin);

    float getBandPower(float minFreq, float maxFreq);

    void applyWindow(uint8_t windowType = 0);

    void reset();

    void advanceWindow(uint8_t axis = 0);
    size_t getHopSize() const { return hopSize; }

    uint8_t getBufferFillLevel() const;

    const float* getBufferData(uint8_t axis = 0) const;
    float* getBufferData(uint8_t axis = 0);
    size_t getBufferSize(uint8_t axis = 0) const;

private:
    SignalBuffer* buffers[3];
    Complex* fftOutput;
    float* magnitudeSpectrum;
    float* windowScratch;
    float* hanningCoefficients;
    size_t hopSize;
    bool fftReady;

    void fft(const float* input, Complex* output, size_t n);

    void bitReverse(Complex* data, size_t n);

    void hanningWindow(float* data, size_t n);

    void hammingWindow(float* data, size_t n);

    void blackmanWindow(float* data, size_t n);

    size_t nextPowerOfTwo(size_t n);
};

#endif
