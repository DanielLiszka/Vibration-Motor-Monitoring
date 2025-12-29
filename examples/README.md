
# Examples Directory

This directory contains example programs and configurations for Motor Vibration Monitor.

## Example Programs

### 1. Spectrum Plotter (`spectrum_plotter.cpp`)

Real-time FFT spectrum visualization using Arduino/PlatformIO Serial Plotter.

**Features:**
- Live frequency spectrum display
- 64-bin downsampled spectrum
- Exponential smoothing for stable visualization
- 10 Hz update rate

**How to use:**
```bash
# Copy to src/main.cpp
cp examples/spectrum_plotter.cpp src/main.cpp

# Build and upload
pio run --target upload

# Open Serial Plotter
pio device monitor --filter send_on_enter
# Or use Arduino IDE: Tools -> Serial Plotter
```

**What to look for:**
- **Imbalance**: Single peak at 1X RPM frequency
- **Misalignment**: Peaks at 2X and 3X RPM
- **Bearing fault**: Multiple high-frequency peaks
- **Looseness**: Broadband energy across spectrum

---

## Motor Configuration Files

Pre-configured settings for different motor types. Include these in your main project to override default settings.

### Small Motor (`motor_configs/small_motor.h`)

**For:**
- Small fans (< 1 HP)
- Conveyor motors
- Small pumps
- Operating speed: 1800 RPM

**Characteristics:**
- Lower sampling rate (100 Hz)
- Smaller FFT window (256 samples)
- More sensitive thresholds
- Frequency bands: 0-20 Hz, 20-50 Hz, 50-100 Hz

**Usage:**
```cpp
#include "motor_configs/small_motor.h"
```

---

### Large Motor (`motor_configs/large_motor.h`)

**For:**
- Industrial pumps (> 10 HP)
- Compressors
- Large HVAC fans
- Operating speed: 1200-3600 RPM

**Characteristics:**
- Higher sampling rate (200 Hz)
- Larger FFT window (512 samples)
- Extended frequency bands
- Bearing-specific fault frequencies
- More calibration samples (200)

**Usage:**
```cpp
#include "motor_configs/large_motor.h"
```

---

### High-Speed Motor (`motor_configs/high_speed_motor.h`)

**For:**
- Spindles (> 3600 RPM)
- Turbines
- High-speed fans
- Operating speed: 3600-10000 RPM

**Characteristics:**
- Maximum sampling rate (400 Hz)
- Extended frequency range (0-400 Hz)
- 16G accelerometer range
- Tighter tolerances
- Extra calibration (300 samples)

**Usage:**
```cpp
#include "motor_configs/high_speed_motor.h"
```

---

## Creating Custom Configurations

1. **Copy a template:**
```bash
cp examples/motor_configs/small_motor.h examples/motor_configs/my_motor.h
```

2. **Edit parameters:**
```cpp
#define MOTOR_TYPE "Custom Motor"
#define MOTOR_RATED_RPM 2400
#define MOTOR_RATED_HP 5
#define MOTOR_POLES 3

#define MOTOR_FUNDAMENTAL_HZ (MOTOR_RATED_RPM / 60.0f)

#define SAMPLING_FREQUENCY_HZ 150

#define THRESHOLD_MULTIPLIER_WARNING 2.0
#define THRESHOLD_MULTIPLIER_CRITICAL 3.5
```

3. **Include in project:**
```cpp
#include "motor_configs/my_motor.h"
```

---

## Frequency Calculation Examples

### 1X RPM (Fundamental Frequency)
```cpp
float freq_1x = 1800.0 / 60.0;
```

### Bearing Fault Frequencies

For a bearing with:
- **Nb** = Number of balls (8)
- **Bd** = Ball diameter (10 mm)
- **Pd** = Pitch diameter (50 mm)
- **φ** = Contact angle (15°)
- **fr** = Rotational frequency (30 Hz for 1800 RPM)

```cpp
float bpfo = (Nb / 2.0) * fr * (1 - (Bd/Pd) * cos(phi));

float bpfi = (Nb / 2.0) * fr * (1 + (Bd/Pd) * cos(phi));

float bsf = (Pd / (2*Bd)) * fr * (1 - pow(Bd/Pd, 2) * pow(cos(phi), 2));

float ftf = (fr / 2.0) * (1 - (Bd/Pd) * cos(phi));
```

### Blade Pass Frequency

For a fan with 6 blades at 1800 RPM:
```cpp
float bpf = (1800.0 / 60.0) * 6;
```

---

## Testing Configurations

### Verify Configuration

```cpp
void setup() {
    Serial.begin(115200);

    Serial.println("Configuration Check:");
    Serial.printf("Motor Type: %s\n", MOTOR_TYPE);
    Serial.printf("Rated RPM: %d\n", MOTOR_RATED_RPM);
    Serial.printf("Fundamental: %.1f Hz\n", MOTOR_FUNDAMENTAL_HZ);
    Serial.printf("Sampling Rate: %d Hz\n", SAMPLING_FREQUENCY_HZ);
    Serial.printf("FFT Size: %d\n", FFT_SIZE);
    Serial.printf("Frequency Resolution: %.2f Hz\n",
                  (float)SAMPLING_FREQUENCY_HZ / FFT_SIZE);
    Serial.printf("Max Frequency: %.1f Hz\n",
                  (float)SAMPLING_FREQUENCY_HZ / 2.0);

    Serial.println("\nFrequency Bands:");
    Serial.printf("  Band 1: 0-%d Hz\n", BAND_1_MAX);
    Serial.printf("  Band 2: %d-%d Hz\n", BAND_2_MIN, BAND_2_MAX);
    Serial.printf("  Band 3: %d-%d Hz\n", BAND_3_MIN, BAND_3_MAX);

    Serial.println("\nThresholds:");
    Serial.printf("  Warning: %.1fx baseline\n", THRESHOLD_MULTIPLIER_WARNING);
    Serial.printf("  Critical: %.1fx baseline\n", THRESHOLD_MULTIPLIER_CRITICAL);
}
```

---

## Example Workflow

### 1. Identify Motor Type
- Check motor nameplate for RPM, HP
- Determine operating speed range
- Note number of poles

### 2. Select Configuration
- Small motor: < 1 HP, < 1800 RPM
- Large motor: > 10 HP, 1200-3600 RPM
- High-speed: > 3600 RPM

### 3. Customize if Needed
- Adjust sampling rate
- Tune frequency bands
- Set threshold sensitivity

### 4. Test and Validate
- Run spectrum plotter
- Verify fundamental frequency matches RPM
- Check for expected harmonics
- Adjust calibration samples if needed

### 5. Deploy
- Include config in main.cpp
- Calibrate with motor running normally
- Monitor for 24-48 hours
- Fine-tune thresholds based on results

---

## Additional Resources

- **Vibration Analysis Handbook** - For fault frequency tables
- **ISO 10816** - Vibration standards for rotating machinery
- **Bearing Manufacturer Datasheets** - For bearing-specific frequencies

---

## Contributing

To contribute a new motor configuration:

1. Create config file in `motor_configs/`
2. Test thoroughly with actual motor
3. Document motor specifications
4. Include frequency calculations
5. Submit pull request with results

---

## Questions?

See the main [README.md](../README.md) and [TROUBLESHOOTING.md](../docs/TROUBLESHOOTING.md) for more information.
