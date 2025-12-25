import logging
from datetime import datetime
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass
from enum import Enum
import json
from collections import defaultdict
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

@dataclass
class LabelingStats:
    total_tasks: int
    pending: int
    in_progress: int
    completed: int
    skipped: int
    disputed: int
    accuracy_vs_predicted: float
    avg_labeling_time_seconds: float
FAULT_LABELS = {0: 'Normal', 1: 'Imbalance', 2: 'Misalignment', 3: 'Bearing Fault', 4: 'Looseness'}

class LabelingService:

    def __init__(self, data_collector=None):
        self.data_collector = data_collector
        self.tasks: Dict[int, LabelingTask] = {}
        self.task_counter = 0
        self.labeler_stats: Dict[str, Dict] = defaultdict(lambda: {'completed': 0, 'avg_time': 0, 'agreement_rate': 0})
        self.selection_strategy = 'uncertainty'
        self.batch_size = 50
        self.on_label_assigned: Optional[Callable] = None

    def create_labeling_batch(self, num_samples: int=None, strategy: str=None, device_id: str=None) -> List[int]:
        if num_samples is None:
            num_samples = self.batch_size
        strategy = strategy or self.selection_strategy
        if self.data_collector:
            samples = self.data_collector.get_unlabeled_samples(limit=num_samples * 3, device_id=device_id)
        else:
            samples = []
        if strategy == 'uncertainty':
            samples = sorted(samples, key=lambda x: x['confidence'])
        elif strategy == 'diversity':
            samples = self._select_diverse(samples, num_samples)
        task_ids = []
        for sample in samples[:num_samples]:
            if self._sample_has_task(sample['sample_id']):
                continue
            task = self._create_task(sample)
            task_ids.append(task.task_id)
        logger.info(f'Created {len(task_ids)} labeling tasks using {strategy} strategy')
        return task_ids

    def _select_diverse(self, samples: List[Dict], num_samples: int) -> List[Dict]:
        if len(samples) <= num_samples:
            return samples
        sorted_samples = sorted(samples, key=lambda x: x['confidence'])
        step = len(sorted_samples) / num_samples
        selected = []
        for i in range(num_samples):
            idx = int(i * step)
            selected.append(sorted_samples[idx])
        return selected

    def _sample_has_task(self, sample_id: int) -> bool:
        for task in self.tasks.values():
            if task.sample_id == sample_id and task.status in [LabelingStatus.PENDING, LabelingStatus.IN_PROGRESS]:
                return True
        return False

    def _create_task(self, sample: Dict) -> LabelingTask:
        self.task_counter += 1
        confidence = sample.get('confidence', 0.5)
        if confidence < 0.3:
            priority = LabelingPriority.HIGH
        elif confidence < 0.5:
            priority = LabelingPriority.MEDIUM
        else:
            priority = LabelingPriority.LOW
        task = LabelingTask(task_id=self.task_counter, sample_id=sample['sample_id'], device_id=sample.get('device_id', ''), features=sample.get('features', []), predicted_label=sample.get('predicted_label', 0), confidence=confidence, priority=priority, status=LabelingStatus.PENDING, created_at=datetime.now())
        self.tasks[task.task_id] = task
        return task

    def get_next_task(self, labeler_id: str=None) -> Optional[Dict]:
        pending = [t for t in self.tasks.values() if t.status == LabelingStatus.PENDING]
        if not pending:
            return None
        pending.sort(key=lambda t: (-t.priority.value, t.created_at))
        task = pending[0]
        if labeler_id:
            task.assigned_to = labeler_id
            task.assigned_at = datetime.now()
            task.status = LabelingStatus.IN_PROGRESS
        return self._task_to_dict(task)

    def get_tasks_batch(self, labeler_id: str=None, batch_size: int=10) -> List[Dict]:
        tasks = []
        for _ in range(batch_size):
            task = self.get_next_task(labeler_id)
            if task:
                tasks.append(task)
            else:
                break
        return tasks

    def submit_label(self, task_id: int, label: int, labeler_id: str=None, confidence: float=1.0, notes: str='') -> bool:
        if task_id not in self.tasks:
            return False
        task = self.tasks[task_id]
        task.assigned_label = label
        task.labeler_confidence = confidence
        task.notes = notes
        task.status = LabelingStatus.LABELED
        task.completed_at = datetime.now()
        if labeler_id:
            task.assigned_to = labeler_id
        if self.data_collector:
            self.data_collector.set_label(task.sample_id, label)
        if task.assigned_to:
            self._update_labeler_stats(task)
        if self.on_label_assigned:
            self.on_label_assigned(task)
        logger.info(f"Task {task_id} labeled as {FAULT_LABELS.get(label, label)} by {labeler_id or 'unknown'}")
        return True

    def skip_task(self, task_id: int, reason: str='') -> bool:
        if task_id not in self.tasks:
            return False
        task = self.tasks[task_id]
        task.status = LabelingStatus.SKIPPED
        task.notes = reason
        task.completed_at = datetime.now()
        return True

    def dispute_label(self, task_id: int, reason: str) -> bool:
        if task_id not in self.tasks:
            return False
        task = self.tasks[task_id]
        task.status = LabelingStatus.DISPUTED
        task.notes = reason
        return True

    def _update_labeler_stats(self, task: LabelingTask):
        labeler = task.assigned_to
        stats = self.labeler_stats[labeler]
        stats['completed'] += 1
        if task.assigned_at and task.completed_at:
            time_taken = (task.completed_at - task.assigned_at).total_seconds()
            n = stats['completed']
            stats['avg_time'] = (stats['avg_time'] * (n - 1) + time_taken) / n
        if task.assigned_label == task.predicted_label:
            agreement = 1
        else:
            agreement = 0
        n = stats['completed']
        stats['agreement_rate'] = (stats['agreement_rate'] * (n - 1) + agreement) / n

    def get_task(self, task_id: int) -> Optional[Dict]:
        if task_id not in self.tasks:
            return None
        return self._task_to_dict(self.tasks[task_id])

    def _task_to_dict(self, task: LabelingTask) -> Dict:
        return {'task_id': task.task_id, 'sample_id': task.sample_id, 'device_id': task.device_id, 'features': task.features, 'predicted_label': task.predicted_label, 'predicted_label_name': FAULT_LABELS.get(task.predicted_label, 'Unknown'), 'confidence': task.confidence, 'priority': task.priority.name, 'status': task.status.value, 'created_at': task.created_at.isoformat(), 'assigned_to': task.assigned_to, 'assigned_at': task.assigned_at.isoformat() if task.assigned_at else None, 'completed_at': task.completed_at.isoformat() if task.completed_at else None, 'assigned_label': task.assigned_label, 'assigned_label_name': FAULT_LABELS.get(task.assigned_label) if task.assigned_label is not None else None, 'labeler_confidence': task.labeler_confidence, 'notes': task.notes, 'available_labels': FAULT_LABELS}

    def get_stats(self) -> Dict:
        status_counts = defaultdict(int)
        total_time = 0
        time_count = 0
        agreement_count = 0
        labeled_count = 0
        for task in self.tasks.values():
            status_counts[task.status.value] += 1
            if task.status == LabelingStatus.LABELED:
                labeled_count += 1
                if task.assigned_at and task.completed_at:
                    total_time += (task.completed_at - task.assigned_at).total_seconds()
                    time_count += 1
                if task.assigned_label == task.predicted_label:
                    agreement_count += 1
        return {'total_tasks': len(self.tasks), 'pending': status_counts['pending'], 'in_progress': status_counts['in_progress'], 'completed': status_counts['labeled'], 'skipped': status_counts['skipped'], 'disputed': status_counts['disputed'], 'agreement_with_model': agreement_count / labeled_count if labeled_count > 0 else 0, 'avg_labeling_time_seconds': total_time / time_count if time_count > 0 else 0}

    def get_labeler_stats(self, labeler_id: str=None) -> Dict:
        if labeler_id:
            return dict(self.labeler_stats.get(labeler_id, {}))
        return {k: dict(v) for k, v in self.labeler_stats.items()}

    def export_labeled_data(self, format: str='json', include_skipped: bool=False) -> str:
        tasks = [t for t in self.tasks.values() if t.status == LabelingStatus.LABELED or (include_skipped and t.status == LabelingStatus.SKIPPED)]
        if format == 'json':
            data = [self._task_to_dict(t) for t in tasks]
            return json.dumps(data, indent=2)
        elif format == 'csv':
            lines = ['sample_id,device_id,predicted_label,assigned_label,confidence,labeler_confidence']
            for task in tasks:
                lines.append(f'{task.sample_id},{task.device_id},{task.predicted_label},{task.assigned_label},{task.confidence},{task.labeler_confidence}')
            return '\n'.join(lines)
        return ''

    def get_label_distribution(self) -> Dict[str, int]:
        distribution = defaultdict(int)
        for task in self.tasks.values():
            if task.status == LabelingStatus.LABELED and task.assigned_label is not None:
                label_name = FAULT_LABELS.get(task.assigned_label, f'Unknown ({task.assigned_label})')
                distribution[label_name] += 1
        return dict(distribution)