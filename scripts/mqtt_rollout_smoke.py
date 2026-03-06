import asyncio
import json
import shutil
import sys
import time
from pathlib import Path
from threading import Thread

import paho.mqtt.client as mqtt
import requests
from werkzeug.serving import make_server

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from cloud.app import create_app
from scripts.simulate_device_update import DeviceUpdateSimulator, build_model_update_topic


def main() -> None:
    runtime_dir = ROOT / '.cloud_data_mqtt_e2e'
    if runtime_dir.exists():
        shutil.rmtree(runtime_dir)
    runtime_dir.mkdir(parents=True, exist_ok=True)

    broker_config = {
        'listeners': {'default': {'type': 'tcp', 'bind': '127.0.0.1:18883'}},
        'sys_interval': 0,
        'topic-check': {'enabled': False},
        'auth': {'allow-anonymous': True},
    }

    broker_state = {}
    broker_loop = asyncio.new_event_loop()

    async def start_broker():
        from amqtt.broker import Broker

        broker = Broker(broker_config)
        broker_state['broker'] = broker
        await broker.start()

    def broker_worker():
        asyncio.set_event_loop(broker_loop)
        broker_loop.run_until_complete(start_broker())
        broker_loop.run_forever()

    broker_thread = Thread(target=broker_worker, daemon=True)
    broker_thread.start()
    time.sleep(1)

    app = create_app(config={
        'STATE_DIR': str(runtime_dir),
        'START_BACKGROUND_WORKERS': False,
        'MQTT_NOTIFY_ENABLED': True,
        'MQTT_NOTIFY_BROKER': '127.0.0.1',
        'MQTT_NOTIFY_PORT': 18883,
        'MQTT_NOTIFY_TOPIC_PREFIX': 'motor-vibration-monitor',
        'PUBLIC_BASE_URL': 'http://127.0.0.1:5007',
    })
    server = make_server('127.0.0.1', 5007, app)
    server_thread = Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    simulator = DeviceUpdateSimulator(
        device_id='replay-device-001',
        status_endpoint='http://127.0.0.1:5007/api/deployments/device-status',
        artifact_dir=str(runtime_dir / 'device_artifacts'),
    )
    received = {}
    client = mqtt.Client(client_id='replay-device-001-sim')
    topic = build_model_update_topic('motor-vibration-monitor', 'replay-device-001')

    def on_connect(_client, _userdata, _flags, reason_code, _properties=None):
        if reason_code == 0:
            _client.subscribe(topic, qos=1)

    def on_message(_client, _userdata, message):
        payload = json.loads(message.payload.decode('utf-8'))
        received['payload'] = payload
        received['result'] = simulator.handle_update(payload)
        _client.disconnect()

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect('127.0.0.1', 18883, 60)
    client.loop_start()
    time.sleep(1)

    artifact_path = runtime_dir / 'artifact.bin'
    artifact_path.write_bytes(b'mqtt-model-artifact')
    services = app.extensions['services']
    services['deployment_manager'].register_model(str(artifact_path), 'v-mqtt-test', accuracy=0.99)
    deployment_id = services['deployment_manager'].deploy_model('v-mqtt-test', target_devices=['replay-device-001'])

    time.sleep(2)
    deployment_status = requests.get(
        f'http://127.0.0.1:5007/api/deployments/{deployment_id}',
        timeout=5,
    ).json()

    print(json.dumps({
        'deployment_id': deployment_id,
        'mqtt_payload': received.get('payload'),
        'simulator_result': received.get('result'),
        'deployment_status': deployment_status,
    }, indent=2))

    client.loop_stop()
    server.shutdown()
    server_thread.join(timeout=5)

    async def stop_broker():
        broker = broker_state.get('broker')
        if broker:
            await broker.shutdown()

    try:
        future = asyncio.run_coroutine_threadsafe(stop_broker(), broker_loop)
        future.result(timeout=5)
    except Exception:
        pass
    broker_loop.call_soon_threadsafe(broker_loop.stop)
    broker_thread.join(timeout=5)


if __name__ == '__main__':
    main()
