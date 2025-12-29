# Troubleshooting

If something looks off, start with the basics: power, wiring, and mounting. A surprising number of “software” problems are really a loose sensor or a flaky power source.

## MPU6050 not detected

Symptoms:

- Serial log reports MPU6050 init failure
- No vibration data

Things to check:

- Wiring matches the pin settings in `include/Config.h`
- MPU6050 is powered from 3.3V (not 5V)
- SDA/SCL aren’t swapped
- If your module supports 0x68/0x69, verify the address matches `MPU6050_I2C_ADDRESS`

## Readings are noisy or jumpy

Symptoms:

- Large spikes at rest
- Frequent false warnings
- Values change when you touch the wires

Most common causes:

- The sensor isn’t mounted rigidly to the motor housing
- Wires are moving/vibrating and tugging the breakout board
- The motor power wiring is coupling noise into the sensor wiring

Try this:

- Re-mount the sensor (epoxy/bolts beat tape)
- Secure the cable run to the motor housing
- Keep I2C wiring short; route it away from motor power

## Random resets / “brownout detector triggered”

Symptoms:

- ESP32 reboots under load
- Serial monitor shows brownout or watchdog resets

Try this:

- Use a known-good USB cable and power supply (avoid long, thin cables)
- Power the ESP32 from a stable 5V source
- Disable optional features (WiFi/MQTT/web server) to reduce peak current draw

## WiFi won’t connect

Symptoms:

- Repeated connect attempts
- IP address never shows up (if you log it)

Try this:

- Confirm SSID/password are set correctly
- Ensure you’re using a 2.4 GHz network (ESP32 doesn’t do 5 GHz)
- Increase `WIFI_TIMEOUT_MS` in `include/Config.h`
- Temporarily disable MQTT/OTA to reduce moving parts while debugging

## MQTT won’t connect

Symptoms:

- WiFi works, but MQTT connect fails

Try this:

- Confirm broker host/port in `include/Config.h`
- If your broker requires auth, set `MQTT_USER`/`MQTT_PASSWORD`
- Verify topic traffic with `scripts/mqtt_monitor.py`

## Calibration produces bad baselines

Symptoms:

- Immediate warnings after calibration
- Anomaly score stays high even during normal operation

Try this:

- Re-calibrate with the motor running normally
- Make sure the sensor is firmly mounted before calibrating
- Increase `CALIBRATION_SAMPLES` or raise the threshold multipliers

## What to include in a bug report

- Board type and power setup
- MPU6050 module and wiring/pins
- A short serial log snippet (from `pio device monitor`)
- Your relevant `include/Config.h` settings (redact credentials)
