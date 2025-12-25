import numpy as np
from typing import Dict, List, Optional, Any
from datetime import datetime
import json

def compute_normalization_params(X: np.ndarray, feature_names: List[str]=None, method: str='zscore') -> Dict[str, Any]:
    n_features = X.shape[1] if len(X.shape) > 1 else 1
    params = {'method': method, 'n_features': n_features, 'feature_names': feature_names or [f'feature_{i}' for i in range(n_features)], 'computed_from_samples': len(X), 'timestamp': datetime.now().isoformat()}
    if method == 'zscore':
        params['mean'] = X.mean(axis=0).tolist()
        params['std'] = X.std(axis=0).tolist()
        params['std'] = [max(s, 1e-07) for s in params['std']]
    elif method == 'minmax':
        params['min'] = X.min(axis=0).tolist()
        params['max'] = X.max(axis=0).tolist()
        params['range'] = [ma - mi if ma - mi > 1e-07 else 1.0 for mi, ma in zip(params['min'], params['max'])]
    elif method == 'robust':
        params['median'] = np.median(X, axis=0).tolist()
        q25 = np.percentile(X, 25, axis=0)
        q75 = np.percentile(X, 75, axis=0)
        iqr = q75 - q25
        params['iqr'] = [max(i, 1e-07) for i in iqr.tolist()]
        params['q25'] = q25.tolist()
        params['q75'] = q75.tolist()
    return params

def export_normalization_json(params: Dict[str, Any], output_path: str) -> str:
    with open(output_path, 'w') as f:
        json.dump(params, f, indent=2)
    return output_path

def export_normalization_header(params: Dict[str, Any], output_path: str, prefix: str='NORM') -> str:
    lines = []
    lines.append(f'// Auto-generated normalization parameters')
    lines.append(f'// Generated: {datetime.now().isoformat()}')
    lines.append(f"// Method: {params['method']}")
    lines.append(f"// Features: {params['n_features']}")
    lines.append(f"// Computed from {params.get('computed_from_samples', 'N/A')} samples")
    lines.append('')
    lines.append(f'#ifndef {prefix}_PARAMS_H')
    lines.append(f'#define {prefix}_PARAMS_H')
    lines.append('')
    lines.append(f"#define {prefix}_NUM_FEATURES {params['n_features']}")
    lines.append(f'''#define {prefix}_METHOD "{params['method']}"''')
    lines.append('')
    if params['method'] == 'zscore':
        lines.append(f'const float {prefix.lower()}_mean[{prefix}_NUM_FEATURES] = {{')
        lines.append(f"    {', '.join((f'{v:.8f}f' for v in params['mean']))}")
        lines.append('};')
        lines.append('')
        lines.append(f'const float {prefix.lower()}_std[{prefix}_NUM_FEATURES] = {{')
        lines.append(f"    {', '.join((f'{v:.8f}f' for v in params['std']))}")
        lines.append('};')
        lines.append('')
        lines.append(f'inline float {prefix.lower()}_normalize(float value, int feature_idx) {{')
        lines.append(f'    return (value - {prefix.lower()}_mean[feature_idx]) / {prefix.lower()}_std[feature_idx];')
        lines.append('}')
        lines.append('')
        lines.append(f'inline float {prefix.lower()}_denormalize(float value, int feature_idx) {{')
        lines.append(f'    return value * {prefix.lower()}_std[feature_idx] + {prefix.lower()}_mean[feature_idx];')
        lines.append('}')
    elif params['method'] == 'minmax':
        lines.append(f'const float {prefix.lower()}_min[{prefix}_NUM_FEATURES] = {{')
        lines.append(f"    {', '.join((f'{v:.8f}f' for v in params['min']))}")
        lines.append('};')
        lines.append('')
        lines.append(f'const float {prefix.lower()}_range[{prefix}_NUM_FEATURES] = {{')
        lines.append(f"    {', '.join((f'{v:.8f}f' for v in params['range']))}")
        lines.append('};')
        lines.append('')
        lines.append(f'inline float {prefix.lower()}_normalize(float value, int feature_idx) {{')
        lines.append(f'    return (value - {prefix.lower()}_min[feature_idx]) / {prefix.lower()}_range[feature_idx];')
        lines.append('}')
        lines.append('')
        lines.append(f'inline float {prefix.lower()}_denormalize(float value, int feature_idx) {{')
        lines.append(f'    return value * {prefix.lower()}_range[feature_idx] + {prefix.lower()}_min[feature_idx];')
        lines.append('}')
    elif params['method'] == 'robust':
        lines.append(f'const float {prefix.lower()}_median[{prefix}_NUM_FEATURES] = {{')
        lines.append(f"    {', '.join((f'{v:.8f}f' for v in params['median']))}")
        lines.append('};')
        lines.append('')
        lines.append(f'const float {prefix.lower()}_iqr[{prefix}_NUM_FEATURES] = {{')
        lines.append(f"    {', '.join((f'{v:.8f}f' for v in params['iqr']))}")
        lines.append('};')
        lines.append('')
        lines.append(f'inline float {prefix.lower()}_normalize(float value, int feature_idx) {{')
        lines.append(f'    return (value - {prefix.lower()}_median[feature_idx]) / {prefix.lower()}_iqr[feature_idx];')
        lines.append('}')
    lines.append('')
    lines.append(f'inline void {prefix.lower()}_normalize_batch(const float* input, float* output, int n_features) {{')
    lines.append('    for (int i = 0; i < n_features; i++) {')
    lines.append(f'        output[i] = {prefix.lower()}_normalize(input[i], i);')
    lines.append('    }')
    lines.append('}')
    lines.append('')
    lines.append('// Feature names:')
    for i, name in enumerate(params.get('feature_names', [])):
        lines.append(f'//   [{i}] {name}')
    lines.append('')
    lines.append(f'#endif // {prefix}_PARAMS_H')
    lines.append('')
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))
    return output_path

def generate_feature_config(feature_names: List[str], normalization_params: Dict[str, Any], output_path: str) -> str:
    lines = []
    lines.append('// Feature Configuration for Motor Fault Detection')
    lines.append(f'// Generated: {datetime.now().isoformat()}')
    lines.append('')
    lines.append('#ifndef FEATURE_CONFIG_H')
    lines.append('#define FEATURE_CONFIG_H')
    lines.append('')
    lines.append(f'#define NUM_FEATURES {len(feature_names)}')
    lines.append('')
    lines.append('// Feature indices')
    for i, name in enumerate(feature_names):
        c_name = name.upper().replace(' ', '_').replace('-', '_')
        lines.append(f'#define FEAT_{c_name} {i}')
    lines.append('')
    lines.append('#ifdef FEATURE_DEBUG')
    lines.append('const char* feature_names[] = {')
    for name in feature_names:
        lines.append(f'    "{name}",')
    lines.append('};')
    lines.append('#endif')
    lines.append('')
    lines.append('#endif // FEATURE_CONFIG_H')
    lines.append('')
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))
    return output_path

class NormalizationExporter:

    def __init__(self, X_train: np.ndarray, feature_names: List[str]=None, method: str='zscore'):
        self.params = compute_normalization_params(X_train, feature_names, method)

    def export_all(self, output_dir: str, prefix: str='norm') -> Dict[str, str]:
        import os
        os.makedirs(output_dir, exist_ok=True)
        outputs = {}
        json_path = os.path.join(output_dir, f'{prefix}_params.json')
        export_normalization_json(self.params, json_path)
        outputs['json'] = json_path
        header_path = os.path.join(output_dir, f'{prefix}_params.h')
        export_normalization_header(self.params, header_path, prefix.upper())
        outputs['header'] = header_path
        config_path = os.path.join(output_dir, 'feature_config.h')
        generate_feature_config(self.params['feature_names'], self.params, config_path)
        outputs['feature_config'] = config_path
        print(f'\nExported normalization parameters:')
        for name, path in outputs.items():
            print(f'  {name}: {path}')
        return outputs

    def normalize(self, X: np.ndarray) -> np.ndarray:
        if self.params['method'] == 'zscore':
            mean = np.array(self.params['mean'])
            std = np.array(self.params['std'])
            return (X - mean) / std
        elif self.params['method'] == 'minmax':
            min_val = np.array(self.params['min'])
            range_val = np.array(self.params['range'])
            return (X - min_val) / range_val
        elif self.params['method'] == 'robust':
            median = np.array(self.params['median'])
            iqr = np.array(self.params['iqr'])
            return (X - median) / iqr
        return X

    def denormalize(self, X: np.ndarray) -> np.ndarray:
        if self.params['method'] == 'zscore':
            mean = np.array(self.params['mean'])
            std = np.array(self.params['std'])
            return X * std + mean
        elif self.params['method'] == 'minmax':
            min_val = np.array(self.params['min'])
            range_val = np.array(self.params['range'])
            return X * range_val + min_val
        elif self.params['method'] == 'robust':
            median = np.array(self.params['median'])
            iqr = np.array(self.params['iqr'])
            return X * iqr + median
        return X