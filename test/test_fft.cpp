#include <Arduino.h>
#include "SignalProcessor.h"
#include "Config.h"

SignalProcessor processor;

void generateSineWave(float* buffer, size_t length, float frequency, float amplitude, float sampleRate) {
    for (size_t i = 0; i < length; i++) {
        buffer[i] = amplitude * sin(2.0 * PI * frequency * i / sampleRate);
    }
}

void generateMixedSignal(float* buffer, size_t length, float sampleRate) {
     
    for (size_t i = 0; i < length; i++) {
        buffer[i] = 2.0 * sin(2.0 * PI * 5.0 * i / sampleRate) +     
                   1.5 * sin(2.0 * PI * 15.0 * i / sampleRate) +    
                   1.0 * sin(2.0 * PI * 35.0 * i / sampleRate);     
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║       FFT Algorithm Test Suite        ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    if (!processor.begin()) {
        Serial.println("✗ Failed to initialize signal processor");
        while(1) { delay(1000); }
    }

    Serial.println("Test 1: Single Frequency Detection");
    Serial.println("──────────────────────────────────");
    testSingleFrequency(10.0, 2.0);  

    Serial.println("\nTest 2: Multiple Frequencies");
    Serial.println("──────────────────────────────────");
    testMixedFrequencies();

    Serial.println("\nTest 3: Performance Benchmark");
    Serial.println("──────────────────────────────────");
    benchmarkFFT();

    Serial.println("\n✓ All tests complete!");
}

void testSingleFrequency(float freq, float amplitude) {
    Serial.printf("Generating %.1f Hz sine wave with amplitude %.1f\n", freq, amplitude);

    float testSignal[WINDOW_SIZE];
    generateSineWave(testSignal, WINDOW_SIZE, freq, amplitude, SAMPLING_FREQUENCY_HZ);

    for (size_t i = 0; i < WINDOW_SIZE; i++) {
        processor.addSample(testSignal[i], 0);
    }

    uint32_t startTime = micros();
    processor.performFFT(0);
    uint32_t fftTime = micros() - startTime;

    float dominantFreq = processor.getDominantFrequency();

    Serial.printf("Expected frequency: %.1f Hz\n", freq);
    Serial.printf("Detected frequency: %.1f Hz\n", dominantFreq);
    Serial.printf("Error: %.2f%%\n", abs(dominantFreq - freq) / freq * 100.0);
    Serial.printf("FFT computation time: %lu µs\n", fftTime);

    if (abs(dominantFreq - freq) < 1.0) {
        Serial.println("✓ PASS - Frequency detected accurately");
    } else {
        Serial.println("✗ FAIL - Frequency detection error too large");
    }

    processor.reset();
}

void testMixedFrequencies() {
    Serial.println("Generating mixed signal (5Hz + 15Hz + 35Hz)");

    float testSignal[WINDOW_SIZE];
    generateMixedSignal(testSignal, WINDOW_SIZE, SAMPLING_FREQUENCY_HZ);

    for (size_t i = 0; i < WINDOW_SIZE; i++) {
        processor.addSample(testSignal[i], 0);
    }

    processor.performFFT(0);

    float spectrum[FFT_OUTPUT_SIZE];
    processor.getMagnitudeSpectrum(spectrum, FFT_OUTPUT_SIZE);

    Serial.println("\nSpectral peaks:");
    for (size_t i = 1; i < FFT_OUTPUT_SIZE - 1; i++) {
         
        if (spectrum[i] > spectrum[i-1] && spectrum[i] > spectrum[i+1] && spectrum[i] > 10.0) {
            float freq = processor.getFrequencyAtBin(i);
            Serial.printf("  Peak at %.1f Hz (magnitude: %.2f)\n", freq, spectrum[i]);
        }
    }

    float lowBand = processor.getBandPower(0, 10);
    float midBand = processor.getBandPower(10, 30);
    float highBand = processor.getBandPower(30, 50);

    Serial.printf("\nBand Power Analysis:\n");
    Serial.printf("  Low (0-10 Hz): %.2f\n", lowBand);
    Serial.printf("  Mid (10-30 Hz): %.2f\n", midBand);
    Serial.printf("  High (30-50 Hz): %.2f\n", highBand);

    processor.reset();
}

void benchmarkFFT() {
    const int iterations = 100;
    uint32_t totalTime = 0;

    Serial.printf("Running %d FFT operations...\n", iterations);

    float testSignal[WINDOW_SIZE];
    generateSineWave(testSignal, WINDOW_SIZE, 20.0, 1.0, SAMPLING_FREQUENCY_HZ);

    for (int i = 0; i < iterations; i++) {
         
        processor.reset();
        for (size_t j = 0; j < WINDOW_SIZE; j++) {
            processor.addSample(testSignal[j], 0);
        }

        uint32_t start = micros();
        processor.performFFT(0);
        uint32_t elapsed = micros() - start;

        totalTime += elapsed;
    }

    Serial.printf("\nBenchmark Results:\n");
    Serial.printf("  Total time: %lu µs\n", totalTime);
    Serial.printf("  Average time: %lu µs\n", totalTime / iterations);
    Serial.printf("  Max throughput: %.2f FFT/sec\n", 1000000.0 / (totalTime / (float)iterations));

    float windowDuration = (WINDOW_SIZE * 1000.0) / SAMPLING_FREQUENCY_HZ;  
    float fftDuration = (totalTime / iterations) / 1000.0;  
    Serial.printf("  Window duration: %.2f ms\n", windowDuration);
    Serial.printf("  FFT duration: %.2f ms\n", fftDuration);
    Serial.printf("  CPU usage: %.1f%%\n", (fftDuration / windowDuration) * 100.0);

    if (fftDuration < windowDuration) {
        Serial.println("  ✓ Real-time processing capable");
    } else {
        Serial.println("  ✗ Cannot keep up with real-time");
    }
}

void loop() {
     
    delay(10000);
}
