# Development

## Build

```bash
pio run -e esp32dev
```

If `pio` is not on your PATH:

```bash
python -m platformio run -e esp32dev
```

## Upload

```bash
pio run -e esp32dev -t upload
```

If `pio` is not on your PATH:

```bash
python -m platformio run -e esp32dev -t upload
```

## Test Utilities

See `test/` for PlatformIO test utilities.
