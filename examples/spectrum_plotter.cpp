#include <Arduino.h>
#include "MPU6050Driver.h"
#include "SignalProcessor.h"
#include "Config.h"

MPU6050Driver sensor;
SignalProcessor processor;

#define PLOT_BINS 64
#define PLOT_INTERVAL_MS 100
#define PLOT_SMOOTHING 0.3

float smoothedSpectrum[PLOT_BINS] = {0};

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("Initializing Spectrum Analyzer...");

    if (!sensor.begin()) {
        Serial.println("Sensor initialization failed!");
        while(1) delay(1000);
    }

    if (!processor.begin()) {
        Serial.println("Processor initialization failed!");
        while(1) delay(1000);
    }

    Serial.println("Ready! Open Serial Plotter for visualization.");
    delay(1000);
}

void loop() {
    static uint32_t lastPlot = 0;

    AccelData accel;
    if (sensor.readAcceleration(accel)) {
        float magnitude = sqrt(accel.x * accel.x +
                              accel.y * accel.y +
                              accel.z * accel.z);

        if (processor.addSample(magnitude, 0)) {

            processor.performFFT(0);

            if (millis() - lastPlot >= PLOT_INTERVAL_MS) {
                plotSpectrum();
                lastPlot = millis();
            }

            processor.reset();
        }
    }

    delay(SAMPLING_PERIOD_MS);
}

void plotSpectrum() {
    float spectrum[FFT_OUTPUT_SIZE];
    processor.getMagnitudeSpectrum(spectrum, FFT_OUTPUT_SIZE);

    int binSize = FFT_OUTPUT_SIZE / PLOT_BINS;

    for (int i = 0; i < PLOT_BINS; i++) {

        float avg = 0;
        for (int j = 0; j < binSize; j++) {
            int idx = i * binSize + j;
            if (idx < FFT_OUTPUT_SIZE) {
                avg += spectrum[idx];
            }
        }
        avg /= binSize;

        smoothedSpectrum[i] = smoothedSpectrum[i] * (1.0 - PLOT_SMOOTHING) +
                             avg * PLOT_SMOOTHING;

        Serial.print(smoothedSpectrum[i]);

        if (i < PLOT_BINS - 1) {
            Serial.print(",");
        }
    }
    Serial.println();
}
