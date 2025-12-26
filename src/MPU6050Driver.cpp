#include "MPU6050Driver.h"

MPU6050Driver::MPU6050Driver()
    : offsetX(0.0f)
    , offsetY(0.0f)
    , offsetZ(0.0f)
    , initialized(false)
    , lastError("")
{
}

MPU6050Driver::~MPU6050Driver() {
}

bool MPU6050Driver::begin() {
    DEBUG_PRINTLN("Initializing MPU6050...");

    Wire.begin(MPU6050_SDA_PIN, MPU6050_SCL_PIN);
    Wire.setClock(400000);

    if (!mpu.begin(MPU6050_I2C_ADDRESS, &Wire)) {
        lastError = "Failed to find MPU6050 chip";
        DEBUG_PRINTLN(lastError);
        return false;
    }

    DEBUG_PRINTLN("MPU6050 Found!");

    if (!setAccelRange(ACCEL_RANGE_G)) {
        lastError = "Failed to set accelerometer range";
        DEBUG_PRINTLN(lastError);
        return false;
    }

    if (!setFilterBandwidth(MPU6050_BAND_21_HZ)) {
        lastError = "Failed to set filter bandwidth";
        DEBUG_PRINTLN(lastError);
        return false;
    }

    if (!selfTest()) {
        lastError = "Self-test failed";
        DEBUG_PRINTLN(lastError);
        return false;
    }

    DEBUG_PRINTLN("Calibrating sensor (keep still)...");
    if (!calibrate(100)) {
        lastError = "Calibration failed";
        DEBUG_PRINTLN(lastError);
        return false;
    }

    initialized = true;
    DEBUG_PRINTLN("MPU6050 initialized successfully!");
    return true;
}

bool MPU6050Driver::readAcceleration(AccelData& data) {
    if (!initialized) {
        lastError = "Sensor not initialized";
        return false;
    }

    sensors_event_t accel, gyro, temp;
    if (!mpu.getEvent(&accel, &gyro, &temp)) {
        lastError = "Failed to read sensor data";
        return false;
    }

    data.x = accel.acceleration.x;
    data.y = accel.acceleration.y;
    data.z = accel.acceleration.z;
    data.timestamp = millis();

    applyCalibration(data);

    return true;
}

bool MPU6050Driver::readRawAcceleration(int16_t& x, int16_t& y, int16_t& z) {
    if (!initialized) {
        lastError = "Sensor not initialized";
        return false;
    }

    AccelData data;
    if (!readAcceleration(data)) {
        return false;
    }

    x = (int16_t)(data.x * 1000);
    y = (int16_t)(data.y * 1000);
    z = (int16_t)(data.z * 1000);

    return true;
}

bool MPU6050Driver::setAccelRange(uint8_t range) {
    mpu6050_accel_range_t accelRange;

    switch(range) {
        case 2:
            accelRange = MPU6050_RANGE_2_G;
            break;
        case 4:
            accelRange = MPU6050_RANGE_4_G;
            break;
        case 8:
            accelRange = MPU6050_RANGE_8_G;
            break;
        case 16:
            accelRange = MPU6050_RANGE_16_G;
            break;
        default:
            lastError = "Invalid accelerometer range";
            return false;
    }

    mpu.setAccelerometerRange(accelRange);
    DEBUG_PRINTF("Accelerometer range set to: %dG\n", range);
    return true;
}

bool MPU6050Driver::setFilterBandwidth(mpu6050_bandwidth_t bandwidth) {
    mpu.setFilterBandwidth(bandwidth);
    DEBUG_PRINTLN("Filter bandwidth configured");
    return true;
}

bool MPU6050Driver::selfTest() {

    AccelData data;
    for (int i = 0; i < 5; i++) {
        if (!readAcceleration(data)) {
            return false;
        }
        delay(10);
    }

    DEBUG_PRINTLN("Self-test passed");
    return true;
}

bool MPU6050Driver::calibrate(uint16_t numSamples) {
    if (!initialized && !mpu.begin(MPU6050_I2C_ADDRESS, &Wire)) {
        lastError = "Cannot calibrate - sensor not found";
        return false;
    }

    float sumX = 0, sumY = 0, sumZ = 0;
    uint16_t validSamples = 0;

    DEBUG_PRINTF("Calibrating with %d samples...\n", numSamples);

    for (uint16_t i = 0; i < numSamples; i++) {
        sensors_event_t accel, gyro, temp;
        if (mpu.getEvent(&accel, &gyro, &temp)) {
            sumX += accel.acceleration.x;
            sumY += accel.acceleration.y;
            sumZ += accel.acceleration.z;
            validSamples++;
        }
        delay(10);
    }

    if (validSamples == 0) {
        lastError = "No valid samples during calibration";
        return false;
    }

    offsetX = sumX / validSamples;
    offsetY = sumY / validSamples;
    offsetZ = (sumZ / validSamples) - 9.81;

    DEBUG_PRINTF("Calibration complete. Offsets: X=%.3f, Y=%.3f, Z=%.3f\n",
                 offsetX, offsetY, offsetZ);

    return true;
}

bool MPU6050Driver::isConnected() {

    uint8_t whoami = 0;
    Wire.beginTransmission(MPU6050_I2C_ADDRESS);
    Wire.write(0x75);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6050_I2C_ADDRESS, (uint8_t)1);

    if (Wire.available()) {
        whoami = Wire.read();
    }

    return (whoami == 0x68 || whoami == 0x72);
}

float MPU6050Driver::getTemperature() {
    if (!initialized) {
        return 0.0f;
    }

    sensors_event_t accel, gyro, temp;
    if (mpu.getEvent(&accel, &gyro, &temp)) {
        return temp.temperature;
    }

    return 0.0f;
}

void MPU6050Driver::setMotionInterrupt(bool enable, uint8_t threshold) {
    if (!initialized) {
        return;
    }

    mpu.setMotionDetectionThreshold(threshold);
    mpu.setMotionDetectionDuration(20);

    if (enable) {
        mpu.setInterruptPinLatch(true);
        mpu.setInterruptPinPolarity(true);
        mpu.setMotionInterrupt(true);
        DEBUG_PRINTLN("Motion interrupt enabled");
    } else {
        mpu.setMotionInterrupt(false);
        DEBUG_PRINTLN("Motion interrupt disabled");
    }
}

void MPU6050Driver::applyCalibration(AccelData& data) {
    data.x -= offsetX;
    data.y -= offsetY;
    data.z -= offsetZ;
}
