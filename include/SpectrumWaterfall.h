#ifndef SPECTRUM_WATERFALL_H
#define SPECTRUM_WATERFALL_H

#include <Arduino.h>
#include "Config.h"

#define WATERFALL_HEIGHT 64
#define WATERFALL_WIDTH (FFT_OUTPUT_SIZE / 2)
#define WATERFALL_COLOR_LEVELS 8

class SpectrumWaterfall {
public:
    SpectrumWaterfall();
    ~SpectrumWaterfall();

    bool begin();

    void addSpectrum(const float* spectrum, size_t length);

    void getWaterfallData(uint8_t* output, size_t& width, size_t& height);

    void reset();

    void setColorMap(uint8_t levels) { colorLevels = levels; }

    uint8_t getHeight() const { return WATERFALL_HEIGHT; }
    uint8_t getWidth() const { return WATERFALL_WIDTH; }

private:
    uint8_t waterfallData[WATERFALL_HEIGHT][WATERFALL_WIDTH];
    uint32_t currentRow;
    uint8_t colorLevels;

    float maxValue;
    float minValue;

    void shiftData();
    uint8_t mapToColor(float value);
    void updateMinMax(const float* spectrum, size_t length);
};

#endif
