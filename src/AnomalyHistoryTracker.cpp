#include "AnomalyHistoryTracker.h"
#include "StorageManager.h"
#include <math.h>

AnomalyHistoryTracker::AnomalyHistoryTracker()
    : currentBucketIndex(0)
    , bucketCount(0)
    , currentScore(0.0f)
    , lastRecordTime(0)
    , lastBucketTime(0)
    , alertThreshold(5.0f)
    , totalSamples(0)
    , totalFaults(0)
    , alertCallback(nullptr)
{
    memset(buckets, 0, sizeof(buckets));
    memset(&currentTrend, 0, sizeof(currentTrend));
    memset(&detectedPattern, 0, sizeof(detectedPattern));
}

AnomalyHistoryTracker::~AnomalyHistoryTracker() {
}

bool AnomalyHistoryTracker::begin() {
    reset();
    DEBUG_PRINTLN("AnomalyHistoryTracker initialized");
    return true;
}

void AnomalyHistoryTracker::reset() {
    currentBucketIndex = 0;
    bucketCount = 0;
    currentScore = 0.0f;
    lastRecordTime = 0;
    lastBucketTime = millis();
    totalSamples = 0;
    totalFaults = 0;

    memset(buckets, 0, sizeof(buckets));
    memset(&currentTrend, 0, sizeof(currentTrend));
    memset(&detectedPattern, 0, sizeof(detectedPattern));

    initializeBucket(0);
}

void AnomalyHistoryTracker::initializeBucket(size_t index) {
    buckets[index].avgScore = 0.0f;
    buckets[index].maxScore = 0.0f;
    buckets[index].minScore = INFINITY;
    buckets[index].sampleCount = 0;
    buckets[index].dominantFaultType = 0;
    memset(buckets[index].faultTypeCounts, 0, sizeof(buckets[index].faultTypeCounts));
    buckets[index].startTime = millis();
    buckets[index].valid = true;
}

void AnomalyHistoryTracker::record(float anomalyScore, uint8_t faultType) {
    uint32_t now = millis();

    if (now - lastBucketTime >= BUCKET_DURATION_MS) {
        advanceBucket();
    }

    updateCurrentBucket(anomalyScore, faultType);

    currentScore = anomalyScore;
    lastRecordTime = now;
    totalSamples++;

    if (faultType > 0) {
        totalFaults++;
    }

    if (totalSamples % 100 == 0) {
        analyzeTrend();
    }

    if (totalSamples % 500 == 0) {
        detectPatterns();
    }

    if (anomalyScore > alertThreshold && alertCallback) {
        alertCallback(anomalyScore, faultType, currentTrend);
    }
}

void AnomalyHistoryTracker::record(const FaultResult& faultResult) {
    record(faultResult.anomalyScore, (uint8_t)faultResult.type);
}

void AnomalyHistoryTracker::updateCurrentBucket(float score, uint8_t faultType) {
    AnomalyBucket& bucket = buckets[currentBucketIndex];

    bucket.sampleCount++;

    float prevAvg = bucket.avgScore;
    bucket.avgScore = prevAvg + (score - prevAvg) / bucket.sampleCount;

    if (score > bucket.maxScore) bucket.maxScore = score;
    if (score < bucket.minScore) bucket.minScore = score;

    if (faultType < 5) {
        bucket.faultTypeCounts[faultType]++;

        uint8_t maxCount = 0;
        for (int i = 0; i < 5; i++) {
            if (bucket.faultTypeCounts[i] > maxCount) {
                maxCount = bucket.faultTypeCounts[i];
                bucket.dominantFaultType = i;
            }
        }
    }
}

void AnomalyHistoryTracker::advanceBucket() {
    currentBucketIndex = (currentBucketIndex + 1) % TOTAL_ANOMALY_BUCKETS;

    if (bucketCount < TOTAL_ANOMALY_BUCKETS) {
        bucketCount++;
    }

    initializeBucket(currentBucketIndex);
    lastBucketTime = millis();
}

AnomalyStats AnomalyHistoryTracker::getStats() const {
    AnomalyStats stats;
    stats.currentScore = currentScore;
    stats.hourlyAvg = getHourlyAverage();
    stats.dailyAvg = getDailyAverage();
    stats.weeklyAvg = getWeeklyAverage();
    stats.percentile90 = getPercentile(90.0f);
    stats.percentile95 = getPercentile(95.0f);
    stats.faultCount = totalFaults;
    stats.totalSamples = totalSamples;
    return stats;
}

float AnomalyHistoryTracker::getHourlyAverage() const {
    if (bucketCount == 0) return 0.0f;

    float sum = 0.0f;
    size_t count = 0;
    size_t bucketsInHour = ANOMALY_BUCKETS_PER_HOUR;

    for (size_t i = 0; i < bucketsInHour && i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (buckets[idx].valid && buckets[idx].sampleCount > 0) {
            sum += buckets[idx].avgScore;
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0f;
}

float AnomalyHistoryTracker::getDailyAverage() const {
    if (bucketCount == 0) return 0.0f;

    float sum = 0.0f;
    size_t count = 0;
    size_t bucketsInDay = ANOMALY_BUCKETS_PER_HOUR * 24;

    for (size_t i = 0; i < bucketsInDay && i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (buckets[idx].valid && buckets[idx].sampleCount > 0) {
            sum += buckets[idx].avgScore;
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0f;
}

float AnomalyHistoryTracker::getWeeklyAverage() const {
    if (bucketCount == 0) return 0.0f;

    float sum = 0.0f;
    size_t count = 0;

    for (size_t i = 0; i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (buckets[idx].valid && buckets[idx].sampleCount > 0) {
            sum += buckets[idx].avgScore;
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0f;
}

HourlyAggregate AnomalyHistoryTracker::getHourlyAggregate(size_t hoursAgo) const {
    HourlyAggregate agg;
    memset(&agg, 0, sizeof(agg));

    size_t startBucket = hoursAgo * ANOMALY_BUCKETS_PER_HOUR;
    size_t endBucket = startBucket + ANOMALY_BUCKETS_PER_HOUR;

    if (startBucket >= bucketCount) return agg;

    float sum = 0.0f;
    float maxScore = 0.0f;
    float m2 = 0.0f;
    size_t n = 0;
    uint8_t faultCounts[5] = {0};

    for (size_t i = startBucket; i < endBucket && i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (!buckets[idx].valid) continue;

        n++;
        float delta = buckets[idx].avgScore - sum / (n > 1 ? n - 1 : 1);
        sum += buckets[idx].avgScore;
        float delta2 = buckets[idx].avgScore - sum / n;
        m2 += delta * delta2;

        if (buckets[idx].maxScore > maxScore) maxScore = buckets[idx].maxScore;

        for (int f = 0; f < 5; f++) {
            faultCounts[f] += buckets[idx].faultTypeCounts[f];
        }

        agg.totalSamples += buckets[idx].sampleCount;
    }

    if (n > 0) {
        agg.avgScore = sum / n;
        agg.maxScore = maxScore;
        agg.variance = (n > 1) ? m2 / (n - 1) : 0.0f;

        uint8_t maxCount = 0;
        for (int f = 0; f < 5; f++) {
            if (faultCounts[f] > maxCount) {
                maxCount = faultCounts[f];
                agg.dominantFault = f;
            }
        }

        agg.hasFault = (faultCounts[1] + faultCounts[2] + faultCounts[3] + faultCounts[4]) > 0;
    }

    return agg;
}

DailyAggregate AnomalyHistoryTracker::getDailyAggregate(size_t daysAgo) const {
    DailyAggregate agg;
    memset(&agg, 0, sizeof(agg));

    agg.minScore = INFINITY;

    size_t startHour = daysAgo * 24;
    size_t endHour = startHour + 24;

    float sum = 0.0f;
    size_t count = 0;

    for (size_t h = startHour; h < endHour; h++) {
        HourlyAggregate hourly = getHourlyAggregate(h);
        if (hourly.totalSamples > 0) {
            sum += hourly.avgScore;
            count++;

            if (hourly.maxScore > agg.maxScore) agg.maxScore = hourly.maxScore;
            if (hourly.avgScore < agg.minScore) agg.minScore = hourly.avgScore;

            if (hourly.hasFault) agg.faultHours++;
        }
    }

    if (count > 0) {
        agg.avgScore = sum / count;
        agg.valid = true;

        agg.slope = computeDailySlope();
    }

    return agg;
}

AnomalyBucket AnomalyHistoryTracker::getBucket(size_t index) const {
    if (index >= TOTAL_ANOMALY_BUCKETS) {
        AnomalyBucket empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return buckets[index];
}

void AnomalyHistoryTracker::analyzeTrend() {
    currentTrend.hourlySlope = computeHourlySlope();
    currentTrend.dailySlope = computeDailySlope();
    currentTrend.weeklySlope = computeWeeklySlope();

    currentTrend.isTrendingUp = currentTrend.dailySlope > 0.01f;
    currentTrend.volatilityScore = computeVolatility(ANOMALY_BUCKETS_PER_HOUR * 24);
    currentTrend.isVolatile = currentTrend.volatilityScore > 0.5f;

    currentTrend.acceleration = currentTrend.dailySlope - currentTrend.weeklySlope;
}

float AnomalyHistoryTracker::computeHourlySlope() const {
    size_t bucketsInHour = ANOMALY_BUCKETS_PER_HOUR;
    if (bucketCount < bucketsInHour) return 0.0f;

    float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;
    size_t n = 0;

    for (size_t i = 0; i < bucketsInHour && i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (!buckets[idx].valid || buckets[idx].sampleCount == 0) continue;

        float x = (float)i;
        float y = buckets[idx].avgScore;

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
        n++;
    }

    if (n < 2) return 0.0f;

    float denom = n * sumX2 - sumX * sumX;
    if (fabs(denom) < 0.0001f) return 0.0f;

    return (n * sumXY - sumX * sumY) / denom;
}

float AnomalyHistoryTracker::computeDailySlope() const {
    size_t bucketsInDay = ANOMALY_BUCKETS_PER_HOUR * 24;
    if (bucketCount < bucketsInDay / 4) return 0.0f;

    size_t windowSize = (bucketCount < bucketsInDay) ? bucketCount : bucketsInDay;

    float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;
    size_t n = 0;

    for (size_t i = 0; i < windowSize; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (!buckets[idx].valid || buckets[idx].sampleCount == 0) continue;

        float x = (float)i;
        float y = buckets[idx].avgScore;

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
        n++;
    }

    if (n < 2) return 0.0f;

    float denom = n * sumX2 - sumX * sumX;
    if (fabs(denom) < 0.0001f) return 0.0f;

    return (n * sumXY - sumX * sumY) / denom;
}

float AnomalyHistoryTracker::computeWeeklySlope() const {
    if (bucketCount < ANOMALY_BUCKETS_PER_HOUR * 24) return 0.0f;

    float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;
    size_t n = 0;

    for (size_t i = 0; i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (!buckets[idx].valid || buckets[idx].sampleCount == 0) continue;

        float x = (float)i;
        float y = buckets[idx].avgScore;

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
        n++;
    }

    if (n < 2) return 0.0f;

    float denom = n * sumX2 - sumX * sumX;
    if (fabs(denom) < 0.0001f) return 0.0f;

    return (n * sumXY - sumX * sumY) / denom;
}

float AnomalyHistoryTracker::computeVolatility(size_t windowBuckets) const {
    if (bucketCount < 2) return 0.0f;

    size_t windowSize = (bucketCount < windowBuckets) ? bucketCount : windowBuckets;

    float sum = 0.0f;
    float m2 = 0.0f;
    size_t n = 0;

    for (size_t i = 0; i < windowSize; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (!buckets[idx].valid || buckets[idx].sampleCount == 0) continue;

        n++;
        float delta = buckets[idx].avgScore - sum / (n > 1 ? n - 1 : 1);
        sum += buckets[idx].avgScore;
        float delta2 = buckets[idx].avgScore - sum / n;
        m2 += delta * delta2;
    }

    if (n < 2) return 0.0f;

    float variance = m2 / (n - 1);
    float mean = sum / n;

    if (mean < 0.0001f) return 0.0f;

    return sqrt(variance) / mean;
}

void AnomalyHistoryTracker::detectPatterns() {
    detectPeriodicPattern();
}

void AnomalyHistoryTracker::detectPeriodicPattern() {
    if (bucketCount < ANOMALY_BUCKETS_PER_HOUR * 48) {
        detectedPattern.hasPeriodicPattern = false;
        return;
    }

    float maxCorr = 0.0f;
    size_t bestLag = 0;

    for (size_t lag = ANOMALY_BUCKETS_PER_HOUR; lag <= ANOMALY_BUCKETS_PER_HOUR * 24; lag += ANOMALY_BUCKETS_PER_HOUR / 2) {
        float corr = computeAutocorrelation(lag);
        if (corr > maxCorr) {
            maxCorr = corr;
            bestLag = lag;
        }
    }

    detectedPattern.hasPeriodicPattern = maxCorr > 0.3f;
    detectedPattern.periodHours = (float)bestLag / ANOMALY_BUCKETS_PER_HOUR;
    detectedPattern.patternStrength = maxCorr;
}

float AnomalyHistoryTracker::computeAutocorrelation(size_t lag) const {
    if (bucketCount <= lag) return 0.0f;

    float mean = getWeeklyAverage();
    float sumNum = 0.0f;
    float sumDenom = 0.0f;
    size_t n = 0;

    for (size_t i = lag; i < bucketCount; i++) {
        size_t idx1 = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        size_t idx2 = (currentBucketIndex - i + lag + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;

        if (!buckets[idx1].valid || !buckets[idx2].valid) continue;

        float y1 = buckets[idx1].avgScore - mean;
        float y2 = buckets[idx2].avgScore - mean;

        sumNum += y1 * y2;
        sumDenom += y1 * y1;
        n++;
    }

    if (n < 10 || sumDenom < 0.0001f) return 0.0f;

    return sumNum / sumDenom;
}

float AnomalyHistoryTracker::getPercentile(float p) const {
    return getQuantile(p / 100.0f);
}

float AnomalyHistoryTracker::getQuantile(float q) const {
    if (bucketCount == 0) return 0.0f;

    float values[TOTAL_ANOMALY_BUCKETS];
    size_t count = 0;

    for (size_t i = 0; i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (buckets[idx].valid && buckets[idx].sampleCount > 0) {
            values[count++] = buckets[idx].avgScore;
        }
    }

    if (count == 0) return 0.0f;

    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = 0; j < count - i - 1; j++) {
            if (values[j] > values[j + 1]) {
                float temp = values[j];
                values[j] = values[j + 1];
                values[j + 1] = temp;
            }
        }
    }

    size_t idx = (size_t)(q * (count - 1));
    return values[idx];
}

uint32_t AnomalyHistoryTracker::getFaultCountInWindow(uint32_t windowHours) const {
    uint32_t count = 0;
    size_t windowBuckets = windowHours * ANOMALY_BUCKETS_PER_HOUR;

    for (size_t i = 0; i < windowBuckets && i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (buckets[idx].valid) {
            for (int f = 1; f < 5; f++) {
                count += buckets[idx].faultTypeCounts[f];
            }
        }
    }

    return count;
}

uint8_t AnomalyHistoryTracker::getDominantFaultType(uint32_t windowHours) const {
    uint32_t faultCounts[5] = {0};
    size_t windowBuckets = windowHours * ANOMALY_BUCKETS_PER_HOUR;

    for (size_t i = 0; i < windowBuckets && i < bucketCount; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (buckets[idx].valid) {
            for (int f = 0; f < 5; f++) {
                faultCounts[f] += buckets[idx].faultTypeCounts[f];
            }
        }
    }

    uint8_t dominant = 0;
    uint32_t maxCount = 0;
    for (int f = 1; f < 5; f++) {
        if (faultCounts[f] > maxCount) {
            maxCount = faultCounts[f];
            dominant = f;
        }
    }

    return dominant;
}

float AnomalyHistoryTracker::predictScore(uint32_t hoursAhead) const {
    float baseScore = getDailyAverage();
    float trend = currentTrend.dailySlope;

    return baseScore + trend * hoursAhead;
}

float AnomalyHistoryTracker::getTimeToThreshold(float threshold) const {
    float currentAvg = getDailyAverage();
    float slope = currentTrend.dailySlope;

    if (slope <= 0.0001f) return 10000.0f;
    if (currentAvg >= threshold) return 0.0f;

    return (threshold - currentAvg) / slope;
}

String AnomalyHistoryTracker::generateHistoryReport() const {
    String report = "=== Anomaly History Report ===\n";
    report += "Current Score: " + String(currentScore, 2) + "\n";
    report += "Hourly Avg: " + String(getHourlyAverage(), 2) + "\n";
    report += "Daily Avg: " + String(getDailyAverage(), 2) + "\n";
    report += "Weekly Avg: " + String(getWeeklyAverage(), 2) + "\n";
    report += "90th Percentile: " + String(getPercentile(90.0f), 2) + "\n";
    report += "Total Samples: " + String(totalSamples) + "\n";
    report += "Total Faults: " + String(totalFaults) + "\n";
    report += "Trend: " + String(currentTrend.isTrendingUp ? "Up" : "Stable/Down") + "\n";
    report += "Volatile: " + String(currentTrend.isVolatile ? "Yes" : "No") + "\n";

    return report;
}

String AnomalyHistoryTracker::generateTrendJSON() const {
    String json = "{\"anomalyHistory\":{";
    json += "\"current\":" + String(currentScore, 4) + ",";
    json += "\"hourlyAvg\":" + String(getHourlyAverage(), 4) + ",";
    json += "\"dailyAvg\":" + String(getDailyAverage(), 4) + ",";
    json += "\"weeklyAvg\":" + String(getWeeklyAverage(), 4) + ",";
    json += "\"trend\":{";
    json += "\"hourlySlope\":" + String(currentTrend.hourlySlope, 6) + ",";
    json += "\"dailySlope\":" + String(currentTrend.dailySlope, 6) + ",";
    json += "\"weeklySlope\":" + String(currentTrend.weeklySlope, 6) + ",";
    json += "\"isTrendingUp\":" + String(currentTrend.isTrendingUp ? "true" : "false") + ",";
    json += "\"isVolatile\":" + String(currentTrend.isVolatile ? "true" : "false") + ",";
    json += "\"volatility\":" + String(currentTrend.volatilityScore, 4);
    json += "},";
    json += "\"bucketCount\":" + String(bucketCount) + ",";
    json += "\"totalSamples\":" + String(totalSamples) + ",";
    json += "\"totalFaults\":" + String(totalFaults);
    json += "}}";

    return json;
}

String AnomalyHistoryTracker::generateDailyReport(size_t daysAgo) const {
    DailyAggregate daily = getDailyAggregate(daysAgo);

    String report = "Day -" + String(daysAgo) + ":\n";
    report += "  Avg Score: " + String(daily.avgScore, 2) + "\n";
    report += "  Max Score: " + String(daily.maxScore, 2) + "\n";
    report += "  Fault Hours: " + String(daily.faultHours) + "\n";
    report += "  Slope: " + String(daily.slope, 4) + "\n";

    return report;
}

bool AnomalyHistoryTracker::saveHistory(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = "AHST";
    data += String(bucketCount) + ",";
    data += String(currentBucketIndex) + ",";
    data += String(totalSamples) + ",";
    data += String(totalFaults) + ",";

    return storage.saveLog(filename, data);
}

bool AnomalyHistoryTracker::loadHistory(const char* filename) {
    StorageManager storage;
    if (!storage.begin()) return false;

    String data = storage.readLog(filename);
    if (data.length() < 10 || !data.startsWith("AHST")) return false;

    return true;
}

void AnomalyHistoryTracker::exportToCSV(String& output, size_t maxBuckets) const {
    output = "index,avgScore,maxScore,minScore,sampleCount,dominantFault\n";

    size_t count = (maxBuckets < bucketCount) ? maxBuckets : bucketCount;

    for (size_t i = 0; i < count; i++) {
        size_t idx = (currentBucketIndex - i + TOTAL_ANOMALY_BUCKETS) % TOTAL_ANOMALY_BUCKETS;
        if (!buckets[idx].valid) continue;

        output += String(i) + ",";
        output += String(buckets[idx].avgScore, 4) + ",";
        output += String(buckets[idx].maxScore, 4) + ",";
        output += String(buckets[idx].minScore, 4) + ",";
        output += String(buckets[idx].sampleCount) + ",";
        output += String(buckets[idx].dominantFaultType) + "\n";
    }
}
