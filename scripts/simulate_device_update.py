import argparse
import hashlib
import json
from pathlib import Path
from typing import Dict, Optional

import paho.mqtt.client as mqtt
import requests


def build_model_update_topic(topic_prefix: str, device_id: str) -> str:
    prefix = topic_prefix.strip('/')
    return f'{prefix}/{device_id}/models/update'


def compute_sha256_hex(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def verify_artifact(content: bytes, expected_hash: Optional[str]) -> bool:
    if not expected_hash:
        return False
    return compute_sha256_hex(content).lower() == expected_hash.lower()


class DeviceUpdateSimulator:
    def __init__(
        self,
        device_id: str,
        status_endpoint: str,
        artifact_dir: str,
        session: Optional[requests.Session] = None,
    ):
        self.device_id = device_id
        self.status_endpoint = status_endpoint
        self.artifact_dir = Path(artifact_dir)
        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        self.session = session or requests.Session()

    def report_status(self, status: str, current_version: Optional[str] = None, error: str = '') -> None:
        payload = {'device_id': self.device_id, 'status': status}
        if current_version:
            payload['current_version'] = current_version
        if error:
            payload['error'] = error
        self.session.post(self.status_endpoint, json=payload, timeout=10).raise_for_status()

    def handle_update(self, payload: Dict) -> Dict:
        version = payload.get('version') or payload.get('model_version')
        download_url = payload.get('download_url') or payload.get('url') or payload.get('model_url')
        expected_hash = payload.get('hash') or payload.get('sha256')

        if not version or not download_url:
            self.report_status('failed', error='Missing version or download URL')
            return {'status': 'failed', 'reason': 'missing_fields'}

        self.report_status('notified')
        self.report_status('downloading')
        response = self.session.get(download_url, timeout=15)
        response.raise_for_status()

        if not verify_artifact(response.content, expected_hash):
            self.report_status('failed', error='SHA-256 verification failed')
            return {'status': 'failed', 'reason': 'hash_mismatch'}

        artifact_path = self.artifact_dir / f'{version}{Path(download_url).suffix or ".bin"}'
        artifact_path.write_bytes(response.content)

        self.report_status('completed', current_version=version)
        return {'status': 'completed', 'version': version, 'artifact_path': str(artifact_path)}


def main() -> None:
    parser = argparse.ArgumentParser(description='Simulate the device-side MQTT model update flow')
    parser.add_argument('--broker', required=True, help='MQTT broker host')
    parser.add_argument('--port', type=int, default=1883, help='MQTT broker port')
    parser.add_argument('--device-id', required=True, help='Target device ID')
    parser.add_argument('--topic-prefix', default='motor-vibration-monitor', help='MQTT topic prefix')
    parser.add_argument('--status-endpoint', required=True, help='Cloud deployment status endpoint')
    parser.add_argument('--artifact-dir', default='.device_simulator', help='Where to store downloaded artifacts')
    parser.add_argument('--username', default='', help='MQTT username')
    parser.add_argument('--password', default='', help='MQTT password')
    parser.add_argument('--oneshot', action='store_true', help='Exit after the first handled model update')
    args = parser.parse_args()

    simulator = DeviceUpdateSimulator(
        device_id=args.device_id,
        status_endpoint=args.status_endpoint,
        artifact_dir=args.artifact_dir,
    )

    client = mqtt.Client(client_id=f'{args.device_id}-simulator')
    if args.username:
        client.username_pw_set(args.username, args.password)

    topic = build_model_update_topic(args.topic_prefix, args.device_id)

    def on_connect(_client, _userdata, _flags, reason_code, _properties=None):
        if reason_code == 0:
            print(f'Subscribed to {topic}')
            _client.subscribe(topic, qos=1)
        else:
            raise RuntimeError(f'Failed to connect to MQTT broker: {reason_code}')

    def on_message(_client, _userdata, message):
        payload = json.loads(message.payload.decode('utf-8'))
        result = simulator.handle_update(payload)
        print(json.dumps(result, indent=2))
        if args.oneshot:
            _client.disconnect()

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, args.port, 60)
    client.loop_forever()


if __name__ == '__main__':
    main()
