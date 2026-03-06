import json
from typing import Any, Dict, Optional

import paho.mqtt.client as mqtt


class MQTTDeploymentNotifier:
    def __init__(
        self,
        broker: str,
        port: int = 1883,
        username: Optional[str] = None,
        password: Optional[str] = None,
        topic_prefix: str = 'motor-vibration-monitor',
        public_base_url: Optional[str] = None,
        client_id: str = 'motor-monitor-cloud',
    ):
        self.broker = broker
        self.port = port
        self.username = username
        self.password = password
        self.topic_prefix = topic_prefix.strip('/')
        self.public_base_url = public_base_url.rstrip('/') if public_base_url else None
        self.client = mqtt.Client(client_id=client_id)
        self.connected = False

        if username:
            self.client.username_pw_set(username, password)

    def connect(self) -> None:
        if self.connected:
            return
        self.client.connect(self.broker, self.port, 60)
        self.client.loop_start()
        self.connected = True

    def disconnect(self) -> None:
        if not self.connected:
            return
        self.client.loop_stop()
        self.client.disconnect()
        self.connected = False

    def build_model_update_topic(self, device_id: str) -> str:
        return f'{self.topic_prefix}/{device_id}/models/update'

    def normalize_update_payload(self, update_info: Dict[str, Any]) -> Dict[str, Any]:
        payload = dict(update_info)
        download_url = payload.get('download_url') or payload.get('url') or payload.get('model_url')
        if download_url and self.public_base_url and download_url.startswith('/'):
            payload['download_url'] = f'{self.public_base_url}{download_url}'
        elif download_url:
            payload['download_url'] = download_url

        if 'download_url' in payload:
            payload['url'] = payload['download_url']
            payload['model_url'] = payload['download_url']
        if 'hash' in payload:
            payload['sha256'] = payload['hash']
        return payload

    def publish_model_update(self, device_id: str, update_info: Dict[str, Any]) -> None:
        self.connect()
        payload = self.normalize_update_payload(update_info)
        self.client.publish(
            self.build_model_update_topic(device_id),
            json.dumps(payload),
            qos=1,
            retain=False,
        )
