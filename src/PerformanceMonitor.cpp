#include "PerformanceMonitor.h"

PerformanceMonitor::PerformanceMonitor()
    : lastUpdateTime(0)
    , updateInterval(1000)  
    , loopTimeSum(0)
    , loopTimeCount(0)
{
    metrics.reset();
}

PerformanceMonitor::~PerformanceMonitor() {
}

bool PerformanceMonitor::begin() {
    DEBUG_PRINTLN("Initializing Performance Monitor...");
    reset();
    lastUpdateTime = millis();
    updateMemoryStats();
    DEBUG_PRINTLN("Performance Monitor initialized");
    return true;
}

void PerformanceMonitor::startLoop() {
    loopTimer.start();
}

void PerformanceMonitor::endLoop() {
    uint32_t elapsed = loopTimer.stop();
    metrics.loopTime = elapsed;

    if (elapsed > metrics.maxLoopTime) {
        metrics.maxLoopTime = elapsed;
    }

    loopTimeSum += elapsed;
    loopTimeCount++;

    if (millis() - lastUpdateTime >= updateInterval) {
        calculateAverages();
        updateMemoryStats();
        updateThroughput();
        lastUpdateTime = millis();
    }
}

void PerformanceMonitor::recordSensorRead(uint32_t timeUs) {
    metrics.sensorReadTime = timeUs;
}

void PerformanceMonitor::recordFFT(uint32_t timeUs) {
    metrics.fftTime = timeUs;
}

void PerformanceMonitor::recordFeatureExtract(uint32_t timeUs) {
    metrics.featureExtractTime = timeUs;
}

void PerformanceMonitor::recordFaultDetect(uint32_t timeUs) {
    metrics.faultDetectTime = timeUs;
}

void PerformanceMonitor::updateMemoryStats() {
    metrics.freeHeap = ESP.getFreeHeap();

    if (metrics.minFreeHeap == 0 || metrics.freeHeap < metrics.minFreeHeap) {
        metrics.minFreeHeap = metrics.freeHeap;
    }

    uint32_t allocated = ESP.getHeapSize() - metrics.freeHeap;
    if (allocated > metrics.maxAllocHeap) {
        metrics.maxAllocHeap = allocated;
    }

    uint32_t maxBlock = ESP.getMaxAllocHeap();
    if (metrics.freeHeap > 0) {
        metrics.heapFragmentation = 100 - ((maxBlock * 100) / metrics.freeHeap);
    }
}

void PerformanceMonitor::updateThroughput() {
     
    float intervalSec = updateInterval / 1000.0f;

    static uint32_t lastTotalSamples = 0;
    static uint32_t lastTotalFFTs = 0;
    static uint32_t lastTotalDetections = 0;

    if (intervalSec > 0) {
        metrics.samplesPerSecond = (metrics.totalSamples - lastTotalSamples) / intervalSec;
        metrics.fftPerSecond = (metrics.totalFFTs - lastTotalFFTs) / intervalSec;
        metrics.detectionsPerSecond = (metrics.totalDetections - lastTotalDetections) / intervalSec;
    }

    lastTotalSamples = metrics.totalSamples;
    lastTotalFFTs = metrics.totalFFTs;
    lastTotalDetections = metrics.totalDetections;
}

void PerformanceMonitor::calculateAverages() {
    if (loopTimeCount > 0) {
        metrics.avgLoopTime = loopTimeSum / loopTimeCount;

        float expectedPeriod = 1000000.0f / SAMPLING_FREQUENCY_HZ;
        metrics.cpuUsage = (metrics.avgLoopTime / expectedPeriod) * 100.0f;

        if (metrics.cpuUsage > 100.0f) metrics.cpuUsage = 100.0f;

        loopTimeSum = 0;
        loopTimeCount = 0;
    }
}

void PerformanceMonitor::printReport() const {
    Serial.println("\n╔════════════════════════════════════════════════════════╗");
    Serial.println("║           PERFORMANCE REPORT                          ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");

    Serial.println("\n[TIMING]");
    Serial.printf("  Loop Time:         %6lu µs (avg: %6lu µs, max: %6lu µs)\n",
                  metrics.loopTime, metrics.avgLoopTime, metrics.maxLoopTime);
    Serial.printf("  Sensor Read:       %6lu µs\n", metrics.sensorReadTime);
    Serial.printf("  FFT:               %6lu µs\n", metrics.fftTime);
    Serial.printf("  Feature Extract:   %6lu µs\n", metrics.featureExtractTime);
    Serial.printf("  Fault Detect:      %6lu µs\n", metrics.faultDetectTime);

    Serial.println("\n[THROUGHPUT]");
    Serial.printf("  Samples/sec:       %.2f Hz (target: %d Hz)\n",
                  metrics.samplesPerSecond, SAMPLING_FREQUENCY_HZ);
    Serial.printf("  FFTs/sec:          %.2f\n", metrics.fftPerSecond);
    Serial.printf("  Detections/sec:    %.2f\n", metrics.detectionsPerSecond);

    Serial.println("\n[MEMORY]");
    Serial.printf("  Free Heap:         %6lu bytes (%.1f KB)\n",
                  metrics.freeHeap, metrics.freeHeap / 1024.0f);
    Serial.printf("  Min Free Heap:     %6lu bytes (%.1f KB)\n",
                  metrics.minFreeHeap, metrics.minFreeHeap / 1024.0f);
    Serial.printf("  Max Allocated:     %6lu bytes (%.1f KB)\n",
                  metrics.maxAllocHeap, metrics.maxAllocHeap / 1024.0f);
    Serial.printf("  Fragmentation:     %3d%%\n", metrics.heapFragmentation);

    Serial.println("\n[CPU]");
    Serial.printf("  CPU Usage:         %.1f%%\n", metrics.cpuUsage);
    Serial.printf("  Real-Time:         %s\n",
                  isRealTimeCapable() ? "✓ Yes" : "✗ No");

    Serial.println("\n[STATISTICS]");
    Serial.printf("  Total Samples:     %lu\n", metrics.totalSamples);
    Serial.printf("  Total FFTs:        %lu\n", metrics.totalFFTs);
    Serial.printf("  Total Detections:  %lu\n", metrics.totalDetections);
    Serial.printf("  Missed Samples:    %lu", metrics.missedSamples);

    if (metrics.missedSamples > 0) {
        float missRate = (metrics.missedSamples / (float)metrics.totalSamples) * 100.0f;
        Serial.printf(" (%.2f%%)", missRate);
    }
    Serial.println();

    Serial.println("\n════════════════════════════════════════════════════════\n");
}

String PerformanceMonitor::toJSON() const {
    String json = "{";
    json += "\"loopTime\":" + String(metrics.loopTime) + ",";
    json += "\"avgLoopTime\":" + String(metrics.avgLoopTime) + ",";
    json += "\"maxLoopTime\":" + String(metrics.maxLoopTime) + ",";
    json += "\"sensorReadTime\":" + String(metrics.sensorReadTime) + ",";
    json += "\"fftTime\":" + String(metrics.fftTime) + ",";
    json += "\"featureExtractTime\":" + String(metrics.featureExtractTime) + ",";
    json += "\"faultDetectTime\":" + String(metrics.faultDetectTime) + ",";
    json += "\"samplesPerSecond\":" + String(metrics.samplesPerSecond, 2) + ",";
    json += "\"fftPerSecond\":" + String(metrics.fftPerSecond, 2) + ",";
    json += "\"freeHeap\":" + String(metrics.freeHeap) + ",";
    json += "\"minFreeHeap\":" + String(metrics.minFreeHeap) + ",";
    json += "\"cpuUsage\":" + String(metrics.cpuUsage, 1) + ",";
    json += "\"totalSamples\":" + String(metrics.totalSamples) + ",";
    json += "\"totalFFTs\":" + String(metrics.totalFFTs) + ",";
    json += "\"missedSamples\":" + String(metrics.missedSamples);
    json += "}";
    return json;
}

void PerformanceMonitor::reset() {
    metrics.reset();
    loopTimeSum = 0;
    loopTimeCount = 0;
    lastUpdateTime = millis();
}

bool PerformanceMonitor::isRealTimeCapable() const {
     
    float targetPeriod = 1000000.0f / SAMPLING_FREQUENCY_HZ;  

    return (metrics.avgLoopTime < targetPeriod) && (metrics.cpuUsage < 90.0f);
}
