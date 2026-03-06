# Motor Vibration Monitor

Motor Vibration Monitor is firmware for an ESP32 + MPU6050 that watches motor vibration and flags changes that can hint at mechanical issues (imbalance, misalignment, bearing wear, looseness).

It's useful for diagnostics, experimentation, and learning. It is not a safety system and shouldn't be the only thing you rely on to protect people or equipment.

## Hardware

The intended hardware is simple:

- an ESP32 development board
- an MPU6050 breakout over I2C
- stable 3.3V power
- a rigid mounting method for the sensor

By default, the firmware expects the following wiring, which matches the values in `include/Config.h`:

- `VCC -> 3.3V`
- `GND -> GND`
- `SDA -> GPIO 21`
- `SCL -> GPIO 22`
- optional interrupt line: `INT -> GPIO 19`

The mount matters more than most threshold tuning. A loose sensor, flexible bracket, or unsecured cable will show up as vibration, and the firmware cannot distinguish that from a real mechanical issue.

## Getting started

Clone the repository, install PlatformIO, and build the `esp32dev` environment. The device dashboard is served from SPIFFS, so the filesystem image needs to be uploaded as well as the firmware.

```bash
git clone https://github.com/DanielLiszka/motor-vibration-monitor.git
cd motor-vibration-monitor

pio run -e esp32dev -t upload
pio run -e esp32dev -t buildfs
pio run -e esp32dev -t uploadfs
pio device monitor
```

If `pio` is not on your `PATH`, the Python entrypoint works the same way:

```bash
python -m platformio run -e esp32dev -t upload
python -m platformio run -e esp32dev -t buildfs
python -m platformio run -e esp32dev -t uploadfs
python -m platformio device monitor
```

## First boot and setup

Sampling and feature-extraction constants still live in `include/Config.h`, but the settings you are likely to change in day-to-day use do not. Device identity, warning thresholds, dashboard timing, WiFi settings, MQTT settings, and OTA settings are stored at runtime and can be changed through the dashboard or REST API.

If the device does not have usable WiFi credentials, it starts a fallback provisioning access point with a name like `MotorMonitor-Setup-XXXXXX`. Join that network, open `http://192.168.4.1/`, enter the network settings you want the device to use, save them, and then restart the board.

Once the device joins your normal network, the same dashboard remains available from its assigned IP address.

## Calibration and normal use

Calibration should be done with the motor running in a known-good state, under the load you actually care about, and with the sensor mounted exactly where it will stay. If you move the sensor, change the mount, or materially change the operating condition, calibrate again.

Persistent warnings immediately after calibration usually mean the baseline was captured under unstable conditions. In practice that is often a poor mount, a changing load, or nearby vibration from something other than the motor being monitored.

## Dashboard, MQTT, and updates

The device dashboard shows the live feature set, current fault state, recent history, and configuration controls. If station WiFi is not available, the same dashboard is reachable over the fallback provisioning AP.

When MQTT is enabled, the device publishes status, features, faults, and spectrum data under the `motor/*` topic family. The most common topics are `motor/status`, `motor/features`, `motor/fault`, and `motor/spectrum`.

OTA remains optional and should be treated like any other administrative surface: keep it off the public internet and use a real password. Cloud-delivered model updates are verified on-device against the expected SHA-256 digest before a hot swap is accepted.

## Cloud and operator tooling

The repository also includes a small cloud service for ingesting samples, managing labeling work, retraining from labeled data, and publishing model artifacts. The cloud-side entrypoint is documented in [cloud/README.md](cloud/README.md).

For local operations and validation, a few scripts are worth knowing about:

```bash
python -m pip install -r scripts/requirements.txt
```

`python scripts/mqtt_monitor.py --broker <host>` watches MQTT traffic from a workstation.

`python scripts/replay_to_cloud.py path\to\device_export.json --endpoint http://127.0.0.1:5000/api/ingest/samples` replays a saved device export into the cloud ingest path.

`python scripts/cloud_e2e_smoke.py` runs a local replay, labeling, retraining, registration, and artifact-download smoke test without hardware.

`python scripts/mqtt_rollout_smoke.py` runs a local broker-backed rollout smoke test with a simulated device update flow.

## Troubleshooting

- If the MPU6050 will not initialize, check power, SDA/SCL wiring, and the configured I2C address first.
- If the readings look noisy or unstable, fix the mount before changing thresholds.
- If the board resets under load, suspect power quality before suspecting signal processing.
- If WiFi never comes up, verify that the target network is 2.4 GHz and that the stored credentials are correct.
- If MQTT connects intermittently, verify broker reachability, credentials, and topic configuration from a second machine.

## Development notes

The firmware and SPIFFS image both build in CI, and the Python-side regression suites cover the supported cloud flows. If you make changes to the firmware, it is worth rebuilding both the firmware image and the filesystem image before you call the change done.

Questions and bug reports are best handled through GitHub issues. Useful reports usually include the board type, wiring notes, the exact firmware behavior, and a short serial log.

## Security

If you believe you have found a security issue, please open a GitHub Security Advisory for the repository. If that is not possible, use the maintainer contact listed in `CITATION.cff`.

## License

This project is released under the MIT License. See [LICENSE](LICENSE).
