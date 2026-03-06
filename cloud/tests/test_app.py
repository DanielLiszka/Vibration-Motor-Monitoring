from cloud.app import create_app


class FakeModel:
    def fit(self, *args, **kwargs):
        return {'history': 'ok'}

    def evaluate(self, *args, **kwargs):
        return {'loss': 0.1, 'accuracy': 0.95}

    def save(self, path):
        with open(path, 'wb') as file_obj:
            file_obj.write(b'fake-model')


def fake_model_factory(input_dim):
    return FakeModel()


def test_app_ingest_label_and_download_flow(tmp_path):
    app = create_app(data_dir=str(tmp_path), model_factory=fake_model_factory, start_workers=False)
    client = app.test_client()

    response = client.get('/')
    assert response.status_code == 302
    assert response.headers['Location'].endswith('/ui/labeling')

    ingest = client.post('/api/data/sample', json={
        'device_id': 'device-1',
        'features': [0.5] * 10,
        'predicted_label': 2,
        'confidence': 0.15,
        'label_source': 0,
        'timestamp': 1234,
    })
    assert ingest.status_code == 200
    assert ingest.get_json()['status'] == 'ok'

    next_task = client.get('/api/labeling/next?labeler_id=tester')
    payload = next_task.get_json()
    assert payload['status'] == 'ok'
    assert payload['task'] is not None

    submit = client.post('/api/labeling/submit', json={
        'task_id': payload['task']['task_id'],
        'label': 4,
        'labeler_id': 'tester',
        'confidence': 1.0,
    })
    assert submit.status_code == 200
    assert submit.get_json()['status'] == 'ok'

    services = app.extensions['motor_monitor']
    artifact_path = tmp_path / 'artifact.bin'
    artifact_path.write_bytes(b'binary-model')
    services['deployment_manager'].register_model(str(artifact_path), 'vtest', accuracy=0.91)

    download = client.get('/api/models/vtest/download')
    assert download.status_code == 200
    assert download.data == b'binary-model'
