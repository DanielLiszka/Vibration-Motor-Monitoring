# Motor Vibration Monitor

Motor Vibration Monitor is firmware for an ESP32 + MPU6050 that watches motor vibration and flags changes that can hint at mechanical issues (imbalance, misalignment, bearing wear, looseness). It’s built for tinkering, diagnostics, and learning — not as a safety system.

## Highlights

- On-device sampling and FFT (windowed)
- Baseline calibration + anomaly scoring
- Optional fault classification (rule-based + lightweight model)
- Serial logging, MQTT telemetry, and a small web dashboard (optional)
- Helper scripts for MQTT monitoring and offline analysis

## Hardware

- ESP32 DevKit v1 (or similar ESP32 board)
- MPU6050 breakout (I2C)
- 3.3V power, wiring, and a way to mount the sensor firmly to the motor

Default wiring:

- MPU6050 VCC → ESP32 3.3V
- MPU6050 GND → ESP32 GND
- MPU6050 SDA → GPIO 21
- MPU6050 SCL → GPIO 22
- (Optional) MPU6050 INT → GPIO 19

## Quick start

1. Install PlatformIO (VS Code extension or CLI).
2. Clone the repo:

```bash
git clone https://github.com/DanielLiszka/motor-vibration-monitor.git
cd motor-vibration-monitor
```

3. Review `include/Config.h` and (optionally) enable WiFi/MQTT/OTA.
4. Build + upload:

```bash
pio run -e esp32dev -t upload
pio device monitor
```

5. Let it calibrate with the motor running in a known-good state, then leave it to monitor.

## Configuration

Most settings live in `include/Config.h`. Common knobs:

- `SAMPLING_FREQUENCY_HZ`, `WINDOW_SIZE`
- `THRESHOLD_MULTIPLIER_WARNING`, `THRESHOLD_MULTIPLIER_CRITICAL`
- `WIFI_ENABLED`, `MQTT_ENABLED`, `OTA_ENABLED`

## MQTT and dashboard

When enabled:

- The web dashboard is served from the ESP32 and shows live status and spectrum.
- MQTT publishes status, features, faults, and spectrum data.

Default topics are under `motor/*` (see `include/MQTTManager.h`).

## Documentation

- `docs/GETTING_STARTED.md`
- `docs/CONFIGURATION.md`
- `docs/TROUBLESHOOTING.md`
- `docs/DEPLOYMENT.md`

## License

MIT (see `LICENSE`).
