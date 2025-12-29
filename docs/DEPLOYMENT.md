# Deployment notes

This document is for people who want to leave Motor Vibration Monitor installed for long periods (shop, lab, hobby setup). It’s not a formal “industrial deployment” guide, but it covers the practical stuff that tends to bite first-time installs.

## Hardware

- Use a stable power source. Random resets are often power-related.
- Mount the sensor rigidly to the motor housing (not to a loose bracket).
- Strain-relief the wiring and keep I2C wiring short.
- If you put the ESP32 in an enclosure, make sure it can shed heat.

## Firmware configuration

Before you flash a long-running install, review `include/Config.h`:

- Set a unique `DEVICE_ID` if you have more than one unit.
- Disable noisy logs for day-to-day use (`DEBUG_ENABLED`, logging interval).
- If you enable WiFi/MQTT/OTA, set credentials and passwords intentionally.

## Calibration

Plan to calibrate after the unit is mounted in its final position. Mounting changes (even just moving the sensor) can shift the baseline.

If you see persistent warnings right after a “good” calibration, treat it as a sign the baseline was captured under a non-normal condition (loose mount, changing load, nearby vibration source).

## Networking and security (optional)

If you turn on network features:

- Put it on a network you trust (or a VLAN).
- Use a real MQTT broker with authentication if you’re sending data off-device.
- Treat OTA as an administrative interface: set a strong password and don’t expose it directly to the internet.

## Operational checklist

- Verify it runs for an hour without resets before leaving it unattended.
- Record the calibration conditions (motor speed/load, mount location).
- Decide how you want to consume alerts (serial, MQTT, dashboard).
- Re-check the mount and re-calibrate after any mechanical work on the motor.
