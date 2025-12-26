#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "Config.h"

struct AccelData {
    float x;
    float y;
    float z;
    uint32_t timestamp;
};

class MPU6050Driver {
public:

    MPU6050Driver();

    ~MPU6050Driver();

    bool begin();

    bool readAcceleration(AccelData& data);

    bool readRawAcceleration(int16_t& x, int16_t& y, int16_t& z);

    bool setAccelRange(uint8_t range);

    bool setFilterBandwidth(mpu6050_bandwidth_t bandwidth);

    bool selfTest();

    bool calibrate(uint16_t numSamples = 100);

    bool isConnected();

    float getTemperature();

    void setMotionInterrupt(bool enable, uint8_t threshold = 10);

    String getLastError() const { return lastError; }

private:
    Adafruit_MPU6050 mpu;

    float offsetX;
    float offsetY;
    float offsetZ;

    bool initialized;
    String lastError;

    void applyCalibration(AccelData& data);
};

#endif
