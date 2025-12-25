#include "MultiAxisAnalyzer.h"

MultiAxisAnalyzer::MultiAxisAnalyzer()
    : procX(nullptr)
    , procY(nullptr)
    , procZ(nullptr)
    , sampleCount(0)
{
    procX = new SignalProcessor();
    procY = new SignalProcessor();
    procZ = new SignalProcessor();
}

MultiAxisAnalyzer::~MultiAxisAnalyzer() {
    delete procX;
    delete procY;
    delete procZ;
}

bool MultiAxisAnalyzer::begin() {
    DEBUG_PRINTLN("Initializing Multi-Axis Analyzer...");

    if (!procX->begin() || !procY->begin() || !procZ->begin()) {
        DEBUG_PRINTLN("Failed to initialize axis processors");
        return false;
    }

    reset();
    return true;
}

void MultiAxisAnalyzer::addSample(float x, float y, float z) {
    procX->addSample(x, 0);
    procY->addSample(y, 0);
    procZ->addSample(z, 0);

    sampleCount++;
}

MultiAxisAnalysis MultiAxisAnalyzer::analyze() {
    MultiAxisAnalysis analysis;

    if (!isReady()) {
        DEBUG_PRINTLN("Not enough samples for multi-axis analysis");
        return analysis;
    }

    procX->performFFT(0);
    procY->performFFT(0);
    procZ->performFFT(0);

    analyzeAxis(procX, analysis.xAxis);
    analyzeAxis(procY, analysis.yAxis);
    analyzeAxis(procZ, analysis.zAxis);

    analysis.totalEnergy = analysis.xAxis.energy +
                          analysis.yAxis.energy +
                          analysis.zAxis.energy;

    calculateRadialAxial(analysis.xAxis, analysis.yAxis,
                        analysis.radialComponent, analysis.axialComponent);

    analysis.crossCorrelationXY = calculateCrossCorrelation(
        procX->getBufferData(0), procY->getBufferData(0), WINDOW_SIZE);

    analysis.crossCorrelationXZ = calculateCrossCorrelation(
        procX->getBufferData(0), procZ->getBufferData(0), WINDOW_SIZE);

    analysis.crossCorrelationYZ = calculateCrossCorrelation(
        procY->getBufferData(0), procZ->getBufferData(0), WINDOW_SIZE);

    analysis.timestamp = millis();

    return analysis;
}

void MultiAxisAnalyzer::reset() {
    procX->reset();
    procY->reset();
    procZ->reset();
    sampleCount = 0;
}

bool MultiAxisAnalyzer::isReady() const {
    return sampleCount >= WINDOW_SIZE;
}

void MultiAxisAnalyzer::analyzeAxis(SignalProcessor* proc, AxisFeatures& features) {
    float spectrum[FFT_OUTPUT_SIZE];
    proc->getMagnitudeSpectrum(spectrum, FFT_OUTPUT_SIZE);

    extractor.extractAllFeatures(
        proc->getBufferData(0),
        WINDOW_SIZE,
        spectrum,
        FFT_OUTPUT_SIZE,
        proc,
        features.features
    );

    features.dominantFreq = proc->getDominantFrequency();

    features.energy = 0;
    for (size_t i = 0; i < FFT_OUTPUT_SIZE; i++) {
        features.energy += spectrum[i] * spectrum[i];
    }
}

float MultiAxisAnalyzer::calculateCrossCorrelation(const float* signal1,
                                                   const float* signal2,
                                                   size_t length) {
    float mean1 = 0, mean2 = 0;
    for (size_t i = 0; i < length; i++) {
        mean1 += signal1[i];
        mean2 += signal2[i];
    }
    mean1 /= length;
    mean2 /= length;

    float numerator = 0, denom1 = 0, denom2 = 0;
    for (size_t i = 0; i < length; i++) {
        float diff1 = signal1[i] - mean1;
        float diff2 = signal2[i] - mean2;
        numerator += diff1 * diff2;
        denom1 += diff1 * diff1;
        denom2 += diff2 * diff2;
    }

    float denominator = sqrt(denom1 * denom2);
    return (denominator != 0) ? (numerator / denominator) : 0;
}

void MultiAxisAnalyzer::calculateRadialAxial(const AxisFeatures& x,
                                             const AxisFeatures& y,
                                             float& radial, float& axial) {
    radial = sqrt(x.energy + y.energy);
    axial = 0;
}
