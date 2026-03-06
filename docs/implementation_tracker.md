# Implementation Tracker

## Objective

Make the project deployable and operable beyond the baseline firmware/cloud productization pass.

## Current Phase

Focus:

- Device provisioning over fallback AP mode using the existing dashboard and runtime config.
- Verified model update handling on-device using the hash already emitted by the cloud deployment manager.
- Replay tooling for feeding recorded device exports back into the cloud ingestion path.

## Work Items

- [x] Add WiFi fallback provisioning mode and expose its state through the API/dashboard.
- [x] Verify downloaded model artifacts against expected SHA-256 before hot-swapping.
- [x] Add a replay CLI for device-export JSON/CSV to cloud ingest.
- [x] Add a repeatable local cloud smoke script and device-update simulator tooling.
- [x] Update docs and tests for the new operator workflows.
- [x] Run firmware, SPIFFS, Python, and syntax verification.

## Verification

- `python -m pytest -q tests_py cloud/tests`
- `python -m py_compile cloud\app.py ... scripts\replay_to_cloud.py`
- `python -m platformio run -e esp32dev --disable-auto-clean`
- `python -m platformio run -e esp32dev -t buildfs --disable-auto-clean`

## Operational Bring-Up

- No ESP32 serial device was visible in the current environment, so firmware upload and SPIFFS flashing could not be performed.
- Local cloud operator smoke flow was completed in-process:
  - replayed 8 exported records into `/api/ingest/samples`
  - created and completed 6 labeling tasks
  - triggered retraining with a fake model factory
  - verified model registration and successful artifact download
  - confirmed completed job metadata reported `num_samples = 6`
- Local MQTT rollout smoke was also completed with a transient broker and simulator:
  - cloud notifier published a model update message
  - simulated device downloaded and verified the artifact hash
  - deployment status advanced to `completed`

## Notes

- Reuse the existing runtime config and dashboard where possible.
- Avoid creating a second settings flow separate from the device UI.
- Keep firmware size within the custom OTA + SPIFFS partition layout.
