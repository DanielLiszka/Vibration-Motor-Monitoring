# Cloud Service

This package now has a single runnable bootstrap:

```bash
python -m pip install -r cloud/requirements.txt
python -m cloud.app --host 0.0.0.0 --port 5000 --data-dir .cloud_data
```

## What It Wires Together

- `DataCollector` stores incoming device samples in SQLite.
- `LabelingService` persists labeling tasks and labeling outcomes in SQLite.
- `RetrainingOrchestrator` trains from labeled samples and registers deployable artifacts.
- `DeploymentManager` stores model registry data and rollout state on disk.

## Primary Routes

- `GET /ui/labeling` or `GET /labeling`: labeling UI
- `GET /api/monitoring/dashboard`: monitoring summary
- `POST /api/data/sample`: ingest one sample
- `POST /api/data/batch`: ingest a batch of samples
- `POST /api/labeling/create-batch`: create labeling tasks
- `GET /api/labeling/next`: fetch next task
- `POST /api/labeling/submit`: submit a label
- `GET /api/models/<version>/download`: download a registered model artifact
- `POST /api/deployments/device-status`: report rollout progress from a device

## Runtime Data

The `--data-dir` directory stores:

- `training_data.db`: collected samples
- `labeling.db`: labeling tasks and outcomes
- `models/registry.json`: registered model metadata
- `models/deployments.json`: deployment and device rollout state
- `training_runs/`: trained artifacts and metadata

## Replay a device export

You can feed a saved device export back into the cloud service:

```bash
python -m pip install -r scripts/requirements.txt
python scripts/replay_to_cloud.py path\to\device_export.json --endpoint http://127.0.0.1:5000/api/ingest/samples
```

The replay tool understands the JSON and CSV export formats produced by the device history/export endpoints.

## Local smoke test

To run a local replay + labeling + retraining + artifact-download smoke test:

```bash
python scripts/cloud_e2e_smoke.py
```

## Simulated device update loop

If you configure the cloud app with an MQTT notification broker, you can simulate the device-side model update flow:

```bash
python -m cloud.app --mqtt-notify-broker 127.0.0.1 --public-base-url http://127.0.0.1:5000
python scripts/simulate_device_update.py --broker 127.0.0.1 --device-id replay-device-001 --status-endpoint http://127.0.0.1:5000/api/deployments/device-status
```

For a fully local broker + notifier + simulated-device smoke run:

```bash
python scripts/mqtt_rollout_smoke.py
```
