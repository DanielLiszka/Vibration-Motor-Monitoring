# MQTT

When MQTT is enabled, the firmware publishes telemetry and fault events to a broker configured in `include/Config.h`.

## Topics

Typical topics include:

- `motor/features`
- `motor/fault`
- `motor/status`
- `motor/spectrum`

See `include/MQTTManager.h` and `src/MQTTManager.cpp` for implementation details.
