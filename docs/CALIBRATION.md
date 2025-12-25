# Calibration

On first boot (or when requested), the firmware collects a set of baseline vibration windows while the motor is operating normally.

The baseline is then used to compare future feature vectors and detect deviations.

Calibration parameters are configured in `include/Config.h`.
