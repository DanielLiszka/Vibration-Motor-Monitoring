# Contributing

Thanks for taking the time to contribute!

## Development Setup

- Install PlatformIO (VSCode extension or CLI): `pip install platformio`
- Build firmware: `pio run -e esp32dev`
- Upload firmware: `pio run -e esp32dev -t upload`
- Serial monitor: `pio device monitor`

## Guidelines

- Keep changes focused and include a short rationale in the PR description.
- Run `pio run -e esp32dev` before submitting.
- Avoid committing credentials or secrets. Use a local `include/credentials.h` (already in `.gitignore`) if needed.
- Update `README.md`/`docs/` and `CHANGELOG.md` when behavior or configuration changes.

## Reporting Bugs / Requesting Features

- Include hardware details (ESP32 variant, sensor module, wiring), firmware version, and a serial log snippet.
- If possible, describe steps to reproduce and expected vs. actual behavior.

