# Changelog

All notable changes to Motor Vibration Monitor are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

- Nothing yet.

## [2.0.0] - 2025-12-29

### Added

- Windowed vibration sampling and FFT processing on ESP32
- Feature extraction and baseline calibration
- Anomaly scoring with warning/critical thresholds
- Optional MQTT telemetry and web dashboard
- Helper scripts for MQTT monitoring and offline analysis

### Changed

- Project renamed to Motor Vibration Monitor for the public release

### Security

- OTA updates require an explicit password (OTA is disabled by default)
