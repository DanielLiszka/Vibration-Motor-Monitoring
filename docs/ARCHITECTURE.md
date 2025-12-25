# Architecture

The firmware is structured as a pipeline:

1. **Sensor I/O**: read accelerometer samples from the MPU6050.
2. **Signal processing**: windowing and FFT over fixed-size sample windows.
3. **Feature extraction**: compute time- and frequency-domain features.
4. **Detection**: compare features to a calibrated baseline and raise alerts.
5. **Optional outputs**: serial logs, web dashboard, MQTT, and webhooks.

Configuration lives in `include/Config.h`.
