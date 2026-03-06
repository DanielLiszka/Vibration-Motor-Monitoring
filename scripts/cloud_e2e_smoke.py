import argparse
import json
import shutil
import sys
import time
from pathlib import Path
from threading import Thread

import requests
from werkzeug.serving import make_server

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from cloud.app import create_app
from scripts.replay_to_cloud import load_export_records, replay_records


class FakeModel:
    def fit(self, *args, **kwargs):
        return {'history': 'ok'}

    def evaluate(self, *args, **kwargs):
        return {'loss': 0.1, 'accuracy': 0.94}

    def save(self, path):
        with open(path, 'wb') as file_obj:
            file_obj.write(b'fake-model-artifact')


def fake_model_factory(input_dim):
    return FakeModel()


def build_sample_export(path: Path, count: int = 8) -> None:
    payload = {
        'records': [
            {
                'timestamp': 1000 + index,
                'features': {
                    'rms': 1.0 + index * 0.1,
                    'peakToPeak': 2.0 + index * 0.1,
                    'kurtosis': 3.0,
                    'skewness': 0.1,
                    'crestFactor': 4.0,
                    'variance': 0.5,
                    'spectralCentroid': 20.0,
                    'spectralSpread': 5.0,
                    'bandPowerRatio': 1.2,
                    'dominantFreq': 18.0,
                },
                'fault': {
                    'type': 'IMBALANCE' if index % 2 else 'NONE',
                    'confidence': 0.85 if index % 2 else 0.1,
                },
            }
            for index in range(count)
        ]
    }
    path.write_text(json.dumps(payload), encoding='utf-8')


def main() -> None:
    parser = argparse.ArgumentParser(description='Run a local cloud replay/label/retrain smoke test')
    parser.add_argument('--runtime-dir', default='.cloud_data_e2e', help='Temporary runtime directory')
    parser.add_argument('--port', type=int, default=5010, help='Local port for the in-process cloud app')
    args = parser.parse_args()

    runtime_dir = Path(args.runtime_dir)
    if runtime_dir.exists():
        shutil.rmtree(runtime_dir)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    export_path = runtime_dir / 'replay_export.json'
    build_sample_export(export_path)
    records = load_export_records(str(export_path))

    app = create_app(
        config={'STATE_DIR': str(runtime_dir), 'START_BACKGROUND_WORKERS': False},
        services={'model_factory': fake_model_factory},
    )
    server = make_server('127.0.0.1', args.port, app)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()

    summary = {}
    base_url = f'http://127.0.0.1:{args.port}'

    try:
        for _ in range(20):
            try:
                if requests.get(f'{base_url}/api/monitoring/health', timeout=2).ok:
                    break
            except Exception:
                time.sleep(0.2)

        summary['replayed'] = replay_records(
            records=records,
            endpoint=f'{base_url}/api/ingest/samples',
            device_id='replay-device-001',
            batch_size=4,
            speed=0.0,
        )
        summary['create_batch'] = requests.post(
            f'{base_url}/api/labeling/create-batch',
            json={'num_samples': 6},
            timeout=5,
        ).json()

        batch = requests.get(f'{base_url}/api/labeling/batch?labeler_id=ops&batch_size=6', timeout=5).json()
        summary['batch'] = {'count': batch.get('count'), 'task_ids': [task['task_id'] for task in batch.get('tasks', [])]}
        for task in batch.get('tasks', []):
            requests.post(
                f'{base_url}/api/labeling/submit',
                json={
                    'task_id': task['task_id'],
                    'label': task['predicted_label'],
                    'labeler_id': 'ops',
                    'confidence': 1.0,
                },
                timeout=5,
            ).raise_for_status()

        summary['dashboard_after_labeling'] = requests.get(f'{base_url}/api/monitoring/dashboard', timeout=5).json()
        summary['retraining_trigger'] = requests.post(
            f'{base_url}/api/monitoring/retraining/trigger',
            json={'trigger': 'ops-smoke'},
            timeout=5,
        ).json()

        retraining_status = None
        for _ in range(50):
            retraining_status = requests.get(f'{base_url}/api/monitoring/retraining', timeout=5).json()
            current = retraining_status.get('current_job')
            if current and current.get('status') in {'completed', 'failed'}:
                break
            time.sleep(0.1)
        summary['retraining_status'] = retraining_status

        models = requests.get(f'{base_url}/api/monitoring/models', timeout=5).json()
        summary['models'] = models
        if models.get('models'):
            version = models['models'][0]['version']
            download = requests.get(f'{base_url}/api/models/{version}/download', timeout=5)
            summary['download'] = {
                'version': version,
                'status_code': download.status_code,
                'bytes': len(download.content),
            }

        print(json.dumps(summary, indent=2))
    finally:
        server.shutdown()
        thread.join(timeout=5)


if __name__ == '__main__':
    main()
