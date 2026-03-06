import argparse
import csv
import json
import time
from pathlib import Path
from typing import Dict, Iterable, List

import requests

FEATURE_FIELDS = [
    'rms',
    'peakToPeak',
    'kurtosis',
    'skewness',
    'crestFactor',
    'variance',
    'spectralCentroid',
    'spectralSpread',
    'bandPowerRatio',
    'dominantFreq',
]

FAULT_TYPE_TO_LABEL = {
    'NONE': 0,
    'IMBALANCE': 1,
    'MISALIGNMENT': 2,
    'BEARING': 3,
    'LOOSENESS': 4,
    'UNKNOWN': 0,
}


def _features_from_mapping(mapping: Dict) -> List[float]:
    return [float(mapping.get(field, 0.0)) for field in FEATURE_FIELDS]


def load_export_records(input_path: str) -> List[Dict]:
    path = Path(input_path)
    suffix = path.suffix.lower()
    if suffix == '.json':
        return _load_json_export(path)
    if suffix == '.csv':
        return _load_csv_export(path)
    raise ValueError(f'Unsupported replay input format: {suffix}')


def _load_json_export(path: Path) -> List[Dict]:
    with path.open('r', encoding='utf-8') as handle:
        payload = json.load(handle)

    records = payload.get('records', payload if isinstance(payload, list) else [])
    normalized = []
    for record in records:
        features = record.get('features', {})
        fault = record.get('fault', {})
        normalized.append({
            'timestamp': int(record.get('timestamp', features.get('timestamp', 0))),
            'features': _features_from_mapping(features),
            'predicted_label': FAULT_TYPE_TO_LABEL.get(str(fault.get('type', 'NONE')).upper(), 0),
            'confidence': float(fault.get('confidence', 0.0)),
            'label_source': 0,
        })
    return normalized


def _load_csv_export(path: Path) -> List[Dict]:
    normalized = []
    with path.open('r', encoding='utf-8') as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            normalized.append({
                'timestamp': int(float(row.get('timestamp', 0) or 0)),
                'features': _features_from_mapping(row),
                'predicted_label': FAULT_TYPE_TO_LABEL.get(str(row.get('faultType', 'NONE')).upper(), 0),
                'confidence': float(row.get('faultConfidence', 0.0) or 0.0),
                'label_source': 0,
            })
    return normalized


def iter_batches(records: List[Dict], batch_size: int) -> Iterable[List[Dict]]:
    for start in range(0, len(records), batch_size):
        yield records[start:start + batch_size]


def replay_records(records: List[Dict], endpoint: str, device_id: str,
                   batch_size: int = 25, speed: float = 0.0, timeout: float = 10.0) -> int:
    sent = 0
    previous_timestamp = None

    for batch in iter_batches(records, batch_size):
        if speed > 0 and previous_timestamp is not None:
            current_timestamp = batch[0]['timestamp']
            delay_seconds = max(0.0, (current_timestamp - previous_timestamp) / 1000.0 / speed)
            if delay_seconds > 0:
                time.sleep(delay_seconds)

        response = requests.post(
            endpoint,
            json={'device_id': device_id, 'samples': batch},
            timeout=timeout,
        )
        response.raise_for_status()
        sent += len(batch)
        previous_timestamp = batch[-1]['timestamp']

    return sent


def main() -> None:
    parser = argparse.ArgumentParser(description='Replay exported device data into the cloud ingest API')
    parser.add_argument('input_path', help='Path to device export JSON or CSV')
    parser.add_argument('--endpoint', default='http://127.0.0.1:5000/api/ingest/samples',
                        help='Cloud ingest endpoint')
    parser.add_argument('--device-id', default='replay-device',
                        help='Device ID to include in replay batches')
    parser.add_argument('--batch-size', type=int, default=25,
                        help='Number of samples per POST')
    parser.add_argument('--speed', type=float, default=0.0,
                        help='Replay speed multiplier based on sample timestamps (0 disables delays)')
    parser.add_argument('--limit', type=int, default=0,
                        help='Optional max number of records to replay')
    parser.add_argument('--dry-run', action='store_true',
                        help='Parse input and print summary without sending')
    args = parser.parse_args()

    records = load_export_records(args.input_path)
    if args.limit > 0:
        records = records[:args.limit]

    print(f'Loaded {len(records)} records from {args.input_path}')
    if not records:
        return

    if args.dry_run:
        print(json.dumps(records[:3], indent=2))
        return

    sent = replay_records(
        records=records,
        endpoint=args.endpoint,
        device_id=args.device_id,
        batch_size=max(1, args.batch_size),
        speed=max(0.0, args.speed),
    )
    print(f'Replayed {sent} records to {args.endpoint}')


if __name__ == '__main__':
    main()
