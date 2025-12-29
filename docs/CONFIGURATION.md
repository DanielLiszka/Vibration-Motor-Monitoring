# Configuration

Most settings are defined in `include/Config.h`.

## Common Settings

- Sampling frequency and window size
- Warning/critical thresholds
- Calibration sample count

## Optional Features

Networked features are disabled by default and can be enabled in `include/Config.h`:

- WiFi
- Web dashboard
- MQTT
- OTA updates

If you enable network features, avoid committing credentials. OTA also requires a non-empty `OTA_PASSWORD`.
