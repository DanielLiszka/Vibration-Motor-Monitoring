# Getting started

This guide covers wiring, flashing, and the first calibration run.

## 1) Hardware

- ESP32 DevKit v1 (or compatible ESP32 board)
- MPU6050 breakout (I2C)
- Jumper wires
- A safe way to mount the sensor to the motor housing

Default wiring (matches `include/Config.h`):

- MPU6050 VCC -> ESP32 3.3V
- MPU6050 GND -> ESP32 GND
- MPU6050 SDA -> GPIO 21
- MPU6050 SCL -> GPIO 22
- (Optional) MPU6050 INT -> GPIO 19

Mounting notes:

- Mount the MPU6050 as rigidly as possible. A loose sensor will look like a noisy motor.
- Secure the wiring so it can’t flap around and add its own “vibration”.

## 2) Install PlatformIO

VS Code:

- Install Visual Studio Code
- Install the “PlatformIO IDE” extension

CLI:

```bash
python -m pip install platformio
```

## 3) Clone the repo

```bash
git clone https://github.com/DanielLiszka/vibesentry.git
cd vibesentry
```

## 4) Configure

Open `include/Config.h` and review the defaults.

Common things to adjust:

- Sampling: `SAMPLING_FREQUENCY_HZ`, `WINDOW_SIZE`
- Sensitivity: `THRESHOLD_MULTIPLIER_WARNING`, `THRESHOLD_MULTIPLIER_CRITICAL`
- Network features: `WIFI_ENABLED`, `MQTT_ENABLED`, `OTA_ENABLED`

If you enable WiFi/MQTT/OTA, set credentials before flashing.

## 5) Build + upload

```bash
pio run -e esp32dev -t upload
pio device monitor
```

If `pio` isn’t on your PATH, you can use `python -m platformio` instead.

## 6) First run (calibration)

Calibration should be done with the motor running in a known-good state.

General checklist:

- Motor is running normally (no intentional imbalance, loose mounts, etc.)
- Sensor is firmly mounted
- Let the device finish calibration before judging any alerts

Once calibration completes, the firmware switches to monitoring and starts scoring each window against the baseline.

## 7) Next steps

- Let it run for a while and watch the serial log and/or dashboard.
- If you’re getting false positives, re-check mounting first, then tune thresholds.
- For common failure modes and fixes, see `docs/TROUBLESHOOTING.md`.
