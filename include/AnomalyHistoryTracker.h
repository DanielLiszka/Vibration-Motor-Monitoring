#ifndef ANOMALY_HISTORY_TRACKER_H
#define ANOMALY_HISTORY_TRACKER_H

#include <Arduino.h>
#include "Config.h"
#include "FaultDetector.h"

#define ANOMALY_HISTORY_HOURS 168
#define ANOMALY_BUCKETS_PER_HOUR 4
#define TOTAL_ANOMALY_BUCKETS (ANOMALY_HISTORY_HOURS * ANOMALY_BUCKETS_PER_HOUR)
#define BUCKET_DURATION_MS (3600000 / ANOMALY_BUCKETS_PER_HOUR)

struct AnomalyBucket {
    float avgScore;
    float maxScore;
    float minScore;
    uint16_t sampleCount;
    uint8_t dominantFaultType;
    uint8_t faultTypeCounts[5];
    uint32_t startTime;
    bool valid;
};

struct HourlyAggregate {
    float avgScore;
    float maxScore;
    float variance;
    uint8_t dominantFault;
    uint16_t totalSamples;
    bool hasFault;
};

struct DailyAggregate {
    float avgScore;
    float maxScore;
    float minScore;
    float slope;
    uint8_t dominantFault;
    uint8_t faultHours;
    bool valid;
};

struct AnomalyTrend {
    float hourlySlope;
    float dailySlope;
    float weeklySlope;
    float acceleration;
    bool isTrendingUp;
    bool isVolatile;
    float volatilityScore;
};

struct AnomalyPattern {
    bool hasPeriodicPattern;
    float periodHours;
    float patternStrength;
    uint8_t peakHour;
    uint8_t troughHour;
};

struct AnomalyStats {
    float currentScore;
    float hourlyAvg;
    float dailyAvg;
    float weeklyAvg;
    float percentile90;
    float percentile95;
    uint32_t faultCount;
    uint32_t totalSamples;
};

typedef void (*AnomalyAlertCallback)(float score, uint8_t faultType, const AnomalyTrend& trend);

class AnomalyHistoryTracker {
public:
    AnomalyHistoryTracker();
    ~AnomalyHistoryTracker();

    bool begin();
    void reset();

    void record(float anomalyScore, uint8_t faultType);
    void record(const FaultResult& faultResult);

    AnomalyStats getStats() const;
    AnomalyTrend getTrend() const { return currentTrend; }
    AnomalyPattern getPattern() const { return detectedPattern; }

    float getCurrentScore() const { return currentScore; }
    float getHourlyAverage() const;
    float getDailyAverage() const;
    float getWeeklyAverage() const;

    HourlyAggregate getHourlyAggregate(size_t hoursAgo) const;
    DailyAggregate getDailyAggregate(size_t daysAgo) const;

    AnomalyBucket getBucket(size_t index) const;
    size_t getBucketCount() const { return bucketCount; }
    size_t getCurrentBucketIndex() const { return currentBucketIndex; }

    void analyzeTrend();
    void detectPatterns();

    bool isTrendingUp() const { return currentTrend.isTrendingUp; }
    bool isVolatile() const { return currentTrend.isVolatile; }
    float getVolatility() const { return currentTrend.volatilityScore; }

    float getPercentile(float p) const;
    float getQuantile(float q) const;

    void setAlertThreshold(float threshold) { alertThreshold = threshold; }
    float getAlertThreshold() const { return alertThreshold; }

    void setCallback(AnomalyAlertCallback callback) { alertCallback = callback; }

    uint32_t getFaultCountInWindow(uint32_t windowHours) const;
    uint8_t getDominantFaultType(uint32_t windowHours) const;

    float predictScore(uint32_t hoursAhead) const;
    float getTimeToThreshold(float threshold) const;

    String generateHistoryReport() const;
    String generateTrendJSON() const;
    String generateDailyReport(size_t daysAgo) const;

    bool saveHistory(const char* filename);
    bool loadHistory(const char* filename);

    void exportToCSV(String& output, size_t maxBuckets) const;

private:
    AnomalyBucket buckets[TOTAL_ANOMALY_BUCKETS];
    size_t currentBucketIndex;
    size_t bucketCount;

    float currentScore;
    uint32_t lastRecordTime;
    uint32_t lastBucketTime;

    AnomalyTrend currentTrend;
    AnomalyPattern detectedPattern;

    float alertThreshold;
    uint32_t totalSamples;
    uint32_t totalFaults;

    AnomalyAlertCallback alertCallback;

    void advanceBucket();
    void initializeBucket(size_t index);
    void updateCurrentBucket(float score, uint8_t faultType);

    float computeHourlySlope() const;
    float computeDailySlope() const;
    float computeWeeklySlope() const;
    float computeVolatility(size_t windowBuckets) const;

    void detectPeriodicPattern();
    float computeAutocorrelation(size_t lag) const;

    size_t getBucketIndexForTime(uint32_t timestamp) const;
    uint32_t getTimeForBucketIndex(size_t index) const;
};

#endif
