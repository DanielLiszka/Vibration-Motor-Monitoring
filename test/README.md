# Tests

This folder contains PlatformIO test utilities used to validate hardware and signal processing.

- `test_mpu6050.cpp` - Sensor bring-up and I2C validation.
- `test_fft.cpp` - FFT correctness and performance checks.

## Run

```bash
pio test -e esp32dev
```
