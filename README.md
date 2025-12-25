# Motor Vibration Fault Detection System

An ESP32-based system that monitors motor vibrations in real time and detects faults before they cause failures. It performs windowed FFT analysis, extracts statistical features, learns a baseline during calibration, and raises alerts when vibration patterns deviate from normal.

## What It Does

The system continuously samples vibration data from an MPU6050 accelerometer, runs FFT analysis, extracts statistical features, and compares them against a learned baseline. When vibration patterns deviate from normal, it classifies the likely fault type and raises alerts.

Detectable fault types:
- Rotor imbalance (uneven weight distribution)
- Shaft misalignment (coupling issues)
- Bearing defects (wear, damage, lubrication problems)
- Mechanical looseness (loose bolts, worn fits)

## Hardware

**Required:**
- ESP32 DevKit v1
- MPU6050 accelerometer module
- 5V power supply (USB is sufficient for most setups)
- Jumper wires

**Wiring:**
```
MPU6050     ESP32
-------     -----
VCC    -->  3.3V
GND    -->  GND
SDA    -->  GPIO 21
SCL    -->  GPIO 22
```

## Quick Start

1. Install PlatformIO (VSCode extension or CLI)

2. Clone and configure:
```bash
git clone https://github.com/DanielLiszka/Embedded_Project.git
cd Embedded_Project
```

3. Optional: enable network features in `include/Config.h` (disabled by default) and set your WiFi credentials:
```cpp
#define WIFI_ENABLED true
#define WIFI_SSID "your_network"
#define WIFI_PASSWORD "your_password"
```

4. Build and upload:
```bash
pio run -e esp32dev -t upload
pio device monitor
```
If `pio` is not on your PATH, run the same commands via `python -m platformio`.

5. Let the system run through calibration (about a minute with the motor running normally), then it starts monitoring.

## How It Works

**Sampling:** Reads accelerometer at 100 Hz, buffers 256 samples (2.56 seconds of data).

**Analysis:** Applies Hanning window, runs FFT, extracts magnitude spectrum.

**Features:** Calculates 10 statistical features from the signal:
- Time domain: RMS, peak-to-peak, kurtosis, skewness, crest factor, variance
- Frequency domain: spectral centroid, spectral spread, band power ratio, dominant frequency

**Detection:** Compares current features against the calibrated baseline using normalized Euclidean distance. Anything beyond 2 standard deviations triggers a warning, beyond 3 is critical.

**Classification:** Uses rule-based logic and an on-device ML classifier to identify the likely fault type based on characteristic patterns (e.g., bearing faults often show elevated kurtosis and higher-frequency content).

## Network Features

The system includes optional connectivity features that can be enabled/disabled in `include/Config.h`:

**Web Dashboard** - Hit the ESP32's IP address in a browser to see real-time spectrum visualization, feature values, and fault status. Uses WebSockets for live updates.

**MQTT** - Publishes to standard topics for integration with home automation, industrial systems, or cloud platforms:
- `motor/features` - Feature vectors
- `motor/fault` - Fault alerts
- `motor/status` - System status
- `motor/spectrum` - Frequency data

**OTA Updates** - Push firmware updates over WiFi instead of plugging in a cable every time.

**Webhook Notifications** - Send alerts to Slack, Discord, or any HTTP endpoint when faults are detected.

## Configuration

Everything lives in `include/Config.h`. Key settings:

```cpp
#define SAMPLING_FREQUENCY_HZ 100
#define WINDOW_SIZE 256
#define THRESHOLD_MULTIPLIER_WARNING 2.0
#define THRESHOLD_MULTIPLIER_CRITICAL 3.0
#define CALIBRATION_SAMPLES 100
```

For different motor sizes, check the preset configs in `examples/motor_configs/`.

## Project Structure

```
├── include/          # Header files
├── src/              # Implementation files
├── test/             # Test utilities
├── examples/         # Example configurations
├── scripts/          # Python tools for monitoring
└── docs/             # Additional documentation
```

Main components:
- `MPU6050Driver` - Sensor communication
- `SignalProcessor` - FFT and windowing
- `FeatureExtractor` - Statistical feature calculation
- `FaultDetector` - Anomaly detection and classification
- `EdgeML` - Neural network classifier
- `WebServer` - Dashboard and API
- `MQTTManager` - IoT messaging
- `TrendAnalyzer` - Long-term pattern tracking
- `MaintenanceScheduler` - Predictive maintenance recommendations

## Testing

The `test/` folder has utilities for verifying the sensor and FFT are working correctly.

For testing fault detection without an actual faulty motor:
- Imbalance: Attach a small weight to something spinning
- Looseness: Loosen mounting screws
- Bearing fault: Hard to simulate, but sharp impacts registered by the accelerometer should trigger high kurtosis alerts

## Limitations

- Calibration needs to happen with the motor actually running normally. Bad baseline = bad detection.
- The rule-based classifier works well for common fault signatures, but real-world vibration patterns can be more complex.
- Single-point measurement. Industrial systems often use multiple sensors at different locations.
- No bearing-specific frequency calculation (requires knowing bearing geometry and RPM).

## References

- ISO 10816 for vibration severity standards
- Cooley-Tukey FFT algorithm
- Various papers on vibration-based condition monitoring (the field has decades of research)

## License

MIT
