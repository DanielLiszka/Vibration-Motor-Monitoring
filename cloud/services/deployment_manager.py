import logging
from datetime import datetime
from typing import Dict, List, Optional, Any
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import json
import shutil
import hashlib
logger = logging.getLogger(__name__)

class DeploymentStatus(Enum):
    PENDING = 'pending'
    ROLLING_OUT = 'rolling_out'
    COMPLETED = 'completed'
    FAILED = 'failed'
    ROLLED_BACK = 'rolled_back'

class DeviceUpdateStatus(Enum):
    PENDING = 'pending'
    NOTIFIED = 'notified'
    DOWNLOADING = 'downloading'
    INSTALLING = 'installing'
    COMPLETED = 'completed'
    FAILED = 'failed'

@dataclass
class DeployedModel:
    version: str
    file_path: str
    size_bytes: int
    hash_sha256: str
    created_at: datetime
    deployed_at: Optional[datetime] = None
    accuracy: float = 0.0
    is_production: bool = False
    metadata: Dict = None

@dataclass
class DeviceDeployment:
    device_id: str
    target_version: str
    current_version: str
    status: DeviceUpdateStatus
    notified_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None
    error_message: str = ''

@dataclass
class DeploymentJob:
    deployment_id: str
    model_version: str
    target_devices: List[str]
    status: DeploymentStatus
    created_at: datetime
    completed_at: Optional[datetime] = None
    success_count: int = 0
    failure_count: int = 0

class DeploymentManager:

    def __init__(self, models_dir: str='./models', registry_file: str='./models/registry.json'):
        self.models_dir = Path(models_dir)
        self.models_dir.mkdir(parents=True, exist_ok=True)
        self.registry_file = Path(registry_file)
        self.registry: Dict[str, DeployedModel] = {}
        self.production_version: Optional[str] = None
        self.deployments: Dict[str, DeploymentJob] = {}
        self.device_status: Dict[str, DeviceDeployment] = {}
        self._load_registry()
        self.notify_device: Optional[callable] = None

    def _load_registry(self):
        if self.registry_file.exists():
            with open(self.registry_file) as f:
                data = json.load(f)
            for version, info in data.get('models', {}).items():
                self.registry[version] = DeployedModel(version=version, file_path=info['file_path'], size_bytes=info['size_bytes'], hash_sha256=info['hash_sha256'], created_at=datetime.fromisoformat(info['created_at']), deployed_at=datetime.fromisoformat(info['deployed_at']) if info.get('deployed_at') else None, accuracy=info.get('accuracy', 0.0), is_production=info.get('is_production', False), metadata=info.get('metadata', {}))
                if info.get('is_production'):
                    self.production_version = version

    def _save_registry(self):
        data = {'models': {}, 'production_version': self.production_version}
        for version, model in self.registry.items():
            data['models'][version] = {'file_path': model.file_path, 'size_bytes': model.size_bytes, 'hash_sha256': model.hash_sha256, 'created_at': model.created_at.isoformat(), 'deployed_at': model.deployed_at.isoformat() if model.deployed_at else None, 'accuracy': model.accuracy, 'is_production': model.is_production, 'metadata': model.metadata or {}}
        with open(self.registry_file, 'w') as f:
            json.dump(data, f, indent=2)

    def register_model(self, model_path: str, version: str, accuracy: float=0.0, metadata: Dict=None) -> DeployedModel:
        source_path = Path(model_path)
        if not source_path.exists():
            raise FileNotFoundError(f'Model not found: {model_path}')
        dest_path = self.models_dir / f'model_{version}.tflite'
        shutil.copy2(source_path, dest_path)
        with open(dest_path, 'rb') as f:
            file_hash = hashlib.sha256(f.read()).hexdigest()
        model = DeployedModel(version=version, file_path=str(dest_path), size_bytes=dest_path.stat().st_size, hash_sha256=file_hash, created_at=datetime.now(), accuracy=accuracy, metadata=metadata)
        self.registry[version] = model
        self._save_registry()
        logger.info(f'Registered model version {version}')
        return model

    def deploy_model(self, version: str, target_devices: List[str]=None) -> str:
        if version not in self.registry:
            raise ValueError(f'Unknown model version: {version}')
        deployment_id = f"deploy_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
        deployment = DeploymentJob(deployment_id=deployment_id, model_version=version, target_devices=target_devices or [], status=DeploymentStatus.PENDING, created_at=datetime.now())
        self.deployments[deployment_id] = deployment
        self._set_production(version)
        self._start_rollout(deployment)
        return deployment_id

    def _set_production(self, version: str):
        if self.production_version and self.production_version in self.registry:
            self.registry[self.production_version].is_production = False
        self.registry[version].is_production = True
        self.registry[version].deployed_at = datetime.now()
        self.production_version = version
        self._save_registry()

    def _start_rollout(self, deployment: DeploymentJob):
        deployment.status = DeploymentStatus.ROLLING_OUT
        model = self.registry[deployment.model_version]
        update_info = {'type': 'model_update', 'version': deployment.model_version, 'size': model.size_bytes, 'hash': model.hash_sha256, 'accuracy': model.accuracy, 'download_url': self._get_download_url(deployment.model_version)}
        if self.notify_device:
            devices = deployment.target_devices or self._get_all_devices()
            for device_id in devices:
                self._notify_device_update(device_id, update_info)
                self.device_status[device_id] = DeviceDeployment(device_id=device_id, target_version=deployment.model_version, current_version='', status=DeviceUpdateStatus.NOTIFIED, notified_at=datetime.now())
        logger.info(f'Started rollout of {deployment.model_version}')

    def _notify_device_update(self, device_id: str, update_info: Dict):
        if self.notify_device:
            try:
                self.notify_device(device_id, update_info)
            except Exception as e:
                logger.error(f'Failed to notify {device_id}: {e}')

    def _get_download_url(self, version: str) -> str:
        return f'/api/models/{version}/download'

    def _get_all_devices(self) -> List[str]:
        return list(self.device_status.keys())

    def report_device_status(self, device_id: str, status: str, current_version: str=None, error: str=None):
        if device_id not in self.device_status:
            return
        device = self.device_status[device_id]
        try:
            device.status = DeviceUpdateStatus(status)
        except ValueError:
            device.status = DeviceUpdateStatus.FAILED
        if current_version:
            device.current_version = current_version
        if error:
            device.error_message = error
        if device.status == DeviceUpdateStatus.COMPLETED:
            device.completed_at = datetime.now()
            self._check_deployment_completion()
        elif device.status == DeviceUpdateStatus.FAILED:
            self._check_deployment_completion()

    def _check_deployment_completion(self):
        for deployment in self.deployments.values():
            if deployment.status != DeploymentStatus.ROLLING_OUT:
                continue
            success = 0
            failure = 0
            pending = 0
            devices = deployment.target_devices or self._get_all_devices()
            for device_id in devices:
                if device_id in self.device_status:
                    status = self.device_status[device_id].status
                    if status == DeviceUpdateStatus.COMPLETED:
                        success += 1
                    elif status == DeviceUpdateStatus.FAILED:
                        failure += 1
                    else:
                        pending += 1
            deployment.success_count = success
            deployment.failure_count = failure
            if pending == 0:
                if failure == 0:
                    deployment.status = DeploymentStatus.COMPLETED
                else:
                    deployment.status = DeploymentStatus.COMPLETED
                deployment.completed_at = datetime.now()
                logger.info(f'Deployment {deployment.deployment_id} completed: {success} success, {failure} failed')

    def rollback(self, to_version: str=None) -> bool:
        if to_version is None:
            versions = sorted([(v, m.deployed_at) for v, m in self.registry.items() if m.deployed_at], key=lambda x: x[1], reverse=True)
            if len(versions) < 2:
                logger.error('No previous version to rollback to')
                return False
            to_version = versions[1][0]
        if to_version not in self.registry:
            logger.error(f'Unknown version: {to_version}')
            return False
        logger.info(f'Rolling back to version {to_version}')
        self.deploy_model(to_version)
        return True

    def get_model_info(self, version: str) -> Optional[Dict]:
        if version not in self.registry:
            return None
        model = self.registry[version]
        return {'version': model.version, 'file_path': model.file_path, 'size_bytes': model.size_bytes, 'hash': model.hash_sha256, 'created_at': model.created_at.isoformat(), 'deployed_at': model.deployed_at.isoformat() if model.deployed_at else None, 'accuracy': model.accuracy, 'is_production': model.is_production, 'metadata': model.metadata}

    def get_production_model(self) -> Optional[Dict]:
        if self.production_version:
            return self.get_model_info(self.production_version)
        return None

    def list_versions(self) -> List[Dict]:
        return [self.get_model_info(v) for v in sorted(self.registry.keys(), reverse=True)]

    def get_deployment_status(self, deployment_id: str) -> Optional[Dict]:
        if deployment_id not in self.deployments:
            return None
        d = self.deployments[deployment_id]
        return {'deployment_id': d.deployment_id, 'model_version': d.model_version, 'status': d.status.value, 'created_at': d.created_at.isoformat(), 'completed_at': d.completed_at.isoformat() if d.completed_at else None, 'success_count': d.success_count, 'failure_count': d.failure_count, 'target_devices': d.target_devices}

    def get_device_deployment_status(self, device_id: str) -> Optional[Dict]:
        if device_id not in self.device_status:
            return None
        d = self.device_status[device_id]
        return {'device_id': d.device_id, 'target_version': d.target_version, 'current_version': d.current_version, 'status': d.status.value, 'notified_at': d.notified_at.isoformat() if d.notified_at else None, 'completed_at': d.completed_at.isoformat() if d.completed_at else None, 'error_message': d.error_message}