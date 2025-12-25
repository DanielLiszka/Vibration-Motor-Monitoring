## Production Deployment Guide

This guide covers deploying the Motor Vibration Fault Detection System in a production environment.

## Pre-Deployment Checklist

### ✓ Hardware Verification

- [ ] ESP32 board tested and functional
- [ ] MPU6050 sensor tested (run `test/test_mpu6050.cpp`)
- [ ] All connections soldered (not breadboard)
- [ ] Enclosure selected and prepared
- [ ] Power supply verified (stable 5V, minimum 1A)
- [ ] Mounting hardware acquired

### ✓ Software Configuration

- [ ] Motor-specific configuration selected
- [ ] WiFi credentials configured (if using)
- [ ] MQTT broker configured (if using)
- [ ] Thresholds tuned for motor type
- [ ] Debug output disabled for production

### ✓ Testing

- [ ] FFT performance verified (run `test/test_fft.cpp`)
- [ ] System runs for 24+ hours without crashes
- [ ] Calibration tested
- [ ] Known fault detection verified
- [ ] False positive rate acceptable

---

## Configuration for Production

### 1. Update Config.h

```cpp
// Disable debug output for performance
#define DEBUG_ENABLED false

// Set appropriate log intervals
#define LOG_INTERVAL_MS 5000      // Log every 5 seconds
#define ALERT_COOLDOWN_MS 60000   // Alert cooldown: 1 minute

// Enable features as needed
#define WIFI_ENABLED true         // Set false if no network
#define MQTT_ENABLED true         // Set false if local only
#define LOG_TO_SERIAL true        // Keep for troubleshooting
#define LOG_TO_FLASH false        // Enable when flash logging implemented
```

### 2. Set Motor Configuration

Include appropriate motor config:

```cpp
// In src/main.cpp, add BEFORE other includes:
#include "motor_configs/large_motor.h"  // Or your motor type
```

### 3. WiFi Security

**Never commit credentials to version control!**

Option A: Use separate credentials file
```cpp
// Create include/credentials.h (add to .gitignore):
#define WIFI_SSID "ProductionNetwork"
#define WIFI_PASSWORD "SecurePassword123"
#define MQTT_USER "motor_monitor"
#define MQTT_PASSWORD "MqttPass456"

// In Config.h:
#include "credentials.h"
```

Option B: Environment variables (advanced)
```cpp
#define WIFI_SSID ${env.WIFI_SSID}
```

---

## Hardware Assembly for Production

### Enclosure Requirements

**IP Rating:** Minimum IP54 for industrial environments
- IP54: Dust protected, splash resistant
- IP65: Dust tight, water jet resistant (recommended)
- IP67: Dust tight, temporary immersion (harsh environments)

**Materials:**
- ABS plastic enclosure
- Cable glands for wire entry
- Rubber gasket for lid
- Ventilation (if temperature > 60°C)

### Recommended Enclosure Layout

```
┌─────────────────────────────────┐
│                                 │
│  ┌──────┐         ┌──────────┐ │
│  │ESP32 │   USB   │ Terminal │ │
│  │      ├─────────┤  Block   │ │
│  └──┬───┘         └────┬─────┘ │
│     │                  │       │
│  ┌──┴────────┐    ┌────┴─────┐ │
│  │ MPU6050   │    │ Power    │ │
│  │ (mounted  │    │ Supply   │ │
│  │ on motor) │    └──────────┘ │
│  └───────────┘                 │
│                                 │
│  [Status LED visible on front] │
└─────────────────────────────────┘
```

### Sensor Mounting

**Critical: Mounting quality directly affects accuracy!**

**Preparation:**
1. Clean motor surface (remove oil, dirt, paint)
2. Use degreaser/alcohol
3. Roughen surface slightly for better adhesion
4. Mark orientation for future reference

**Mounting Methods (best to worst):**

1. **Stud Mount (Best)**
   - Drill and tap M6 hole in motor
   - Use threaded stud and adhesive
   - Torque to spec
   - Most reliable, permanent

2. **Epoxy/JB Weld (Good)**
   - Two-part epoxy or JB Weld
   - Clamp for 24 hours
   - Very strong, semi-permanent
   - Can be removed with heat if needed

3. **Industrial Adhesive (Good)**
   - 3M VHB tape or Loctite adhesive
   - Clean surface thoroughly
   - Apply pressure for 30 seconds
   - Wait 24 hours before operation

4. **Hot Glue (Acceptable for testing)**
   - Quick installation
   - Not for high-vibration applications
   - Temperature sensitive

**Mounting Location:**
- Directly on bearing housing (preferred)
- Motor end bell
- Motor frame near bearing
- Avoid mounting on: panels, covers, bases

**Orientation:**
- Z-axis vertical (perpendicular to shaft)
- X-axis radial
- Y-axis axial
- Document orientation for future reference

### Wiring

**Cable Selection:**
- 22-24 AWG stranded wire
- Shielded if EMI present
- Rated for temperature (typically 105°C)

**Wire Management:**
- Secure wires every 15cm
- Use cable ties or loom
- Avoid sharp bends
- Keep away from power cables (>10cm separation)
- Use ferrite beads if near motor power

**Connection Points:**
```
Motor Junction Box → Cable Gland → Enclosure → ESP32
```

**Strain Relief:**
- Essential at both ends
- Prevents wire fatigue
- Use proper cable glands

---

## Calibration Procedure

### Pre-Calibration

1. **Motor Inspection**
   - Verify motor is in good condition
   - No existing faults
   - Normal operating temperature
   - Proper alignment

2. **Environment Check**
   - No external vibration sources
   - Stable operating conditions
   - Normal load

3. **System Check**
   - Sensor securely mounted
   - All connections tight
   - Power stable
   - Serial monitor connected

### Calibration Steps

1. **Start Motor**
   - Allow 15-30 minutes warm-up
   - Reach normal operating temperature
   - Stabilize speed

2. **Reset ESP32**
   - Power cycle or press reset button
   - System enters calibration mode automatically

3. **Monitor Progress**
   ```
   Watch serial output:
   Progress: 10% (10/100)
   Progress: 20% (20/100)
   ...
   ✓ Calibration complete!
   ```

4. **Verify Baseline**
   - Check baseline statistics
   - RMS should be consistent
   - Anomaly scores near 0

5. **Test Detection**
   - Intentionally introduce small imbalance
   - Verify detection occurs
   - Remove test fault

### Post-Calibration

- Document baseline values
- Save baseline (if persistence enabled)
- Monitor for 24 hours
- Fine-tune thresholds if needed

---

## Installation Steps

### Day 1: Installation

**Morning:**
1. De-energize motor (LOTO procedures)
2. Mount sensor on motor
3. Route cables to enclosure
4. Install enclosure
5. Make connections

**Afternoon:**
6. Re-energize motor
7. Verify system boots
8. Run initial tests
9. Begin calibration
10. Monitor for rest of day

### Day 2-3: Monitoring

- Watch for false positives
- Check system stability
- Verify data logging
- Test MQTT if enabled
- Adjust thresholds if needed

### Week 1: Validation

- Introduce known faults (controlled)
- Verify detection
- Document response
- Fine-tune settings

---

## Monitoring and Maintenance

### Daily

- Check system status (LED, MQTT messages)
- Review any alerts
- Verify data logging

### Weekly

- Download logs
- Review trends
- Check for anomalies
- Inspect physical connections

### Monthly

- Clean sensor
- Check mounting
- Verify calibration still valid
- Update firmware if available

### Annually

- Re-calibrate
- Inspect all connections
- Replace battery (if RTC present)
- Review detection history

---

## Multi-Sensor Deployment

For monitoring multiple motors:

### Option 1: Individual Systems

**Pros:**
- Independent operation
- Localized processing
- No network dependency

**Cons:**
- More hardware cost
- Harder to centralize data

**Configuration:**
```cpp
// Give each device unique ID:
#define DEVICE_NAME "MotorVibMonitor_01"  // Increment for each
#define MQTT_CLIENT_ID DEVICE_NAME
```

### Option 2: Sensor Network

**Architecture:**
```
[MPU6050] → [ESP32] ┐
[MPU6050] → [ESP32] ├→ [MQTT Broker] → [Central Dashboard]
[MPU6050] → [ESP32] ┘
```

**MQTT Topic Structure:**
```
factory/building1/motor01/status
factory/building1/motor01/fault
factory/building1/motor02/status
factory/building1/motor02/fault
```

---

## Data Management

### Local Storage

When flash logging is implemented:
- Circular buffer (30 days)
- Daily log rotation
- Automatic old data purge

### Remote Storage

**Option A: MQTT → InfluxDB**
```bash
# Install InfluxDB
docker run -p 8086:8086 influxdb

# Configure Telegraf MQTT input
[[inputs.mqtt_consumer]]
  servers = ["tcp://broker:1883"]
  topics = ["motor/#"]
```

**Option B: MQTT → PostgreSQL**
- Use Node-RED or custom script
- Parse JSON messages
- Insert into database

**Option C: Cloud Services**
- AWS IoT Core
- Azure IoT Hub
- Google Cloud IoT

---

## Security Considerations

### Network Security

1. **Isolate on VLAN**
   - Separate industrial network
   - Firewall from corporate network

2. **MQTT Security**
   ```cpp
   // Use TLS for MQTT (requires cert)
   #define MQTT_USE_TLS true
   #define MQTT_PORT 8883  // TLS port

   // Always use authentication
   #define MQTT_USER "motor_monitor"
   #define MQTT_PASSWORD "SecurePassword"
   ```

3. **WiFi Security**
   - WPA2 minimum (WPA3 preferred)
   - Strong password
   - MAC address filtering (optional)
   - Hidden SSID (optional)

### Physical Security

- Lock enclosures
- Tamper-evident seals
- Physical access controls
- Camera monitoring (if critical)

---

## Backup and Recovery

### Backup Configuration

```bash
# Backup firmware
pio run
cp .pio/build/esp32dev/firmware.bin backups/firmware_v1.0.0.bin

# Backup configuration
cp include/Config.h backups/config_motor01_2025-01-01.h
```

### Recovery Procedure

1. **System Failure**
   - Replace ESP32
   - Re-upload firmware
   - Load saved configuration
   - Re-calibrate

2. **Data Loss**
   - Check MQTT retained messages
   - Restore from database backup
   - Re-calibrate if baseline lost

---

## Performance Optimization

### For Resource-Constrained Deployments

```cpp
// Reduce memory usage
#define WINDOW_SIZE 128           // Smaller FFT
#define CALIBRATION_SAMPLES 50    // Faster calibration

// Reduce processing
#define SAMPLING_FREQUENCY_HZ 50  // Lower rate

// Disable features
#define WIFI_ENABLED false        // Local only
```

### For Maximum Accuracy

```cpp
// Increase resolution
#define WINDOW_SIZE 512
#define SAMPLING_FREQUENCY_HZ 200

// More calibration
#define CALIBRATION_SAMPLES 500

// Stricter thresholds
#define THRESHOLD_MULTIPLIER_WARNING 1.5
```

---

## Troubleshooting in Production

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for detailed guide.

**Quick Checks:**
1. Power LED on?
2. WiFi connected?
3. MQTT publishing?
4. Free heap > 10KB?
5. Anomaly scores reasonable?

**Emergency Response:**
1. Check serial output
2. Verify motor is running
3. Check sensor mounting
4. Power cycle system
5. Re-calibrate if needed

---

## Documentation

Maintain records for each installation:

**Installation Record:**
- Date installed
- Motor details (make, model, RPM, HP)
- Sensor location (photo)
- Serial number
- Configuration used
- Baseline values
- Calibration date

**Maintenance Log:**
- Date
- Action taken
- Person responsible
- Results
- Next action date

---

## Compliance

### Industrial Standards

- **ISO 10816**: Vibration standards
- **NFPA 70E**: Electrical safety
- **OSHA 1910**: Machinery safety

### Documentation Required

- Installation procedures
- Calibration records
- Maintenance logs
- Incident reports
- Training records

---

## Support and Updates

### Firmware Updates

```bash
# Over USB
pio run --target upload

# OTA (when implemented)
# Update via web interface or MQTT
```

### Support Contacts

- GitHub Issues
- Documentation
- Community forum

---

## Success Metrics

Track these KPIs:

- **Uptime**: Target >99%
- **False Positive Rate**: <5%
- **Detection Rate**: >90%
- **Response Time**: <5 minutes
- **Calibration Stability**: >30 days

---

**Remember:** This system enhances maintenance but does not replace regular inspection procedures!
