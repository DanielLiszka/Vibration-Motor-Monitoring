# Motor Vibration Monitor

Motor Vibration Monitor is firmware for an ESP32 + MPU6050 that watches motor vibration and flags changes that can hint at mechanical issues (imbalance, misalignment, bearing wear, looseness).

It's useful for diagnostics, experimentation, and learning. It is not a safety system and shouldn't be the only thing you rely on to protect people or equipment.

## Highlights

- On-device sampling and FFT (windowed)
- Baseline calibration + anomaly scoring
- Optional fault classification (rule-based + lightweight model)
- Serial logging, MQTT telemetry, and a small web dashboard (optional)
- Helper scripts for MQTT monitoring and offline analysis

## What it does

At a high level:

- Samples acceleration from the MPU6050 at a fixed rate.
- Processes each window with a Hann/Hanning window + FFT.
- Extracts a feature vector from the time and frequency domains.
- Learns a "normal" baseline during calibration.
- Scores each window against the baseline and raises warning/critical alerts when it drifts.

## Hardware and wiring

- ESP32 DevKit v1 (or similar ESP32 board)
- MPU6050 breakout (I2C)
- 3.3V power, wiring, and a way to mount the sensor firmly to the motor

Default wiring (matches the defaults in `include/Config.h`):

- MPU6050 VCC -> ESP32 3.3V
- MPU6050 GND -> ESP32 GND
- MPU6050 SDA -> GPIO 21
- MPU6050 SCL -> GPIO 22
- (Optional) MPU6050 INT -> GPIO 19

Mounting notes:

- Mount the sensor rigidly to the motor housing. A loose sensor will look like a noisy motor.
- Secure the wiring so it can't flap around and add its own vibration.

## Build and flash

1. Install PlatformIO (VS Code extension or CLI).
2. Clone the repo:

```bash
git clone https://github.com/DanielLiszka/motor-vibration-monitor.git
cd motor-vibration-monitor
```

3. Review `include/Config.h` and (optionally) enable WiFi/MQTT/OTA.
4. Build and upload:

```bash
pio run -e esp32dev -t upload
pio device monitor
```

If `pio` is not on your PATH:

```bash
python -m platformio run -e esp32dev -t upload
python -m platformio device monitor
```

## Configuration

Most settings live in `include/Config.h`.

Common knobs:

- Sampling: `SAMPLING_FREQUENCY_HZ`, `WINDOW_SIZE`
- Sensitivity: `THRESHOLD_MULTIPLIER_WARNING`, `THRESHOLD_MULTIPLIER_CRITICAL`
- Network features: `WIFI_ENABLED`, `MQTT_ENABLED`, `OTA_ENABLED`

If you enable network features, don't commit credentials. This repo ignores common local headers like `credentials.h` and `secrets.h` via `.gitignore`.

## Calibration

Calibration should be done with the motor running in a known-good state and with the sensor mounted in its final position. If you move the sensor or change the mounting, re-calibrate.

If you see persistent warnings right after a "good" calibration, treat it as a sign the baseline was captured under a non-normal condition (loose mount, changing load, nearby vibration source).

## MQTT and dashboard

When enabled:

- The web dashboard is served from the ESP32 and shows live status and spectrum.
- MQTT publishes status, features, faults, and spectrum data.

Default topics are under `motor/*` (see `include/MQTTManager.h`). Common ones:

- `motor/status`
- `motor/features`
- `motor/fault`
- `motor/spectrum`

To monitor MQTT traffic from a laptop/desktop:

```bash
python -m pip install -r scripts/requirements.txt
python scripts/mqtt_monitor.py --broker <host>
```

## OTA updates (optional)

OTA is disabled by default. If you enable it, set a non-empty `OTA_PASSWORD` in `include/Config.h` before flashing.

Treat OTA like an admin interface: keep it off the public internet and use a strong password.

## Troubleshooting

- MPU6050 init fails: double-check 3.3V power, SDA/SCL wiring, and the I2C address in `include/Config.h`.
- Noisy readings / false alerts: re-mount the sensor (rigid mounting matters more than thresholds), secure the wiring, then tune thresholds.
- Random resets: try a better USB cable/power supply; WiFi/MQTT can increase peak current draw.
- WiFi won't connect: confirm 2.4 GHz network, SSID/password, and `WIFI_TIMEOUT_MS`.
- MQTT won't connect: verify broker host/port and credentials, and test with `scripts/mqtt_monitor.py`.

## Contributing and support

- Questions and bug reports: open a GitHub issue and include wiring details and a short serial log snippet.
- PRs: keep changes focused and build `esp32dev` (`pio run -e esp32dev`) before submitting.

## Security

If you believe you found a security issue, please open a GitHub Security Advisory for this repository. If you can't use advisories, contact the maintainer email listed in `CITATION.cff`.

## Changelog

- 2.0.0 (2025-12-29): public release cleanup, rebrand, docs consolidated into this README.

## License

MIT (see `LICENSE`).
