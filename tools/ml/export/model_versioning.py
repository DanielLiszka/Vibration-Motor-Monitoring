import os
import json
import hashlib
import shutil
from datetime import datetime
from typing import Dict, Any, List, Optional
from pathlib import Path

def compute_file_hash(filepath: str, algorithm: str='sha256') -> str:
    h = hashlib.new(algorithm)
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()

class ModelVersion:

    def __init__(self, version: str, model_path: str, metrics: Dict[str, float]=None, metadata: Dict[str, Any]=None):
        self.version = version
        self.model_path = model_path
        self.metrics = metrics or {}
        self.metadata = metadata or {}
        self.created_at = datetime.now().isoformat()
        self.hash = None
        if os.path.exists(model_path):
            self.hash = compute_file_hash(model_path)
            self.size_bytes = os.path.getsize(model_path)

    def to_dict(self) -> Dict:
        return {'version': self.version, 'model_path': self.model_path, 'metrics': self.metrics, 'metadata': self.metadata, 'created_at': self.created_at, 'hash': self.hash, 'size_bytes': getattr(self, 'size_bytes', None)}

    @classmethod
    def from_dict(cls, data: Dict) -> 'ModelVersion':
        mv = cls(version=data['version'], model_path=data['model_path'], metrics=data.get('metrics', {}), metadata=data.get('metadata', {}))
        mv.created_at = data.get('created_at')
        mv.hash = data.get('hash')
        mv.size_bytes = data.get('size_bytes')
        return mv

class ModelRegistry:

    def __init__(self, registry_dir: str='./model_registry'):
        self.registry_dir = Path(registry_dir)
        self.registry_dir.mkdir(parents=True, exist_ok=True)
        self.registry_file = self.registry_dir / 'registry.json'
        self.models_dir = self.registry_dir / 'models'
        self.models_dir.mkdir(exist_ok=True)
        self.registry = self._load_registry()

    def _load_registry(self) -> Dict:
        if self.registry_file.exists():
            with open(self.registry_file) as f:
                return json.load(f)
        return {'models': {}, 'current_production': None, 'history': []}

    def _save_registry(self):
        with open(self.registry_file, 'w') as f:
            json.dump(self.registry, f, indent=2)

    def register_model(self, model_name: str, model_path: str, version: str=None, metrics: Dict[str, float]=None, metadata: Dict[str, Any]=None, copy_to_registry: bool=True) -> ModelVersion:
        if model_name not in self.registry['models']:
            self.registry['models'][model_name] = {'versions': [], 'latest': None, 'production': None}
        if version is None:
            existing = len(self.registry['models'][model_name]['versions'])
            version = f'v{existing + 1}.0.0'
        if copy_to_registry:
            model_filename = f'{model_name}_{version}{Path(model_path).suffix}'
            dest_path = self.models_dir / model_filename
            shutil.copy2(model_path, dest_path)
            model_path = str(dest_path)
        model_version = ModelVersion(version=version, model_path=model_path, metrics=metrics, metadata=metadata)
        self.registry['models'][model_name]['versions'].append(model_version.to_dict())
        self.registry['models'][model_name]['latest'] = version
        self.registry['history'].append({'action': 'register', 'model_name': model_name, 'version': version, 'timestamp': datetime.now().isoformat()})
        self._save_registry()
        print(f'Registered {model_name} {version}')
        if metrics:
            for k, v in metrics.items():
                print(f'  {k}: {v:.4f}')
        return model_version

    def get_version(self, model_name: str, version: str='latest') -> Optional[ModelVersion]:
        if model_name not in self.registry['models']:
            return None
        model_data = self.registry['models'][model_name]
        if version == 'latest':
            version = model_data['latest']
        elif version == 'production':
            version = model_data['production']
            if version is None:
                return None
        for v in model_data['versions']:
            if v['version'] == version:
                return ModelVersion.from_dict(v)
        return None

    def promote_to_production(self, model_name: str, version: str):
        if model_name not in self.registry['models']:
            raise ValueError(f'Model {model_name} not found')
        found = False
        for v in self.registry['models'][model_name]['versions']:
            if v['version'] == version:
                found = True
                break
        if not found:
            raise ValueError(f'Version {version} not found')
        old_prod = self.registry['models'][model_name]['production']
        self.registry['models'][model_name]['production'] = version
        self.registry['history'].append({'action': 'promote', 'model_name': model_name, 'version': version, 'previous': old_prod, 'timestamp': datetime.now().isoformat()})
        self._save_registry()
        print(f'Promoted {model_name} {version} to production')

    def list_versions(self, model_name: str) -> List[Dict]:
        if model_name not in self.registry['models']:
            return []
        return self.registry['models'][model_name]['versions']

    def list_models(self) -> List[str]:
        return list(self.registry['models'].keys())

    def compare_versions(self, model_name: str, version1: str, version2: str) -> Dict[str, Any]:
        v1 = self.get_version(model_name, version1)
        v2 = self.get_version(model_name, version2)
        if not v1 or not v2:
            raise ValueError('Version not found')
        comparison = {'version1': version1, 'version2': version2, 'metrics_diff': {}, 'size_diff': None}
        all_metrics = set(v1.metrics.keys()) | set(v2.metrics.keys())
        for metric in all_metrics:
            m1 = v1.metrics.get(metric, 0)
            m2 = v2.metrics.get(metric, 0)
            comparison['metrics_diff'][metric] = {'v1': m1, 'v2': m2, 'diff': m2 - m1, 'pct_change': (m2 - m1) / m1 * 100 if m1 != 0 else 0}
        if v1.size_bytes and v2.size_bytes:
            comparison['size_diff'] = {'v1': v1.size_bytes, 'v2': v2.size_bytes, 'diff': v2.size_bytes - v1.size_bytes}
        return comparison

    def get_deployment_history(self, model_name: str=None) -> List[Dict]:
        history = self.registry['history']
        if model_name:
            history = [h for h in history if h.get('model_name') == model_name]
        return history

    def delete_version(self, model_name: str, version: str, delete_file: bool=True):
        if model_name not in self.registry['models']:
            return
        versions = self.registry['models'][model_name]['versions']
        for i, v in enumerate(versions):
            if v['version'] == version:
                if delete_file and os.path.exists(v['model_path']):
                    os.remove(v['model_path'])
                versions.pop(i)
                break
        if self.registry['models'][model_name]['latest'] == version:
            if versions:
                self.registry['models'][model_name]['latest'] = versions[-1]['version']
            else:
                self.registry['models'][model_name]['latest'] = None
        self._save_registry()

def generate_version_string(major: int=1, minor: int=0, patch: int=0, suffix: str=None) -> str:
    version = f'v{major}.{minor}.{patch}'
    if suffix:
        version += f'-{suffix}'
    return version

def increment_version(current: str, part: str='patch') -> str:
    version = current.lstrip('v')
    parts = version.split('.')
    major = int(parts[0]) if len(parts) > 0 else 1
    minor = int(parts[1]) if len(parts) > 1 else 0
    patch = int(parts[2].split('-')[0]) if len(parts) > 2 else 0
    if part == 'major':
        major += 1
        minor = 0
        patch = 0
    elif part == 'minor':
        minor += 1
        patch = 0
    else:
        patch += 1
    return f'v{major}.{minor}.{patch}'