#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <Arduino.h>
#include "Config.h"

struct PerformanceMetrics {

    uint32_t loopTime;
    uint32_t sensorReadTime;
    uint32_t fftTime;
    uint32_t featureExtractTime;
    uint32_t faultDetectTime;

    float samplesPerSecond;
    float fftPerSecond;
    float detectionsPerSecond;

    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t maxAllocHeap;
    uint8_t heapFragmentation;

    float cpuUsage;
    uint32_t maxLoopTime;
    uint32_t avgLoopTime;

    uint32_t totalSamples;
    uint32_t totalFFTs;
    uint32_t totalDetections;
    uint32_t missedSamples;

    void reset() {
        loopTime = sensorReadTime = fftTime = 0;
        featureExtractTime = faultDetectTime = 0;
        samplesPerSecond = fftPerSecond = detectionsPerSecond = 0.0f;
        freeHeap = minFreeHeap = maxAllocHeap = 0;
        heapFragmentation = 0;
        cpuUsage = 0.0f;
        maxLoopTime = avgLoopTime = 0;
        totalSamples = totalFFTs = totalDetections = missedSamples = 0;
    }
};

class ProfileTimer {
public:
    ProfileTimer() : startTime(0), elapsed(0) {}

    void start() {
        startTime = micros();
    }

    uint32_t stop() {
        elapsed = micros() - startTime;
        return elapsed;
    }

    uint32_t getElapsed() const {
        return elapsed;
    }

private:
    uint32_t startTime;
    uint32_t elapsed;
};

class PerformanceMonitor {
public:

    PerformanceMonitor();

    ~PerformanceMonitor();

    bool begin();

    void startLoop();

    void endLoop();

    void recordSensorRead(uint32_t timeUs);

    void recordFFT(uint32_t timeUs);

    void recordFeatureExtract(uint32_t timeUs);

    void recordFaultDetect(uint32_t timeUs);

    void incrementSamples() { metrics.totalSamples++; }

    void incrementFFTs() { metrics.totalFFTs++; }

    void incrementDetections() { metrics.totalDetections++; }

    void recordMissedSample() { metrics.missedSamples++; }

    void updateMemoryStats();

    void updateThroughput();

    const PerformanceMetrics& getMetrics() const { return metrics; }

    void printReport() const;

    String toJSON() const;

    void reset();

    bool isRealTimeCapable() const;

    float getCPUUsage() const { return metrics.cpuUsage; }

private:
    PerformanceMetrics metrics;
    ProfileTimer loopTimer;

    uint32_t lastUpdateTime;
    uint32_t updateInterval;

    uint32_t loopTimeSum;
    uint32_t loopTimeCount;

    void calculateAverages();
};

#endif
