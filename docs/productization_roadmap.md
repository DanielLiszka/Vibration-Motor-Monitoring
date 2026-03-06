# Productization Roadmap

## Phase 1: Coherent Device Runtime

Status: completed in this change set.

Goals:

- Persist runtime settings instead of relying on compile-time-only network and threshold values.
- Expose a real REST control surface for configuration, history, baseline, and export.
- Serve one supported dashboard path from SPIFFS.
- Keep the firmware image within flash limits while preserving OTA and SPIFFS support.

Delivered:

- `RuntimeConfigManager` for persisted device identity, thresholds, dashboard interval, WiFi, MQTT, and OTA settings.
- Real `/api/v1/config`, `/api/v1/history`, `/api/v1/baseline`, `/api/v1/export`, and alert acknowledgement endpoints.
- Self-contained dashboard assets loaded from SPIFFS without CDN dependencies.
- Performance counters wired into the live pipeline.
- Custom partition table with larger OTA app slots and a practical SPIFFS allocation.

## Phase 2: Supported Cloud Bootstrap

Goals:

- Ship one runnable cloud entrypoint instead of disconnected service modules.
- Define the supported MQTT/cloud topology and remove redundant paths.
- Add a simple operator deployment path such as `docker compose up`.

Status: foundational implementation completed in this change set.

Targets:

- `cloud/app.py` or equivalent Flask/FastAPI bootstrap.
- Persistent SQLite-backed service wiring for monitoring, labeling, retraining, and deployment metadata.
- A documented local deployment flow.

## Phase 3: Real Data Operations

Goals:

- Make the device-to-cloud-to-model workflow practical with real samples.
- Turn labeling into a durable workflow rather than an in-memory task queue.

Status: substantial progress completed in this change set.

Targets:

- Persist labeling queues and decisions.
- Make collected samples traceable by device, timestamp, and model version.
- Expose a supported workflow for collecting, labeling, exporting, and retraining on real logs.

## Phase 4: Model Lifecycle

Goals:

- Replace synthetic-data-first training with a real dataset pipeline.
- Make retraining, validation, version registration, and deployment one coherent loop.

Status: foundational implementation completed in this change set.

Targets:

- Training entrypoints that consume stored device samples.
- Validation gates against production metrics.
- Version registration before deployment.
- A documented rollback path.

## Phase 5: Regression Safety

Goals:

- Add tests that protect the supported runtime and API surfaces.
- Keep hardware smoke testing separate from host-side regression coverage.

Status: host-side baseline completed in this change set.

Targets:

- Host-side tests for config serialization, REST payloads, buffer export, and threshold validation.
- Firmware smoke tests for sensor, FFT, and connectivity.
- CI that does more than compile.

## Phase 6: Operational UX

Status: completed in this change set.

Goals:

- Make first-boot setup workable without recompiling firmware.
- Add integrity checks to model hot-swap paths.
- Give operators a way to replay saved field data back through the cloud stack.

Delivered:

- Fallback provisioning AP mode on the device using the existing dashboard and runtime config.
- SHA-256 verification of downloaded model artifacts before hot swap.
- `scripts/replay_to_cloud.py` for replaying device export JSON/CSV into the cloud ingest API.
