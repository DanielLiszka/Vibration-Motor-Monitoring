# Troubleshooting Guide

## Common Issues and Solutions

### Hardware Issues

#### 1. "MPU6050 initialization failed" or "Failed to find MPU6050 chip"

**Symptoms:**
- System stops at initialization
- Error message in serial output
- LED blinks rapidly

**Possible Causes & Solutions:**

✓ **Check Wiring**
```
Verify connections:
- MPU6050 VCC → ESP32 3.3V (NOT 5V!)
- MPU6050 GND → ESP32 GND
- MPU6050 SDA → ESP32 GPIO 21
- MPU6050 SCL → ESP32 GPIO 22
```

✓ **I2C Address**
```cpp
// The MPU6050 can have two addresses: 0x68 (default) or 0x69
// If AD0 pin is HIGH, address is 0x69
// Try running I2C scanner to detect address
```

✓ **Power Supply**
- MPU6050 requires 3.3V (damage possible if 5V used)
- Check if USB provides enough current
- Try external 3.3V power supply

✓ **Faulty Module**
- Test module with another microcontroller
- Check for cold solder joints
- Try a different MPU6050 module

**Diagnostic Commands:**
```cpp
// Add to setup() for debugging:
Wire.begin(21, 22);
Serial.println("Scanning I2C bus...");
for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
        Serial.printf("Found device at 0x%02X\n", addr);
    }
}
```

---

#### 2. Erratic Readings / Noisy Data

**Symptoms:**
- Vibration values jump around wildly
- Constant false fault alerts
- Unrealistic acceleration values

**Solutions:**

✓ **Secure Mounting**
```
- Sensor MUST be rigidly mounted
- Use hot glue, epoxy, or bolts
- Double-sided tape often insufficient
- Sensor should not vibrate independently
```

✓ **Wire Management**
```
- Secure wires to prevent wire vibration
- Keep wires away from motor power cables
- Use twisted pair for I2C if possible
- Add ferrite beads if electromagnetic interference suspected
```

✓ **Grounding**
```
- Ensure good ground connection
- ESP32 and motor should share ground
- Avoid ground loops
```

✓ **Filtering**
```cpp
// Try increasing low-pass filter in Config.h
#define MPU6050_BANDWIDTH MPU6050_BAND_10_HZ  // Lower = more filtering
```

---

#### 3. System Resets / Crashes

**Symptoms:**
- ESP32 reboots randomly
- "Brownout detector triggered" messages
- Watchdog timer resets

**Solutions:**

✓ **Power Issues**
```
- Use quality USB cable
- Try 2A power supply minimum
- Add 100µF capacitor across ESP32 power pins
- Check voltage with multimeter (should be 3.3V ±0.1V)
```

✓ **Memory Issues**
```cpp
// Check free heap in loop:
if (ESP.getFreeHeap() < 10000) {
    Serial.println("WARNING: Low memory!");
}

// Reduce buffer sizes if needed:
#define WINDOW_SIZE 128  // Instead of 256
```

✓ **Stack Overflow**
```cpp
// Reduce recursion or large local arrays
// Use heap allocation for large buffers
```

---

### Software Issues

#### 4. WiFi Connection Fails

**Symptoms:**
- "WiFi connection timeout"
- Never connects to network
- IP address shows 0.0.0.0

**Solutions:**

✓ **Credentials**
```cpp
// Double-check SSID and password in Config.h
#define WIFI_SSID "YourSSID"        // Case sensitive!
#define WIFI_PASSWORD "YourPassword" // Must be exact
```

✓ **Network Type**
```
- ESP32 only supports 2.4 GHz WiFi (NOT 5 GHz)
- Check router settings
- Ensure WPA2 encryption (WPA3 may not work)
```

✓ **Signal Strength**
```cpp
// Check signal strength:
Serial.println(WiFi.RSSI());
// Should be > -80 dBm for reliable connection
```

✓ **Disable WiFi (Temporary)**
```cpp
// In Config.h:
#define WIFI_ENABLED false  // Disable WiFi to test
```

---

#### 5. MQTT Connection Fails

**Symptoms:**
- WiFi connects but MQTT doesn't
- "MQTT connection failed, rc=X"

**Solutions:**

✓ **Return Codes:**
```
rc = -4: Connection timeout
rc = -3: Connection lost
rc = -2: Connect failed
rc = -1: Disconnected
rc = 1: Wrong protocol
rc = 2: Client ID rejected
rc = 3: Server unavailable
rc = 4: Bad credentials
rc = 5: Not authorized
```

✓ **Broker Settings**
```cpp
// Try public broker for testing:
#define MQTT_BROKER "test.mosquitto.org"
#define MQTT_PORT 1883

// Check if broker requires authentication
#define MQTT_USER ""
#define MQTT_PASSWORD ""
```

✓ **Firewall**
```
- Check if port 1883 is blocked
- Try different broker
- Test with mosquitto_sub on computer
```

---

#### 6. False Fault Detections

**Symptoms:**
- Constant fault alerts on known-good motor
- Every reading triggers fault
- Unrealistic fault classifications

**Solutions:**

✓ **Re-Calibrate**
```
1. Ensure motor is running normally
2. Remove any weights/intentional faults
3. Reset ESP32 to force recalibration
4. Wait for 100 baseline samples
5. Monitor for stable operation
```

✓ **Adjust Thresholds**
```cpp
// In Config.h, increase threshold multipliers:
#define THRESHOLD_MULTIPLIER_WARNING 3.0   // Instead of 2.0
#define THRESHOLD_MULTIPLIER_CRITICAL 5.0  // Instead of 3.0
```

✓ **Check Mounting**
```
- Sensor must not vibrate independently
- Mount directly on motor bearing housing
- Avoid mounting on panels or covers
```

✓ **Environmental Factors**
```
- Check for external vibrations (nearby equipment)
- Isolate from floor vibrations
- Avoid mounting near fans/AC units
```

---

#### 7. No Fault Detections (Known Fault Not Detected)

**Symptoms:**
- Obvious vibration not detected
- System shows "Normal" despite issues
- Anomaly scores always low

**Solutions:**

✓ **Increase Sensitivity**
```cpp
// Lower thresholds:
#define THRESHOLD_MULTIPLIER_WARNING 1.5   // More sensitive
#define THRESHOLD_MULTIPLIER_CRITICAL 2.0
```

✓ **Check Calibration**
```
- Was system calibrated with fault present?
- If yes, re-calibrate with fault removed
- Baseline may have included the fault condition
```

✓ **Verify Sensor Placement**
```
- Move sensor closer to fault source
- For bearing faults: mount near bearing
- For imbalance: mount on motor body
```

✓ **Check Frequency Bands**
```cpp
// Adjust bands for your motor speed:
// For 1800 RPM motor (30 Hz fundamental):
#define BAND_1_MAX 10   // Imbalance, misalignment
#define BAND_2_MAX 30   // Bearing faults
#define BAND_3_MAX 50   // High frequency
```

---

#### 8. High CPU Usage / Slow Performance

**Symptoms:**
- System can't keep up with 100 Hz sampling
- "Cannot keep up with real-time" message
- Missed samples counter increasing

**Solutions:**

✓ **Reduce Sampling Rate**
```cpp
#define SAMPLING_FREQUENCY_HZ 50  // Instead of 100
```

✓ **Smaller FFT Window**
```cpp
#define WINDOW_SIZE 128  // Instead of 256
```

✓ **Disable Features**
```cpp
#define WIFI_ENABLED false
#define MQTT_ENABLED false
#define LOG_TO_SERIAL false  // Reduces serial overhead
```

✓ **Optimize Code**
```cpp
// Reduce debug output:
#define DEBUG_ENABLED false
```

---

### Compilation Issues

#### 9. Compilation Errors

**"undefined reference to" errors:**
```bash
# Clean build and try again:
pio run --target clean
pio run
```

**Library not found:**
```bash
# Install dependencies:
pio lib install
```

**ESP32 board not found:**
```bash
# Update platform:
pio platform update
```

---

### Performance Monitoring

#### 10. Check System Health

**Add to main loop for diagnostics:**

```cpp
// In loop(), periodically print stats:
static uint32_t lastHealthCheck = 0;
if (millis() - lastHealthCheck > 10000) {  // Every 10 seconds
    Serial.println("\n=== SYSTEM HEALTH ===");
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
    Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    Serial.printf("Total Samples: %lu\n", totalSamples);
    Serial.printf("Faults Detected: %lu\n", faultsDetected);
    Serial.println("=====================\n");
    lastHealthCheck = millis();
}
```

---

## Diagnostic Tools

### Test Programs

1. **MPU6050 Test** (`test/test_mpu6050.cpp`)
```bash
# Upload test program:
pio run -e esp32dev --target upload
```

2. **FFT Benchmark** (`test/test_fft.cpp`)
```bash
# Tests FFT performance and accuracy
```

### Serial Commands

Add to `main.cpp` for interactive debugging:

```cpp
void loop() {
    // Check for serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        switch(cmd) {
            case 'r':  // Reset calibration
                faultDetect.reset();
                Serial.println("Calibration reset");
                break;
            case 's':  // Print status
                printSystemStatus();
                break;
            case 'm':  // Memory info
                Serial.printf("Free: %d bytes\n", ESP.getFreeHeap());
                break;
            case 'f':  // Force FFT display
                // Display spectrum
                break;
        }
    }
}
```

---

## Getting Help

If problems persist:

1. **Enable Debug Output**
```cpp
#define DEBUG_ENABLED true
```

2. **Capture Serial Output**
```bash
pio device monitor > debug.log
```

3. **Check Documentation**
- README.md - Main documentation
- API_REFERENCE.md - Code API
- GETTING_STARTED.md - Setup guide

4. **Report Issue**
- Include serial output
- Describe hardware setup
- List what you've tried
- Post on GitHub Issues

---

## Quick Reference

### Reset to Factory Settings

```cpp
// In setup(), force recalibration:
faultDetect.startCalibration(CALIBRATION_SAMPLES);
```

### Backup Configuration

Before making changes, save your Config.h:
```bash
cp include/Config.h include/Config.h.backup
```

### Restore Defaults

```bash
git checkout include/Config.h
```

---

## Preventive Maintenance

✓ Check connections monthly
✓ Verify mounting security weekly
✓ Monitor free heap memory
✓ Keep firmware updated
✓ Re-calibrate after motor maintenance
✓ Test with known faults periodically

---

**Remember:** This is a monitoring tool, not a safety device. Always follow proper industrial safety procedures!
