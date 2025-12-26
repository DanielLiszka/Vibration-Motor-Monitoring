# Getting Started Guide

## Step-by-Step Setup

### 1. Hardware Assembly

#### Materials Needed
- ESP32 development board
- MPU6050 sensor module
- 4 female-to-female jumper wires
- USB cable (for ESP32)
- Motor to monitor (for testing)

#### Wiring Instructions

1. **Power Connections**
   - Connect MPU6050 VCC to ESP32 3.3V pin
   - Connect MPU6050 GND to ESP32 GND pin

2. **I2C Communication**
   - Connect MPU6050 SDA to ESP32 GPIO 21
   - Connect MPU6050 SCL to ESP32 GPIO 22

3. **Optional Interrupt Pin**
   - Connect MPU6050 INT to ESP32 GPIO 19

#### Mounting the Sensor

**Critical**: Proper sensor mounting is essential for accurate readings!

- Mount MPU6050 directly on motor housing or bearing cap
- Use strong double-sided tape or bolts
- Ensure sensor cannot vibrate independently
- Orient sensor consistently (note X, Y, Z axes)
- Keep wires secured to prevent wire vibration

### 2. Software Installation

#### Install PlatformIO

**Option A: VSCode Extension**
1. Install Visual Studio Code
2. Open Extensions (Ctrl+Shift+X)
3. Search for "PlatformIO IDE"
4. Click Install

**Option B: Command Line**
```bash
pip install platformio
```

#### Clone and Open Project

```bash
# Clone repository
git clone https://github.com/DanielLiszka/Embedded_Project.git
cd Embedded_Project

# Open in VSCode (if using VSCode)
code .

# Or just navigate to directory for CLI
```

### 3. Configuration

#### Edit Config.h

Open `include/Config.h` and modify these settings:

```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"

#define MQTT_BROKER "your.mqtt.broker.com"
#define MQTT_PORT 1883

#define WIFI_ENABLED false
#define MQTT_ENABLED false
#define DEBUG_ENABLED true
```

#### Adjust Sampling Parameters (Optional)

For different motors, you may need to adjust:

```cpp
#define SAMPLING_FREQUENCY_HZ 100

#define THRESHOLD_MULTIPLIER_WARNING 2.0
#define THRESHOLD_MULTIPLIER_CRITICAL 3.0
```

### 4. Building and Uploading

#### Using PlatformIO IDE (VSCode)

1. Open the project folder in VSCode
2. Click the PlatformIO icon in the left sidebar
3. Under "Project Tasks", click:
   - "Build" to compile
   - "Upload" to flash to ESP32
   - "Monitor" to view serial output

#### Using Command Line

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Open serial monitor
pio device monitor

# Or do all at once
pio run --target upload && pio device monitor
```

### 5. First Run - Calibration

#### Important Calibration Steps

1. **Start Motor**: Ensure motor is running at normal operating conditions
2. **Verify Normal Operation**: Check that motor has no existing faults
3. **Power On ESP32**: Connect USB cable
4. **Wait for Prompt**: Serial monitor will show calibration instructions
5. **Keep Still**: Do not disturb motor or sensor during calibration
6. **Monitor Progress**: Watch serial output for completion

#### Expected Serial Output

```
╔════════════════════════════════════════════════════════╗
║   Motor Vibration Fault Detection System              ║
║   Version 2.0.0                                        ║
╚════════════════════════════════════════════════════════╝

=== Initializing System ===
1. Initializing MPU6050 sensor...
   ✓ MPU6050 OK
2. Initializing Signal Processor...
   ✓ Signal Processor OK
...

========================================
  CALIBRATION MODE
========================================
Please ensure:
  1. Motor is running normally
  2. No abnormal vibrations present
  3. Sensor is securely mounted

Collecting 100 baseline samples...
========================================

Progress: 10% (10/100)
Progress: 20% (20/100)
...
Progress: 100% (100/100)

✓ Calibration complete!
========================================
```

### 6. Normal Operation

After calibration, the system will:

1. Continuously sample vibration at 100 Hz
2. Process 256-sample windows with FFT
3. Extract features every 2.56 seconds
4. Detect anomalies and classify faults
5. Log data to serial console
6. Publish to MQTT (if enabled)
7. Alert on fault detection

#### Monitoring Serial Output

Normal operation shows:

```
--- Motor Vibration Log ---
Timestamp: 00:05:32.123
Temperature: 28.5 °C

Features:
RMS: 1.2345
Peak-to-Peak: 4.5678
Kurtosis: 2.3456
...
---------------------------
```

#### Fault Alert Example

```
========================================
       FAULT ALERT!
========================================
=== Fault Detection Result ===
Type: BEARING
Severity: WARNING
Confidence: 85.00%
Anomaly Score: 2.3456
Description: Bearing fault detected - inspect bearing condition
========================================
```

### 7. Testing the System

#### Test 1: Baseline Verification

1. Let system run for 5 minutes after calibration
2. Should see mostly "Normal operation" status
3. Anomaly scores should be < 2.0

#### Test 2: Simulated Imbalance

1. Safely stop motor
2. Add small weight (tape a coin) to rotating part
3. Restart motor
4. System should detect imbalance within 1-2 minutes

Expected detection:
```
Type: IMBALANCE
Severity: WARNING or CRITICAL
```

#### Test 3: Looseness Detection

1. Slightly loosen one mounting bolt (1/4 turn)
2. System should detect looseness
3. Re-tighten bolt after test

### 8. Troubleshooting

#### "MPU6050 initialization failed"

**Solutions**:
- Check wiring connections
- Verify 3.3V power (not 5V!)
- Try different I2C pins
- Check if sensor is damaged
- Verify I2C address (0x68 or 0x69)

#### "WiFi connection timeout"

**Solutions**:
- Verify SSID and password in Config.h
- Check WiFi signal strength
- Try disabling WiFi (set WIFI_ENABLED to false)
- Ensure 2.4 GHz network (ESP32 doesn't support 5 GHz)

#### "False fault detections"

**Solutions**:
- Re-calibrate system
- Increase threshold multipliers (less sensitive)
- Check sensor mounting (must be rigid)
- Ensure motor was normal during calibration
- Verify no external vibrations during calibration

#### "No readings / system frozen"

**Solutions**:
- Check USB cable and power supply
- Verify serial monitor baud rate (115200)
- Press ESP32 reset button
- Re-upload firmware
- Check for wiring short circuits

### 9. Viewing Data Remotely (MQTT)

#### Setup MQTT Broker

**Option A: Public Broker (Testing Only)**
```cpp
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
```

**Option B: Local Broker (Recommended)**

Install Mosquitto:
```bash
# Ubuntu/Debian
sudo apt-get install mosquitto mosquitto-clients

# macOS
brew install mosquitto

# Start broker
mosquitto -v
```

Update Config.h:
```cpp
#define MQTT_BROKER "192.168.1.100"
#define MQTT_PORT 1883
```

#### Subscribe to Topics

```bash
# Status messages
mosquitto_sub -h broker.hivemq.com -t "motor/status"

# Fault alerts
mosquitto_sub -h broker.hivemq.com -t "motor/fault"

# All topics
mosquitto_sub -h broker.hivemq.com -t "motor/#"
```

### 10. Next Steps

- **Monitor for a Week**: Establish baseline behavior
- **Document Results**: Keep log of detections
- **Tune Parameters**: Adjust thresholds based on your motor
- **Add Features**: Implement web dashboard, alerts, etc.
- **Scale Up**: Deploy multiple sensors

### Additional Resources

- [Main README](../README.md) - Full documentation
- [API Reference](API_REFERENCE.md) - Code documentation
- [Troubleshooting Guide](TROUBLESHOOTING.md) - Common issues
- ESP32 Datasheet
- MPU6050 Datasheet

## Safety Reminders

⚠️ **Always:**
- Follow electrical safety procedures
- Never work on energized motors
- Use proper lockout/tagout procedures
- Wear appropriate PPE
- This is a monitoring tool, not a safety system
- Perform regular manual inspections

## Need Help?

- Check the troubleshooting section
- Review serial monitor output
- Verify connections and configuration
- Open an issue on GitHub
- Contact support

---

Happy monitoring! 🎉
