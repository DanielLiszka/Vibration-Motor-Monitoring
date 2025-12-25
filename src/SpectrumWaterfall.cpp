#include "SpectrumWaterfall.h"

SpectrumWaterfall::SpectrumWaterfall()
    : currentRow(0)
    , colorLevels(WATERFALL_COLOR_LEVELS)
    , maxValue(0)
    , minValue(0)
{
    memset(waterfallData, 0, sizeof(waterfallData));
}

SpectrumWaterfall::~SpectrumWaterfall() {
}

bool SpectrumWaterfall::begin() {
    DEBUG_PRINTLN("Initializing Spectrum Waterfall...");
    reset();
    return true;
}

void SpectrumWaterfall::addSpectrum(const float* spectrum, size_t length) {
    shiftData();

    updateMinMax(spectrum, length);

    for (size_t i = 0; i < WATERFALL_WIDTH && i < length / 2; i++) {
        size_t srcIdx = i * 2;
        float value = (spectrum[srcIdx] + spectrum[srcIdx + 1]) / 2.0f;
        waterfallData[currentRow][i] = mapToColor(value);
    }

    currentRow = (currentRow + 1) % WATERFALL_HEIGHT;
}

void SpectrumWaterfall::getWaterfallData(uint8_t* output, size_t& width, size_t& height) {
    width = WATERFALL_WIDTH;
    height = WATERFALL_HEIGHT;

    memcpy(output, waterfallData, sizeof(waterfallData));
}

void SpectrumWaterfall::reset() {
    memset(waterfallData, 0, sizeof(waterfallData));
    currentRow = 0;
    maxValue = 0;
    minValue = 0;
}

void SpectrumWaterfall::shiftData() {
}

uint8_t SpectrumWaterfall::mapToColor(float value) {
    if (maxValue == minValue) return 0;

    float normalized = (value - minValue) / (maxValue - minValue);
    normalized = constrain(normalized, 0.0f, 1.0f);

    return (uint8_t)(normalized * (colorLevels - 1));
}

void SpectrumWaterfall::updateMinMax(const float* spectrum, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (spectrum[i] > maxValue || maxValue == 0) {
            maxValue = spectrum[i];
        }
        if (spectrum[i] < minValue || minValue == 0) {
            minValue = spectrum[i];
        }
    }

    maxValue = maxValue * 0.99f + spectrum[0] * 0.01f;
    minValue = minValue * 0.99f + spectrum[0] * 0.01f;
}
