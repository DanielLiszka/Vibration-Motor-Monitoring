import json
import logging
import sqlite3
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Callable, Dict, List, Optional

logger = logging.getLogger(__name__)


class LabelingPriority(Enum):
    LOW = 1
    MEDIUM = 2
    HIGH = 3
    URGENT = 4


class LabelingStatus(Enum):
    PENDING = 'pending'
    IN_PROGRESS = 'in_progress'
    LABELED = 'labeled'
    SKIPPED = 'skipped'
    DISPUTED = 'disputed'


FAULT_LABELS = {
    0: 'Normal',
    1: 'Imbalance',
    2: 'Misalignment',
    3: 'Bearing Fault',
    4: 'Looseness',
}


@dataclass
class LabelingTask:
    task_id: int
    sample_id: int
    device_id: str
    features: List[float]
    predicted_label: int
    confidence: float
    priority: LabelingPriority
    status: LabelingStatus
    created_at: datetime
    assigned_to: Optional[str] = None
    assigned_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None
    assigned_label: Optional[int] = None
    labeler_confidence: float = 1.0
    notes: str = ''


class LabelingService:
    def __init__(self, data_collector=None, database_path: str='./data/labeling.db'):
        self.data_collector = data_collector
        self.database_path = Path(database_path)
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        self.selection_strategy = 'uncertainty'
        self.batch_size = 50
        self.on_label_assigned: Optional[Callable] = None
        self._init_database()

    def _connect(self):
        conn = sqlite3.connect(str(self.database_path))
        conn.row_factory = sqlite3.Row
        return conn

    def _init_database(self):
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute(
            '''
            CREATE TABLE IF NOT EXISTS tasks (
                task_id INTEGER PRIMARY KEY AUTOINCREMENT,
                sample_id INTEGER NOT NULL,
                device_id TEXT NOT NULL,
                features TEXT NOT NULL,
                predicted_label INTEGER NOT NULL,
                confidence REAL NOT NULL,
                priority INTEGER NOT NULL,
                status TEXT NOT NULL,
                created_at TEXT NOT NULL,
                assigned_to TEXT,
                assigned_at TEXT,
                completed_at TEXT,
                assigned_label INTEGER,
                labeler_confidence REAL DEFAULT 1.0,
                notes TEXT DEFAULT ''
            )
            '''
        )
        cursor.execute(
            '''
            CREATE INDEX IF NOT EXISTS idx_tasks_status_priority
            ON tasks (status, priority, created_at)
            '''
        )
        cursor.execute(
            '''
            CREATE INDEX IF NOT EXISTS idx_tasks_sample_id
            ON tasks (sample_id)
            '''
        )
        conn.commit()
        conn.close()

    def create_labeling_batch(self, num_samples: int=None, strategy: str=None, device_id: str=None) -> List[int]:
        if num_samples is None:
            num_samples = self.batch_size

        strategy = strategy or self.selection_strategy
        samples = self.data_collector.get_unlabeled_samples(limit=num_samples * 3, device_id=device_id) if self.data_collector else []
        if strategy == 'uncertainty':
            samples = sorted(samples, key=lambda item: item['confidence'])
        elif strategy == 'diversity':
            samples = self._select_diverse(samples, num_samples)

        task_ids = []
        for sample in samples[:num_samples]:
            if self._sample_has_open_task(sample['sample_id']):
                continue
            task = self._create_task(sample)
            task_ids.append(task.task_id)

        logger.info('Created %s labeling tasks using %s strategy', len(task_ids), strategy)
        return task_ids

    def _select_diverse(self, samples: List[Dict], num_samples: int) -> List[Dict]:
        if len(samples) <= num_samples:
            return samples
        ordered = sorted(samples, key=lambda item: item['confidence'])
        step = len(ordered) / num_samples
        return [ordered[int(index * step)] for index in range(num_samples)]

    def _sample_has_open_task(self, sample_id: int) -> bool:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute(
            '''
            SELECT 1 FROM tasks
            WHERE sample_id = ? AND status IN (?, ?)
            LIMIT 1
            ''',
            (sample_id, LabelingStatus.PENDING.value, LabelingStatus.IN_PROGRESS.value),
        )
        row = cursor.fetchone()
        conn.close()
        return row is not None

    def _create_task(self, sample: Dict) -> LabelingTask:
        confidence = sample.get('confidence', 0.5)
        if confidence < 0.2:
            priority = LabelingPriority.URGENT
        elif confidence < 0.4:
            priority = LabelingPriority.HIGH
        elif confidence < 0.6:
            priority = LabelingPriority.MEDIUM
        else:
            priority = LabelingPriority.LOW

        created_at = datetime.now()
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute(
            '''
            INSERT INTO tasks (
                sample_id, device_id, features, predicted_label, confidence,
                priority, status, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ''',
            (
                sample['sample_id'],
                sample.get('device_id', ''),
                json.dumps(sample.get('features', [])),
                sample.get('predicted_label', 0),
                confidence,
                priority.value,
                LabelingStatus.PENDING.value,
                created_at.isoformat(),
            ),
        )
        task_id = cursor.lastrowid
        conn.commit()
        conn.close()

        return LabelingTask(
            task_id=task_id,
            sample_id=sample['sample_id'],
            device_id=sample.get('device_id', ''),
            features=sample.get('features', []),
            predicted_label=sample.get('predicted_label', 0),
            confidence=confidence,
            priority=priority,
            status=LabelingStatus.PENDING,
            created_at=created_at,
        )

    def get_next_task(self, labeler_id: str=None) -> Optional[Dict]:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute(
            '''
            SELECT * FROM tasks
            WHERE status = ?
            ORDER BY priority DESC, created_at ASC
            LIMIT 1
            ''',
            (LabelingStatus.PENDING.value,),
        )
        row = cursor.fetchone()
        if not row:
            conn.close()
            if self.data_collector:
                created = self.create_labeling_batch()
                if created:
                    return self.get_next_task(labeler_id)
            return None

        if labeler_id:
            assigned_at = datetime.now().isoformat()
            cursor.execute(
                '''
                UPDATE tasks
                SET assigned_to = ?, assigned_at = ?, status = ?
                WHERE task_id = ?
                ''',
                (labeler_id, assigned_at, LabelingStatus.IN_PROGRESS.value, row['task_id']),
            )
            conn.commit()
            cursor.execute('SELECT * FROM tasks WHERE task_id = ?', (row['task_id'],))
            row = cursor.fetchone()

        task = self._row_to_task(row)
        conn.close()
        return self._task_to_dict(task)

    def get_tasks_batch(self, labeler_id: str=None, batch_size: int=10) -> List[Dict]:
        tasks = []
        for _ in range(batch_size):
            task = self.get_next_task(labeler_id)
            if not task:
                break
            tasks.append(task)
        return tasks

    def submit_label(self, task_id: int, label: int, labeler_id: str=None,
                     confidence: float=1.0, notes: str='') -> bool:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute('SELECT * FROM tasks WHERE task_id = ?', (task_id,))
        row = cursor.fetchone()
        if not row:
            conn.close()
            return False

        completed_at = datetime.now().isoformat()
        assigned_to = labeler_id or row['assigned_to']
        cursor.execute(
            '''
            UPDATE tasks
            SET assigned_label = ?, labeler_confidence = ?, notes = ?, status = ?,
                completed_at = ?, assigned_to = COALESCE(?, assigned_to)
            WHERE task_id = ?
            ''',
            (
                label,
                confidence,
                notes,
                LabelingStatus.LABELED.value,
                completed_at,
                assigned_to,
                task_id,
            ),
        )
        conn.commit()
        cursor.execute('SELECT * FROM tasks WHERE task_id = ?', (task_id,))
        task = self._row_to_task(cursor.fetchone())
        conn.close()

        if self.data_collector:
            self.data_collector.set_label(task.sample_id, label)
        if self.on_label_assigned:
            self.on_label_assigned(task)

        logger.info('Task %s labeled as %s', task_id, FAULT_LABELS.get(label, label))
        return True

    def skip_task(self, task_id: int, reason: str='') -> bool:
        return self._update_terminal_task(task_id, LabelingStatus.SKIPPED, notes=reason)

    def dispute_label(self, task_id: int, reason: str) -> bool:
        return self._update_terminal_task(task_id, LabelingStatus.DISPUTED, notes=reason)

    def _update_terminal_task(self, task_id: int, status: LabelingStatus, notes: str='') -> bool:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute(
            '''
            UPDATE tasks
            SET status = ?, notes = ?, completed_at = ?
            WHERE task_id = ?
            ''',
            (status.value, notes, datetime.now().isoformat(), task_id),
        )
        changed = cursor.rowcount > 0
        conn.commit()
        conn.close()
        return changed

    def get_task(self, task_id: int) -> Optional[Dict]:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute('SELECT * FROM tasks WHERE task_id = ?', (task_id,))
        row = cursor.fetchone()
        conn.close()
        return self._task_to_dict(self._row_to_task(row)) if row else None

    def get_stats(self) -> Dict:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute('SELECT * FROM tasks')
        rows = cursor.fetchall()
        conn.close()

        counts = defaultdict(int)
        agreement = 0
        completed = 0
        total_time = 0.0
        timed = 0
        for row in rows:
            counts[row['status']] += 1
            if row['status'] == LabelingStatus.LABELED.value:
                completed += 1
                if row['assigned_label'] == row['predicted_label']:
                    agreement += 1
            if row['assigned_at'] and row['completed_at']:
                start = datetime.fromisoformat(row['assigned_at'])
                end = datetime.fromisoformat(row['completed_at'])
                total_time += (end - start).total_seconds()
                timed += 1

        return {
            'total_tasks': len(rows),
            'pending': counts[LabelingStatus.PENDING.value],
            'in_progress': counts[LabelingStatus.IN_PROGRESS.value],
            'completed': counts[LabelingStatus.LABELED.value],
            'skipped': counts[LabelingStatus.SKIPPED.value],
            'disputed': counts[LabelingStatus.DISPUTED.value],
            'agreement_with_model': agreement / completed if completed else 0,
            'avg_labeling_time_seconds': total_time / timed if timed else 0,
        }

    def get_labeler_stats(self, labeler_id: str=None) -> Dict:
        conn = self._connect()
        cursor = conn.cursor()
        params: tuple = ()
        query = 'SELECT * FROM tasks WHERE assigned_to IS NOT NULL'
        if labeler_id:
            query += ' AND assigned_to = ?'
            params = (labeler_id,)
        cursor.execute(query, params)
        rows = cursor.fetchall()
        conn.close()

        stats = defaultdict(lambda: {'completed': 0, 'avg_time': 0.0, 'agreement_rate': 0.0})
        for row in rows:
            labeler = row['assigned_to']
            if not labeler:
                continue
            info = stats[labeler]
            if row['status'] == LabelingStatus.LABELED.value:
                info['completed'] += 1
                if row['assigned_label'] == row['predicted_label']:
                    info['agreement_rate'] += 1
            if row['assigned_at'] and row['completed_at']:
                info['avg_time'] += (datetime.fromisoformat(row['completed_at']) - datetime.fromisoformat(row['assigned_at'])).total_seconds()

        for info in stats.values():
            if info['completed']:
                info['agreement_rate'] /= info['completed']
                info['avg_time'] /= info['completed']

        if labeler_id:
            return dict(stats.get(labeler_id, {}))
        return {labeler: dict(info) for labeler, info in stats.items()}

    def export_labeled_data(self, format: str='json', include_skipped: bool=False) -> str:
        conn = self._connect()
        cursor = conn.cursor()
        statuses = [LabelingStatus.LABELED.value]
        if include_skipped:
            statuses.append(LabelingStatus.SKIPPED.value)
        placeholders = ','.join('?' * len(statuses))
        cursor.execute(f'SELECT * FROM tasks WHERE status IN ({placeholders})', statuses)
        rows = cursor.fetchall()
        conn.close()

        tasks = [self._row_to_task(row) for row in rows]
        if format == 'csv':
            lines = ['sample_id,device_id,predicted_label,assigned_label,confidence,labeler_confidence,status']
            for task in tasks:
                lines.append(
                    f'{task.sample_id},{task.device_id},{task.predicted_label},{task.assigned_label},'
                    f'{task.confidence},{task.labeler_confidence},{task.status.value}'
                )
            return '\n'.join(lines)

        return json.dumps([self._task_to_dict(task) for task in tasks], indent=2)

    def get_label_distribution(self) -> Dict[str, int]:
        conn = self._connect()
        cursor = conn.cursor()
        cursor.execute(
            '''
            SELECT assigned_label, COUNT(*) FROM tasks
            WHERE status = ? AND assigned_label IS NOT NULL
            GROUP BY assigned_label
            ''',
            (LabelingStatus.LABELED.value,),
        )
        rows = cursor.fetchall()
        conn.close()
        return {FAULT_LABELS.get(row[0], f'Unknown ({row[0]})'): row[1] for row in rows}

    def _row_to_task(self, row) -> LabelingTask:
        return LabelingTask(
            task_id=row['task_id'],
            sample_id=row['sample_id'],
            device_id=row['device_id'],
            features=json.loads(row['features']),
            predicted_label=row['predicted_label'],
            confidence=row['confidence'],
            priority=LabelingPriority(row['priority']),
            status=LabelingStatus(row['status']),
            created_at=datetime.fromisoformat(row['created_at']),
            assigned_to=row['assigned_to'],
            assigned_at=datetime.fromisoformat(row['assigned_at']) if row['assigned_at'] else None,
            completed_at=datetime.fromisoformat(row['completed_at']) if row['completed_at'] else None,
            assigned_label=row['assigned_label'],
            labeler_confidence=row['labeler_confidence'],
            notes=row['notes'] or '',
        )

    def _task_to_dict(self, task: LabelingTask) -> Dict:
        return {
            'task_id': task.task_id,
            'sample_id': task.sample_id,
            'device_id': task.device_id,
            'features': task.features,
            'predicted_label': task.predicted_label,
            'predicted_label_name': FAULT_LABELS.get(task.predicted_label, 'Unknown'),
            'confidence': task.confidence,
            'priority': task.priority.name,
            'status': task.status.value,
            'created_at': task.created_at.isoformat(),
            'assigned_to': task.assigned_to,
            'assigned_at': task.assigned_at.isoformat() if task.assigned_at else None,
            'completed_at': task.completed_at.isoformat() if task.completed_at else None,
            'assigned_label': task.assigned_label,
            'assigned_label_name': FAULT_LABELS.get(task.assigned_label) if task.assigned_label is not None else None,
            'labeler_confidence': task.labeler_confidence,
            'notes': task.notes,
            'available_labels': FAULT_LABELS,
        }
