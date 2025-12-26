# API Reference

## Table of Contents

- [MPU6050Driver](#mpu6050driver)
- [SignalProcessor](#signalprocessor)
- [FeatureExtractor](#featureextractor)
- [FaultDetector](#faultdetector)
- [DataLogger](#datalogger)
- [WiFiManager](#wifimanager)

---

## MPU6050Driver

Manages communication with the MPU6050 accelerometer/gyroscope sensor.

### Constructor

```cpp
MPU6050Driver()
```

### Public Methods

#### begin()
```cpp
bool begin()
```
Initialize the MPU6050 sensor.

**Returns**: `true` if initialization successful, `false` otherwise

**Example**:
```cpp
MPU6050Driver sensor;
if (sensor.begin()) {
    Serial.println("Sensor initialized!");
}
```

---

#### readAcceleration()
```cpp
bool readAcceleration(AccelData& data)
```
Read acceleration data from all axes.

**Parameters**:
- `data`: Reference to AccelData structure to store results

**Returns**: `true` if read successful, `false` otherwise

**Example**:
```cpp
AccelData accel;
if (sensor.readAcceleration(accel)) {
    Serial.printf("X: %.2f, Y: %.2f, Z: %.2f\n",
                  accel.x, accel.y, accel.z);
}
```

---

#### calibrate()
```cpp
bool calibrate(uint16_t numSamples = 100)
```
Calibrate the sensor by removing DC offset.

**Parameters**:
- `numSamples`: Number of samples for calibration (default: 100)

**Returns**: `true` if calibration successful, `false` otherwise

---

#### getTemperature()
```cpp
float getTemperature()
```
Get sensor temperature in Celsius.

**Returns**: Temperature value

---

## SignalProcessor

Performs FFT and signal processing operations.

### Constructor

```cpp
SignalProcessor()
```

### Public Methods

#### begin()
```cpp
bool begin()
```
Initialize the signal processor.

**Returns**: `true` if successful, `false` otherwise

---

#### addSample()
```cpp
bool addSample(float value, uint8_t axis = 0)
```
Add a sample to the processing buffer.

**Parameters**:
- `value`: Acceleration value
- `axis`: Axis identifier (0=X, 1=Y, 2=Z)

**Returns**: `true` if buffer is full and ready for processing

**Example**:
```cpp
SignalProcessor proc;
AccelData accel;

while (true) {
    sensor.readAcceleration(accel);
    if (proc.addSample(accel.x, 0)) {
        proc.performFFT(0);
        break;
    }
    delay(10);
}
```

---

#### performFFT()
```cpp
bool performFFT(uint8_t axis = 0)
```
Perform FFT on the current buffer.

**Parameters**:
- `axis`: Axis to process (0=X, 1=Y, 2=Z)

**Returns**: `true` if FFT successful, `false` otherwise

---

#### getMagnitudeSpectrum()
```cpp
bool getMagnitudeSpectrum(float* spectrum, size_t size)
```
Get FFT magnitude spectrum.

**Parameters**:
- `spectrum`: Pointer to array to store magnitude spectrum
- `size`: Size of output array (should be FFT_SIZE/2)

**Returns**: `true` if successful, `false` otherwise

---

#### getDominantFrequency()
```cpp
float getDominantFrequency()
```
Get dominant frequency in the signal.

**Returns**: Dominant frequency in Hz

---

#### getBandPower()
```cpp
float getBandPower(float minFreq, float maxFreq)
```
Calculate power in a frequency band.

**Parameters**:
- `minFreq`: Minimum frequency (Hz)
- `maxFreq`: Maximum frequency (Hz)

**Returns**: Total power in the band

---

## FeatureExtractor

Extracts statistical features from vibration signals.

### Public Methods

#### extractTimeFeatures()
```cpp
bool extractTimeFeatures(const float* signal, size_t length,
                         FeatureVector& features)
```
Extract time-domain features from signal.

**Parameters**:
- `signal`: Input signal array
- `length`: Signal length
- `features`: Output feature vector

**Returns**: `true` if successful, `false` otherwise

---

#### extractFreqFeatures()
```cpp
bool extractFreqFeatures(const float* spectrum, size_t length,
                        SignalProcessor* processor, FeatureVector& features)
```
Extract frequency-domain features from spectrum.

**Parameters**:
- `spectrum`: Magnitude spectrum
- `length`: Spectrum length
- `processor`: Signal processor for frequency calculations
- `features`: Output feature vector

**Returns**: `true` if successful, `false` otherwise

---

#### Static Feature Calculation Methods

```cpp
static float calculateRMS(const float* signal, size_t length)
static float calculatePeakToPeak(const float* signal, size_t length)
static float calculateMean(const float* signal, size_t length)
static float calculateVariance(const float* signal, size_t length, float mean = 0.0f)
static float calculateKurtosis(const float* signal, size_t length)
static float calculateSkewness(const float* signal, size_t length)
static float calculateCrestFactor(const float* signal, size_t length)
```

**Example**:
```cpp
float signal[256] = {  };
float rms = FeatureExtractor::calculateRMS(signal, 256);
float kurtosis = FeatureExtractor::calculateKurtosis(signal, 256);
```

---

## FaultDetector

Detects anomalies and classifies fault types.

### Public Methods

#### begin()
```cpp
bool begin()
```
Initialize the fault detector.

**Returns**: `true` if successful, `false` otherwise

---

#### startCalibration()
```cpp
void startCalibration(uint32_t numSamples = CALIBRATION_SAMPLES)
```
Start baseline calibration.

**Parameters**:
- `numSamples`: Number of samples to collect for baseline

---

#### addCalibrationSample()
```cpp
bool addCalibrationSample(const FeatureVector& features)
```
Add a sample during calibration.

**Parameters**:
- `features`: Feature vector

**Returns**: `true` if calibration complete, `false` if more samples needed

**Example**:
```cpp
FaultDetector detector;
detector.startCalibration(100);

for (int i = 0; i < 100; i++) {
    FeatureVector features;

    if (detector.addCalibrationSample(features)) {
        Serial.println("Calibration complete!");
        break;
    }
}
```

---

#### detectFault()
```cpp
bool detectFault(const FeatureVector& features, FaultResult& result)
```
Detect faults in feature vector.

**Parameters**:
- `features`: Input feature vector
- `result`: Output fault result

**Returns**: `true` if fault detected, `false` if normal

**Example**:
```cpp
FeatureVector features;
FaultResult result;

if (detector.detectFault(features, result)) {
    Serial.printf("Fault detected: %s\n", result.getFaultTypeName());
    Serial.printf("Severity: %s\n", result.getSeverityName());
    result.print();
}
```

---

#### setThresholds()
```cpp
void setThresholds(float warningMultiplier, float criticalMultiplier)
```
Set detection thresholds.

**Parameters**:
- `warningMultiplier`: Warning threshold multiplier (default: 2.0)
- `criticalMultiplier`: Critical threshold multiplier (default: 3.0)

---

## DataLogger

Logs vibration data and alerts.

### Public Methods

#### begin()
```cpp
bool begin()
```
Initialize the data logger.

**Returns**: `true` if successful, `false` otherwise

---

#### log()
```cpp
void log(const FeatureVector& features, const FaultResult& fault,
         float temperature = 0.0f)
```
Log a data entry.

**Parameters**:
- `features`: Feature vector
- `fault`: Fault detection result
- `temperature`: Sensor temperature

---

#### logAlert()
```cpp
void logAlert(const FaultResult& fault)
```
Log fault alert.

**Parameters**:
- `fault`: Fault result

---

## WiFiManager

Manages WiFi and MQTT connectivity.

### Public Methods

#### begin()
```cpp
bool begin()
```
Initialize WiFi and MQTT.

**Returns**: `true` if successful, `false` otherwise

---

#### connectWiFi()
```cpp
bool connectWiFi(const char* ssid, const char* password,
                 uint32_t timeoutMs = WIFI_TIMEOUT_MS)
```
Connect to WiFi network.

**Parameters**:
- `ssid`: Network SSID
- `password`: Network password
- `timeoutMs`: Connection timeout in milliseconds

**Returns**: `true` if connected, `false` otherwise

---

#### publishFault()
```cpp
bool publishFault(const FaultResult& fault)
```
Publish fault alert to MQTT.

**Parameters**:
- `fault`: Fault result

**Returns**: `true` if published, `false` otherwise

**Example**:
```cpp
WiFiManager wifi;
wifi.begin();

FaultResult fault;

if (wifi.isMQTTConnected()) {
    wifi.publishFault(fault);
}
```

---

#### loop()
```cpp
void loop()
```
Maintain WiFi/MQTT connections. Call this regularly in main loop.

---

## Data Structures

### AccelData

```cpp
struct AccelData {
    float x;
    float y;
    float z;
    uint32_t timestamp;
};
```

### FeatureVector

```cpp
struct FeatureVector {
    float rms;
    float peakToPeak;
    float kurtosis;
    float skewness;
    float crestFactor;
    float variance;

    float spectralCentroid;
    float spectralSpread;
    float bandPowerRatio;
    float dominantFrequency;

    uint32_t timestamp;

    void print() const;
    void toArray(float* arr) const;
};
```

### FaultResult

```cpp
struct FaultResult {
    FaultType type;
    SeverityLevel severity;
    float confidence;
    float anomalyScore;
    String description;
    uint32_t timestamp;

    void print() const;
    const char* getFaultTypeName() const;
    const char* getSeverityName() const;
};
```

### Enums

```cpp
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
```

---

## Configuration Constants

All defined in `include/Config.h`:

```cpp
#define SAMPLING_FREQUENCY_HZ 100
#define WINDOW_SIZE 256
#define FFT_SIZE WINDOW_SIZE

#define THRESHOLD_MULTIPLIER_WARNING 2.0
#define THRESHOLD_MULTIPLIER_CRITICAL 3.0
#define CALIBRATION_SAMPLES 100

#define NUM_TIME_FEATURES 6
#define NUM_FREQ_FEATURES 4
#define NUM_TOTAL_FEATURES 10
```
