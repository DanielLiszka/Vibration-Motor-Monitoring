#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FIRMWARE_VERSION "2.0.0"
#define DEVICE_NAME "VibeSentry"
#define DEVICE_ID "vibesentry-001"

#define MPU6050_I2C_ADDRESS 0x68
#define MPU6050_SDA_PIN 21
#define MPU6050_SCL_PIN 22
#define MPU6050_INT_PIN 19

#define SAMPLING_FREQUENCY_HZ 100
#define SAMPLING_PERIOD_MS (1000 / SAMPLING_FREQUENCY_HZ)
#define WINDOW_SIZE 256
#define OVERLAP_PERCENTAGE 50

#define ACCEL_RANGE_G 8

#define FFT_SIZE WINDOW_SIZE
#define FFT_OUTPUT_SIZE (FFT_SIZE / 2)

#define BAND_1_MIN 0
#define BAND_1_MAX 10
#define BAND_2_MIN 10
#define BAND_2_MAX 30
#define BAND_3_MIN 30
#define BAND_3_MAX 50

#define NUM_TIME_FEATURES 6
#define NUM_FREQ_FEATURES 4
#define NUM_TOTAL_FEATURES (NUM_TIME_FEATURES + NUM_FREQ_FEATURES)

enum TimeFeature {
    FEAT_RMS = 0,
    FEAT_PEAK_TO_PEAK,
    FEAT_KURTOSIS,
    FEAT_SKEWNESS,
    FEAT_CREST_FACTOR,
    FEAT_VARIANCE
};

enum FreqFeature {
    FEAT_SPECTRAL_CENTROID = 6,
    FEAT_SPECTRAL_SPREAD,
    FEAT_BAND_POWER_RATIO,
    FEAT_DOMINANT_FREQ
};

#define THRESHOLD_MULTIPLIER_WARNING 2.0
#define THRESHOLD_MULTIPLIER_CRITICAL 3.0

#define CALIBRATION_SAMPLES 100
#define CALIBRATION_DURATION_MS (CALIBRATION_SAMPLES * SAMPLING_PERIOD_MS)

enum FaultType {
    FAULT_NONE = 0,
    FAULT_IMBALANCE,
    FAULT_MISALIGNMENT,
    FAULT_BEARING,
    FAULT_LOOSENESS,
    FAULT_UNKNOWN
};

enum SeverityLevel {
    SEVERITY_NORMAL = 0,
    SEVERITY_WARNING,
    SEVERITY_CRITICAL
};

#define LOG_TO_SERIAL true
#define LOG_TO_FLASH false
#define LOG_BUFFER_SIZE 100

#define LOG_INTERVAL_MS 1000
#define ALERT_COOLDOWN_MS 5000

#define WIFI_ENABLED false
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "your_password"
#define WIFI_TIMEOUT_MS 10000

#define MQTT_ENABLED false
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID DEVICE_NAME
#define MQTT_USER ""
#define MQTT_PASSWORD ""

#define MQTT_TOPIC_STATUS "motor/status"
#define MQTT_TOPIC_VIBRATION "motor/vibration"
#define MQTT_TOPIC_FAULT "motor/fault"
#define MQTT_TOPIC_FEATURES "motor/features"
#define MQTT_TOPIC_COMMAND "motor/command"

#define MQTT_BROKER_ADDRESS MQTT_BROKER
#define MQTT_BROKER_PORT MQTT_PORT

#define OTA_ENABLED false
#define OTA_PASSWORD ""
#define OTA_HOSTNAME DEVICE_NAME

#define VOLTAGE_NOMINAL 220.0f
#define CURRENT_NOMINAL 5.0f
#define POWER_FACTOR 0.85f

#define LED_STATUS_PIN 2
#define LED_FAULT_PIN 13

#define DEBUG_ENABLED true
#define DEBUG_SERIAL Serial
#define DEBUG_BAUD_RATE 115200

#if DEBUG_ENABLED
    #define DEBUG_PRINT(x) DEBUG_SERIAL.print(x)
    #define DEBUG_PRINTLN(x) DEBUG_SERIAL.println(x)
    #define DEBUG_PRINTF(fmt, ...) DEBUG_SERIAL.printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(fmt, ...)
#endif

#define USE_PSRAM false
#define STACK_SIZE 8192

#endif
