#include "EnergyMonitor.h"

EnergyMonitor::EnergyMonitor()
    : historyIndex(0)
    , historyCount(0)
    , totalEnergy(0.0f)
    , peakPower(0.0f)
    , startTime(0)
    , energyRate(0.12f)
{
    memset(powerHistory, 0, sizeof(powerHistory));
}

EnergyMonitor::~EnergyMonitor() {
}

bool EnergyMonitor::begin() {
    DEBUG_PRINTLN("Initializing Energy Monitor...");
    reset();
    startTime = millis();
    return true;
}

void EnergyMonitor::updatePowerMetrics(float voltage, float current) {
    currentMetrics.voltage = voltage;
    currentMetrics.current = current;
    currentMetrics.power = voltage * current * POWER_FACTOR;
    currentMetrics.powerFactor = POWER_FACTOR;
    currentMetrics.timestamp = millis();

    if (currentMetrics.power > peakPower) {
        peakPower = currentMetrics.power;
    }

    uint32_t timeDelta = 1000;
    if (historyCount > 0) {
        timeDelta = currentMetrics.timestamp - powerHistory[historyIndex];
    }

    float energyIncrement = (currentMetrics.power * timeDelta) / 3600000.0f;
    totalEnergy += energyIncrement;
    currentMetrics.energy = totalEnergy;

    powerHistory[historyIndex] = currentMetrics.power;
    historyIndex = (historyIndex + 1) % ENERGY_SAMPLES;
    if (historyCount < ENERGY_SAMPLES) {
        historyCount++;
    }
}

EnergyAnalysis EnergyMonitor::getAnalysis() const {
    EnergyAnalysis analysis;

    analysis.totalEnergy = totalEnergy;
    analysis.averagePower = calculateMovingAverage(historyCount);
    analysis.peakPower = peakPower;
    analysis.powerTrend = calculatePowerTrend();

    uint32_t runTime = millis() - startTime;
    analysis.operatingHours = runTime / 3600000UL;

    float expectedEnergy = analysis.averagePower * analysis.operatingHours / 1000.0f;
    if (expectedEnergy > 0) {
        analysis.efficiencyScore = (expectedEnergy / (totalEnergy + 0.001f)) * EFFICIENCY_NOMINAL;
    } else {
        analysis.efficiencyScore = EFFICIENCY_NOMINAL;
    }

    analysis.costEstimate = estimateCost(totalEnergy);

    return analysis;
}

float EnergyMonitor::estimateVibrationPowerLoss(const FeatureVector& features) {
    float vibrationPower = 0.0f;

    vibrationPower += features.rms * 0.5f;

    vibrationPower += features.peakToPeak * 0.3f;

    if (features.dominantFrequency > 0) {
        vibrationPower += (features.dominantFrequency / 100.0f) * 0.2f;
    }

    return vibrationPower;
}

float EnergyMonitor::calculateEfficiency(float inputPower, float vibrationLevel) {
    if (inputPower <= 0) {
        return 0.0f;
    }

    float vibrationLoss = vibrationLevel * 0.01f;

    float efficiency = EFFICIENCY_NOMINAL * (1.0f - vibrationLoss);

    return max(0.0f, min(1.0f, efficiency));
}

float EnergyMonitor::getDailyCost() const {
    uint32_t runTime = millis() - startTime;
    float hours = runTime / 3600000.0f;

    if (hours < 0.01f) {
        return 0.0f;
    }

    float energyPerHour = totalEnergy / hours;
    float dailyEnergy = energyPerHour * 24.0f;

    return estimateCost(dailyEnergy);
}

float EnergyMonitor::getMonthlyCost() const {
    return getDailyCost() * 30.0f;
}

float EnergyMonitor::getYearlyCost() const {
    return getDailyCost() * 365.0f;
}

String EnergyMonitor::generateEnergyReport() {
    EnergyAnalysis analysis = getAnalysis();

    String report = "=== ENERGY REPORT ===\n\n";

    report += "Current Power: " + String(currentMetrics.power, 2) + " W\n";
    report += "Voltage: " + String(currentMetrics.voltage, 1) + " V\n";
    report += "Current: " + String(currentMetrics.current, 2) + " A\n";
    report += "Power Factor: " + String(currentMetrics.powerFactor, 2) + "\n\n";

    report += "Total Energy: " + String(analysis.totalEnergy, 3) + " Wh\n";
    report += "Average Power: " + String(analysis.averagePower, 2) + " W\n";
    report += "Peak Power: " + String(analysis.peakPower, 2) + " W\n";
    report += "Efficiency: " + String(analysis.efficiencyScore * 100, 1) + "%\n\n";

    report += "Operating Hours: " + String(analysis.operatingHours) + " h\n";
    report += "Power Trend: " + String(analysis.powerTrend > 0 ? "Increasing" : "Decreasing") + "\n\n";

    report += "Cost Estimates:\n";
    report += "  Daily: $" + String(getDailyCost(), 2) + "\n";
    report += "  Monthly: $" + String(getMonthlyCost(), 2) + "\n";
    report += "  Yearly: $" + String(getYearlyCost(), 2) + "\n";

    report += "\n=====================\n";

    return report;
}

void EnergyMonitor::reset() {
    historyIndex = 0;
    historyCount = 0;
    totalEnergy = 0.0f;
    peakPower = 0.0f;
    startTime = millis();
    memset(powerHistory, 0, sizeof(powerHistory));
}

float EnergyMonitor::calculateMovingAverage(size_t window) const {
    if (window == 0 || historyCount == 0) {
        return 0.0f;
    }

    size_t samples = min(window, historyCount);
    float sum = 0.0f;

    for (size_t i = 0; i < samples; i++) {
        size_t idx = (historyIndex - 1 - i + ENERGY_SAMPLES) % ENERGY_SAMPLES;
        sum += powerHistory[idx];
    }

    return sum / samples;
}

float EnergyMonitor::calculatePowerTrend() const {
    if (historyCount < 10) {
        return 0.0f;
    }

    float recent = calculateMovingAverage(10);
    float older = calculateMovingAverage(historyCount);

    return recent - older;
}

float EnergyMonitor::estimateCost(float energyKWh) const {
    return (energyKWh / 1000.0f) * energyRate;
}
