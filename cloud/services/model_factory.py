from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


def create_default_classifier(input_dim: int, num_classes: int=5):
    module_path = Path(__file__).resolve().parents[2] / 'tools' / 'ml' / 'models' / 'mlp.py'
    spec = spec_from_file_location('embedded_project_cloud_mlp', module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f'Unable to load classifier from {module_path}')
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.MLPClassifier(input_dim=input_dim, num_classes=num_classes, model_size='medium')
