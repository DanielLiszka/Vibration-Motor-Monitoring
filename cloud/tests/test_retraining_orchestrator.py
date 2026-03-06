import time

from cloud.services.data_collector import DataCollector
from cloud.services.deployment_manager import DeploymentManager
from cloud.services.retraining_orchestrator import RetrainingConfig, RetrainingOrchestrator, RetrainingStatus


class FakeModel:
    def fit(self, *args, **kwargs):
        return {'history': 'ok'}

    def evaluate(self, *args, **kwargs):
        return {'loss': 0.2, 'accuracy': 0.93}

    def save(self, path):
        with open(path, 'wb') as file_obj:
            file_obj.write(b'model-bytes')


def fake_model_factory(input_dim):
    return FakeModel()


def test_retraining_registers_and_deploys_model(tmp_path):
    db_path = tmp_path / 'training_data.db'
    collector = DataCollector(database_path=str(db_path))
    deployment = DeploymentManager(models_dir=str(tmp_path / 'models'), registry_file=str(tmp_path / 'models' / 'registry.json'))

    for index in range(12):
        collector.receive_sample({
            'device_id': 'device-train',
            'features': [float(index)] * 10,
            'predicted_label': index % 5,
            'confidence': 0.9,
            'label_source': 0,
            'timestamp': index,
            'true_label': index % 5,
        })

    orchestrator = RetrainingOrchestrator(
        data_collector=collector,
        deployment_manager=deployment,
        model_factory=fake_model_factory,
        config=RetrainingConfig(min_samples_for_retraining=5, min_new_samples=1, min_labeled_ratio=0.1, epochs=1),
        models_dir=str(tmp_path / 'models'),
    )

    orchestrator.trigger_retraining('test')
    deadline = time.time() + 5
    while time.time() < deadline:
        job = orchestrator.get_job_status()
        if job and job['status'] in {RetrainingStatus.COMPLETED.value, RetrainingStatus.FAILED.value}:
            break
        time.sleep(0.1)

    job = orchestrator.get_job_status()
    assert job is not None
    assert job['status'] == RetrainingStatus.COMPLETED.value
    assert job['num_samples'] >= 2
    assert deployment.get_production_model()['version'] == job['model_version']
    assert collector.get_stats_summary()['used_for_training'] > 0
