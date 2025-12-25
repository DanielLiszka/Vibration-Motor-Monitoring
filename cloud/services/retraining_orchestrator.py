import logging
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import json
import threading
import time
logger = logging.getLogger(__name__)

class RetrainingStatus(Enum):
    IDLE = 'idle'
    SCHEDULED = 'scheduled'
    PREPARING_DATA = 'preparing_data'
    TRAINING = 'training'
    VALIDATING = 'validating'
    DEPLOYING = 'deploying'
    COMPLETED = 'completed'
    FAILED = 'failed'

@dataclass
class RetrainingJob:
    job_id: str
    status: RetrainingStatus
    triggered_by: str
    created_at: datetime
    started_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None
    num_samples: int = 0
    train_accuracy: float = 0.0
    val_accuracy: float = 0.0
    model_version: str = ''
    error_message: str = ''

@dataclass
class RetrainingConfig:
    min_samples_for_retraining: int = 1000
    min_new_samples: int = 200
    min_labeled_ratio: float = 0.5
    validation_split: float = 0.2
    min_accuracy_improvement: float = 0.01
    max_training_time_seconds: int = 3600
    epochs: int = 100
    batch_size: int = 32
    early_stopping_patience: int = 10

class RetrainingOrchestrator:

    def __init__(self, data_collector, deployment_manager, model_factory: Callable, config: RetrainingConfig=None, models_dir: str='./models'):
        self.data_collector = data_collector
        self.deployment_manager = deployment_manager
        self.model_factory = model_factory
        self.config = config or RetrainingConfig()
        self.models_dir = Path(models_dir)
        self.models_dir.mkdir(parents=True, exist_ok=True)
        self.current_job: Optional[RetrainingJob] = None
        self.job_history: List[RetrainingJob] = []
        self.job_lock = threading.Lock()
        self.running = False
        self.worker_thread = None
        self.production_accuracy = 0.0
        self.on_job_started: Optional[Callable] = None
        self.on_job_completed: Optional[Callable] = None
        self.on_job_failed: Optional[Callable] = None

    def start(self, check_interval_seconds: int=3600):
        self.running = True
        self.check_interval = check_interval_seconds
        self.worker_thread = threading.Thread(target=self._worker_loop, daemon=True)
        self.worker_thread.start()
        logger.info('Retraining orchestrator started')

    def stop(self):
        self.running = False
        if self.worker_thread:
            self.worker_thread.join(timeout=10)
        logger.info('Retraining orchestrator stopped')

    def _worker_loop(self):
        while self.running:
            try:
                if self.should_retrain():
                    self.trigger_retraining('schedule')
            except Exception as e:
                logger.error(f'Error in orchestrator loop: {e}')
            time.sleep(self.check_interval)

    def should_retrain(self) -> bool:
        if self.current_job and self.current_job.status not in [RetrainingStatus.COMPLETED, RetrainingStatus.FAILED]:
            return False
        stats = self.data_collector.get_stats_summary()
        if stats['total_samples'] < self.config.min_samples_for_retraining:
            return False
        if stats['labeling_rate'] < self.config.min_labeled_ratio:
            return False
        new_samples = stats['total_samples'] - stats['used_for_training']
        if new_samples < self.config.min_new_samples:
            return False
        return True

    def trigger_retraining(self, trigger: str='manual') -> str:
        with self.job_lock:
            job_id = f"job_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
            self.current_job = RetrainingJob(job_id=job_id, status=RetrainingStatus.SCHEDULED, triggered_by=trigger, created_at=datetime.now())
            logger.info(f'Retraining job {job_id} triggered by {trigger}')
        threading.Thread(target=self._run_retraining_job, daemon=True).start()
        return job_id

    def _run_retraining_job(self):
        job = self.current_job
        if not job:
            return
        try:
            self._update_job_status(RetrainingStatus.PREPARING_DATA)
            job.started_at = datetime.now()
            if self.on_job_started:
                self.on_job_started(job)
            logger.info('Preparing training data...')
            train_data, val_data = self._prepare_data()
            if len(train_data) == 0:
                raise ValueError('No training data available')
            job.num_samples = len(train_data)
            self._update_job_status(RetrainingStatus.TRAINING)
            logger.info(f'Training model with {len(train_data)} samples...')
            model, train_acc, val_acc = self._train_model(train_data, val_data)
            job.train_accuracy = train_acc
            job.val_accuracy = val_acc
            self._update_job_status(RetrainingStatus.VALIDATING)
            logger.info('Validating model...')
            if not self._validate_model(model, val_acc):
                raise ValueError(f'Model validation failed: accuracy {val_acc:.4f} not better than production {self.production_accuracy:.4f}')
            self._update_job_status(RetrainingStatus.DEPLOYING)
            version = self._save_model(model)
            job.model_version = version
            if self.deployment_manager:
                self.deployment_manager.deploy_model(version)
            sample_ids = [s['sample_id'] for s in train_data + val_data]
            self.data_collector.mark_used_for_training(sample_ids)
            self._update_job_status(RetrainingStatus.COMPLETED)
            job.completed_at = datetime.now()
            self.production_accuracy = val_acc
            logger.info(f'Retraining completed: version {version}, accuracy {val_acc:.4f}')
            if self.on_job_completed:
                self.on_job_completed(job)
        except Exception as e:
            logger.error(f'Retraining failed: {e}')
            job.error_message = str(e)
            self._update_job_status(RetrainingStatus.FAILED)
            job.completed_at = datetime.now()
            if self.on_job_failed:
                self.on_job_failed(job, e)
        finally:
            self.job_history.append(job)

    def _update_job_status(self, status: RetrainingStatus):
        if self.current_job:
            self.current_job.status = status
            logger.info(f'Job status: {status.value}')

    def _prepare_data(self):
        import numpy as np
        samples = self.data_collector.get_training_dataset(min_confidence=0.5, labeled_only=True)
        np.random.shuffle(samples)
        split_idx = int(len(samples) * (1 - self.config.validation_split))
        train_data = samples[:split_idx]
        val_data = samples[split_idx:]
        return (train_data, val_data)

    def _train_model(self, train_data, val_data):
        import numpy as np
        X_train = np.array([s['features'] for s in train_data], dtype=np.float32)
        y_train = np.array([s['true_label'] for s in train_data], dtype=np.int32)
        X_val = np.array([s['features'] for s in val_data], dtype=np.float32)
        y_val = np.array([s['true_label'] for s in val_data], dtype=np.int32)
        model = self.model_factory(input_dim=X_train.shape[1])
        history = model.fit(X_train, y_train, X_val=X_val, y_val=y_val, epochs=self.config.epochs, batch_size=self.config.batch_size, early_stopping_patience=self.config.early_stopping_patience, verbose=1)
        train_metrics = model.evaluate(X_train, y_train)
        val_metrics = model.evaluate(X_val, y_val)
        return (model, train_metrics['accuracy'], val_metrics['accuracy'])

    def _validate_model(self, model, val_accuracy: float) -> bool:
        if val_accuracy < self.production_accuracy + self.config.min_accuracy_improvement:
            return False
        return True

    def _save_model(self, model) -> str:
        version = f"v{datetime.now().strftime('%Y%m%d_%H%M%S')}"
        model_path = self.models_dir / f'model_{version}'
        model.save(str(model_path))
        metadata = {'version': version, 'created_at': datetime.now().isoformat(), 'accuracy': self.current_job.val_accuracy if self.current_job else 0, 'num_samples': self.current_job.num_samples if self.current_job else 0}
        metadata_path = self.models_dir / f'model_{version}_metadata.json'
        with open(metadata_path, 'w') as f:
            json.dump(metadata, f, indent=2)
        return version

    def get_job_status(self, job_id: str=None) -> Optional[Dict]:
        if job_id:
            for job in [self.current_job] + self.job_history:
                if job and job.job_id == job_id:
                    return self._job_to_dict(job)
            return None
        elif self.current_job:
            return self._job_to_dict(self.current_job)
        return None

    def _job_to_dict(self, job: RetrainingJob) -> Dict:
        return {'job_id': job.job_id, 'status': job.status.value, 'triggered_by': job.triggered_by, 'created_at': job.created_at.isoformat(), 'started_at': job.started_at.isoformat() if job.started_at else None, 'completed_at': job.completed_at.isoformat() if job.completed_at else None, 'num_samples': job.num_samples, 'train_accuracy': job.train_accuracy, 'val_accuracy': job.val_accuracy, 'model_version': job.model_version, 'error_message': job.error_message}

    def get_job_history(self, limit: int=10) -> List[Dict]:
        return [self._job_to_dict(job) for job in reversed(self.job_history[-limit:])]