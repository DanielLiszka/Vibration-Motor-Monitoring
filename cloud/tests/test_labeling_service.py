from cloud.services.data_collector import DataCollector
from cloud.services.labeling_service import LabelingService, LabelingStatus


def test_labeling_tasks_persist_across_service_restart(tmp_path):
    db_path = tmp_path / 'training_data.db'
    collector = DataCollector(database_path=str(db_path))

    collector.receive_sample({
        'device_id': 'device-a',
        'features': [0.1] * 10,
        'predicted_label': 1,
        'confidence': 0.22,
        'label_source': 0,
        'timestamp': 1000,
    })

    service = LabelingService(data_collector=collector, database_path=str(db_path))
    task_ids = service.create_labeling_batch(num_samples=1)
    assert len(task_ids) == 1

    reloaded = LabelingService(data_collector=collector, database_path=str(db_path))
    task = reloaded.get_task(task_ids[0])
    assert task is not None
    assert task['status'] == LabelingStatus.PENDING.value

    assert reloaded.submit_label(task_ids[0], 3, labeler_id='tester', confidence=0.9)

    after_label = LabelingService(data_collector=collector, database_path=str(db_path))
    stats = after_label.get_stats()
    assert stats['completed'] == 1
    assert stats['pending'] == 0


def test_get_next_task_returns_none_without_recursive_loop(tmp_path):
    db_path = tmp_path / 'training_data.db'
    collector = DataCollector(database_path=str(db_path))
    service = LabelingService(data_collector=collector, database_path=str(tmp_path / 'labeling.db'))

    assert service.get_next_task('tester') is None
