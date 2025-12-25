import json
import logging
from datetime import datetime
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass, asdict
from pathlib import Path
import sqlite3
import threading
import queue
logger = logging.getLogger(__name__)

@dataclass
class TrainingSample:
    device_id: str
    features: List[float]
    predicted_label: int
    confidence: float
    label_source: int
    timestamp: int
    received_at: datetime = None
    true_label: Optional[int] = None
    sample_id: Optional[int] = None

    def to_dict(self) -> Dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict) -> 'TrainingSample':
        return cls(device_id=data.get('device_id', ''), features=data.get('features', []), predicted_label=data.get('predicted_label', 0), confidence=data.get('confidence', 0.0), label_source=data.get('label_source', 0), timestamp=data.get('timestamp', 0), received_at=datetime.now(), true_label=data.get('true_label'), sample_id=data.get('sample_id'))

@dataclass
class DeviceStats:
    device_id: str
    total_samples: int
    labeled_samples: int
    last_seen: datetime
    model_version: str

class DataCollector:

    def __init__(self, database_path: str='./data/training_data.db', buffer_size: int=1000):
        self.database_path = Path(database_path)
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        self.buffer: List[TrainingSample] = []
        self.buffer_size = buffer_size
        self.buffer_lock = threading.Lock()
        self.sample_queue = queue.Queue()
        self.running = False
        self.worker_thread = None
        self._init_database()
        self.on_sample_received: Optional[Callable] = None
        self.on_batch_stored: Optional[Callable] = None

    def _init_database(self):
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        cursor.execute('\n            CREATE TABLE IF NOT EXISTS samples (\n                id INTEGER PRIMARY KEY AUTOINCREMENT,\n                device_id TEXT NOT NULL,\n                features TEXT NOT NULL,\n                predicted_label INTEGER NOT NULL,\n                confidence REAL NOT NULL,\n                label_source INTEGER NOT NULL,\n                timestamp INTEGER NOT NULL,\n                received_at TEXT NOT NULL,\n                true_label INTEGER,\n                used_for_training INTEGER DEFAULT 0,\n                created_at TEXT DEFAULT CURRENT_TIMESTAMP\n            )\n        ')
        cursor.execute('\n            CREATE TABLE IF NOT EXISTS devices (\n                device_id TEXT PRIMARY KEY,\n                total_samples INTEGER DEFAULT 0,\n                labeled_samples INTEGER DEFAULT 0,\n                last_seen TEXT,\n                model_version TEXT,\n                created_at TEXT DEFAULT CURRENT_TIMESTAMP\n            )\n        ')
        cursor.execute('\n            CREATE INDEX IF NOT EXISTS idx_samples_device\n            ON samples (device_id, received_at)\n        ')
        cursor.execute('\n            CREATE INDEX IF NOT EXISTS idx_samples_unlabeled\n            ON samples (true_label) WHERE true_label IS NULL\n        ')
        conn.commit()
        conn.close()

    def start(self):
        self.running = True
        self.worker_thread = threading.Thread(target=self._worker_loop, daemon=True)
        self.worker_thread.start()
        logger.info('Data collector started')

    def stop(self):
        self.running = False
        if self.worker_thread:
            self.worker_thread.join(timeout=5)
        self._flush_buffer()
        logger.info('Data collector stopped')

    def _worker_loop(self):
        while self.running:
            try:
                while not self.sample_queue.empty():
                    sample = self.sample_queue.get(timeout=1)
                    self._add_to_buffer(sample)
            except queue.Empty:
                pass
            if len(self.buffer) >= self.buffer_size:
                self._flush_buffer()

    def receive_sample(self, sample_data: Dict) -> bool:
        try:
            sample = TrainingSample.from_dict(sample_data)
            self.sample_queue.put(sample)
            if self.on_sample_received:
                self.on_sample_received(sample)
            return True
        except Exception as e:
            logger.error(f'Error receiving sample: {e}')
            return False

    def receive_batch(self, batch_data: Dict) -> int:
        device_id = batch_data.get('device_id', '')
        samples = batch_data.get('samples', [])
        count = 0
        for sample_data in samples:
            sample_data['device_id'] = device_id
            if self.receive_sample(sample_data):
                count += 1
        logger.info(f'Received {count} samples from {device_id}')
        return count

    def _add_to_buffer(self, sample: TrainingSample):
        with self.buffer_lock:
            self.buffer.append(sample)

    def _flush_buffer(self):
        with self.buffer_lock:
            if not self.buffer:
                return
            samples_to_store = self.buffer.copy()
            self.buffer.clear()
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        try:
            for sample in samples_to_store:
                cursor.execute('\n                    INSERT INTO samples\n                    (device_id, features, predicted_label, confidence,\n                     label_source, timestamp, received_at, true_label)\n                    VALUES (?, ?, ?, ?, ?, ?, ?, ?)\n                ', (sample.device_id, json.dumps(sample.features), sample.predicted_label, sample.confidence, sample.label_source, sample.timestamp, sample.received_at.isoformat() if sample.received_at else datetime.now().isoformat(), sample.true_label))
                cursor.execute('\n                    INSERT INTO devices (device_id, total_samples, last_seen)\n                    VALUES (?, 1, ?)\n                    ON CONFLICT(device_id) DO UPDATE SET\n                        total_samples = total_samples + 1,\n                        last_seen = excluded.last_seen\n                ', (sample.device_id, datetime.now().isoformat()))
            conn.commit()
            logger.info(f'Flushed {len(samples_to_store)} samples to database')
            if self.on_batch_stored:
                self.on_batch_stored(len(samples_to_store))
        except Exception as e:
            logger.error(f'Error flushing buffer: {e}')
            conn.rollback()
        finally:
            conn.close()

    def get_unlabeled_samples(self, limit: int=100, device_id: str=None) -> List[Dict]:
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        query = '\n            SELECT id, device_id, features, predicted_label, confidence,\n                   label_source, timestamp, received_at\n            FROM samples\n            WHERE true_label IS NULL\n        '
        params = []
        if device_id:
            query += ' AND device_id = ?'
            params.append(device_id)
        query += ' ORDER BY confidence ASC LIMIT ?'
        params.append(limit)
        cursor.execute(query, params)
        rows = cursor.fetchall()
        conn.close()
        samples = []
        for row in rows:
            samples.append({'sample_id': row[0], 'device_id': row[1], 'features': json.loads(row[2]), 'predicted_label': row[3], 'confidence': row[4], 'label_source': row[5], 'timestamp': row[6], 'received_at': row[7]})
        return samples

    def set_label(self, sample_id: int, label: int) -> bool:
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        try:
            cursor.execute('\n                UPDATE samples SET true_label = ? WHERE id = ?\n            ', (label, sample_id))
            cursor.execute('\n                UPDATE devices SET labeled_samples = labeled_samples + 1\n                WHERE device_id = (SELECT device_id FROM samples WHERE id = ?)\n            ', (sample_id,))
            conn.commit()
            return True
        except Exception as e:
            logger.error(f'Error setting label: {e}')
            return False
        finally:
            conn.close()

    def get_training_dataset(self, min_confidence: float=0.0, labeled_only: bool=True, limit: int=None) -> List[Dict]:
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        query = '\n            SELECT id, device_id, features, predicted_label, confidence,\n                   true_label, used_for_training\n            FROM samples\n            WHERE confidence >= ?\n        '
        params = [min_confidence]
        if labeled_only:
            query += ' AND true_label IS NOT NULL'
        if limit:
            query += ' LIMIT ?'
            params.append(limit)
        cursor.execute(query, params)
        rows = cursor.fetchall()
        conn.close()
        samples = []
        for row in rows:
            samples.append({'sample_id': row[0], 'device_id': row[1], 'features': json.loads(row[2]), 'predicted_label': row[3], 'confidence': row[4], 'true_label': row[5] if row[5] is not None else row[3], 'used_for_training': bool(row[6])})
        return samples

    def mark_used_for_training(self, sample_ids: List[int]):
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        placeholders = ','.join('?' * len(sample_ids))
        cursor.execute(f'\n            UPDATE samples SET used_for_training = 1\n            WHERE id IN ({placeholders})\n        ', sample_ids)
        conn.commit()
        conn.close()

    def get_device_stats(self, device_id: str=None) -> List[DeviceStats]:
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        if device_id:
            cursor.execute('\n                SELECT device_id, total_samples, labeled_samples,\n                       last_seen, model_version\n                FROM devices WHERE device_id = ?\n            ', (device_id,))
        else:
            cursor.execute('\n                SELECT device_id, total_samples, labeled_samples,\n                       last_seen, model_version\n                FROM devices\n            ')
        rows = cursor.fetchall()
        conn.close()
        stats = []
        for row in rows:
            stats.append(DeviceStats(device_id=row[0], total_samples=row[1], labeled_samples=row[2], last_seen=datetime.fromisoformat(row[3]) if row[3] else None, model_version=row[4]))
        return stats

    def get_stats_summary(self) -> Dict:
        conn = sqlite3.connect(str(self.database_path))
        cursor = conn.cursor()
        cursor.execute('SELECT COUNT(*) FROM samples')
        total_samples = cursor.fetchone()[0]
        cursor.execute('SELECT COUNT(*) FROM samples WHERE true_label IS NOT NULL')
        labeled_samples = cursor.fetchone()[0]
        cursor.execute('SELECT COUNT(DISTINCT device_id) FROM devices')
        total_devices = cursor.fetchone()[0]
        cursor.execute('SELECT COUNT(*) FROM samples WHERE used_for_training = 1')
        used_for_training = cursor.fetchone()[0]
        conn.close()
        return {'total_samples': total_samples, 'labeled_samples': labeled_samples, 'unlabeled_samples': total_samples - labeled_samples, 'total_devices': total_devices, 'used_for_training': used_for_training, 'labeling_rate': labeled_samples / total_samples if total_samples > 0 else 0}