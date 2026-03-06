from cloud.services.mqtt_notifier import MQTTDeploymentNotifier


class FakeClient:
    def __init__(self):
        self.connected = False
        self.published = []

    def connect(self, broker, port, keepalive):
        self.connected = True
        self.connect_args = (broker, port, keepalive)

    def loop_start(self):
        pass

    def publish(self, topic, payload, qos=0, retain=False):
        self.published.append((topic, payload, qos, retain))


def test_notifier_builds_absolute_download_url():
    notifier = MQTTDeploymentNotifier(
        broker='broker.example',
        topic_prefix='motor-vibration-monitor',
        public_base_url='http://127.0.0.1:5000',
    )
    fake_client = FakeClient()
    notifier.client = fake_client

    notifier.publish_model_update('device-1', {
        'version': 'v1',
        'download_url': '/api/models/v1/download',
        'hash': 'abc123',
    })

    assert fake_client.published
    topic, payload, qos, retain = fake_client.published[0]
    assert topic == 'motor-vibration-monitor/device-1/models/update'
    assert '"download_url": "http://127.0.0.1:5000/api/models/v1/download"' in payload
    assert '"sha256": "abc123"' in payload
