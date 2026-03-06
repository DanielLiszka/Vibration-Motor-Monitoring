from cloud.services.data_collector import DataCollector
from tools.train_model import load_dataset_from_sqlite


def test_load_dataset_from_sqlite_reads_labeled_samples(tmp_path):
    db_path = tmp_path / 'training_data.db'
    collector = DataCollector(database_path=str(db_path))

    collector.receive_sample({
        'device_id': 'device-loader',
        'features': [0.3] * 10,
        'predicted_label': 1,
        'confidence': 0.8,
        'label_source': 0,
        'timestamp': 10,
        'true_label': 1,
    })

    X, y = load_dataset_from_sqlite(str(db_path))
    assert X.shape == (1, 10)
    assert y.tolist() == [1]
