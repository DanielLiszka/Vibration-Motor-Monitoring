#ifndef ENERGY_MONITOR_H
#define ENERGY_MONITOR_H

#include <Arduino.h>
#include "Config.h"
#include "FeatureExtractor.h"

#define ENERGY_SAMPLES 100
#define EFFICIENCY_NOMINAL 0.90

struct PowerMetrics {
    float voltage;
    float current;
    float power;
    float powerFactor;
    float energy;
    float efficiency;
    uint32_t timestamp;
};

struct EnergyAnalysis {
    float totalEnergy;
    float averagePower;
    float peakPower;
    float powerTrend;
    float efficiencyScore;
    float costEstimate;
    uint32_t operatingHours;
};

class EnergyMonitor {
public:
    EnergyMonitor();
    ~EnergyMonitor();

    bool begin();

    void updatePowerMetrics(float voltage, float current);

    PowerMetrics getCurrentMetrics() const { return currentMetrics; }
    EnergyAnalysis getAnalysis() const;

    float estimateVibrationPowerLoss(const FeatureVector& features);

    float calculateEfficiency(float inputPower, float vibrationLevel);

    void setEnergyRate(float ratePerKWh) { energyRate = ratePerKWh; }
    float getEnergyRate() const { return energyRate; }

    float getDailyCost() const;
    float getMonthlyCost() const;
    float getYearlyCost() const;

    String generateEnergyReport();

    void reset();

private:
    PowerMetrics currentMetrics;
    float powerHistory[ENERGY_SAMPLES];
    size_t historyIndex;
    size_t historyCount;

    float totalEnergy;
    float peakPower;
    uint32_t startTime;
    float energyRate;

    float calculateMovingAverage(size_t window) const;
    float calculatePowerTrend() const;
    float estimateCost(float energyKWh) const;
};

#endif
