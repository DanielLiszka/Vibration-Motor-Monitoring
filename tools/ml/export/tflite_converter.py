import numpy as np
from typing import Optional, Callable, Dict, Any, List
from datetime import datetime
import json
import os

def convert_to_tflite(model, output_path: str, quantization: str='int8', representative_data: np.ndarray=None, metadata: Dict[str, Any]=None) -> Dict[str, Any]:
    import tensorflow as tf
    if isinstance(model, str):
        converter = tf.lite.TFLiteConverter.from_saved_model(model)
    else:
        converter = tf.lite.TFLiteConverter.from_keras_model(model)
    if quantization == 'int8':
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        if representative_data is not None:

            def representative_dataset():
                for i in range(min(100, len(representative_data))):
                    sample = representative_data[i:i + 1].astype(np.float32)
                    if len(sample.shape) == 2:
                        sample = sample[..., np.newaxis]
                    yield [sample]
            converter.representative_dataset = representative_dataset
            converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
            converter.inference_input_type = tf.int8
            converter.inference_output_type = tf.int8
    elif quantization == 'float16':
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]
    elif quantization == 'dynamic':
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()
    with open(output_path, 'wb') as f:
        f.write(tflite_model)
    stats = {'output_path': output_path, 'size_bytes': len(tflite_model), 'size_kb': len(tflite_model) / 1024, 'quantization': quantization, 'timestamp': datetime.now().isoformat()}
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    stats['input_shape'] = list(input_details['shape'])
    stats['input_dtype'] = str(input_details['dtype'])
    stats['output_shape'] = list(output_details['shape'])
    stats['output_dtype'] = str(output_details['dtype'])
    if 'quantization' in input_details:
        stats['input_scale'] = float(input_details['quantization'][0])
        stats['input_zero_point'] = int(input_details['quantization'][1])
    if 'quantization' in output_details:
        stats['output_scale'] = float(output_details['quantization'][0])
        stats['output_zero_point'] = int(output_details['quantization'][1])
    return stats

def generate_c_header(tflite_path: str, header_path: str, model_name: str='fault_model', include_metadata: bool=True, metadata: Dict[str, Any]=None) -> str:
    import tensorflow as tf
    with open(tflite_path, 'rb') as f:
        model_data = f.read()
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    lines = []
    lines.append(f'// Auto-generated TFLite model header')
    lines.append(f'// Generated: {datetime.now().isoformat()}')
    lines.append(f'// Model size: {len(model_data)} bytes ({len(model_data) / 1024:.1f} KB)')
    lines.append(f'//')
    lines.append(f"// Input shape:  {list(input_details['shape'])}")
    lines.append(f"// Input dtype:  {input_details['dtype']}")
    lines.append(f"// Output shape: {list(output_details['shape'])}")
    lines.append(f"// Output dtype: {output_details['dtype']}")
    if 'quantization' in input_details:
        scale, zero = input_details['quantization']
        lines.append(f'// Input quantization: scale={scale}, zero_point={zero}')
    if metadata:
        lines.append(f'//')
        lines.append(f'// Metadata:')
        for key, value in metadata.items():
            lines.append(f'//   {key}: {value}')
    lines.append('')
    lines.append(f'#ifndef {model_name.upper()}_H')
    lines.append(f'#define {model_name.upper()}_H')
    lines.append('')
    lines.append('#include <stdint.h>')
    lines.append('')
    lines.append(f'const unsigned int {model_name}_len = {len(model_data)};')
    lines.append('')
    lines.append(f'alignas(8) const unsigned char {model_name}_data[] = {{')
    bytes_per_line = 16
    for i in range(0, len(model_data), bytes_per_line):
        chunk = model_data[i:i + bytes_per_line]
        hex_values = ', '.join((f'0x{b:02x}' for b in chunk))
        comma = ',' if i + bytes_per_line < len(model_data) else ''
        lines.append(f'    {hex_values}{comma}')
    lines.append('};')
    lines.append('')
    lines.append(f"#define {model_name.upper()}_INPUT_SIZE {np.prod(input_details['shape'])}")
    lines.append(f"#define {model_name.upper()}_OUTPUT_SIZE {np.prod(output_details['shape'])}")
    if 'quantization' in input_details:
        scale, zero = input_details['quantization']
        lines.append(f'#define {model_name.upper()}_INPUT_SCALE {scale}f')
        lines.append(f'#define {model_name.upper()}_INPUT_ZERO_POINT {zero}')
    if 'quantization' in output_details:
        scale, zero = output_details['quantization']
        lines.append(f'#define {model_name.upper()}_OUTPUT_SCALE {scale}f')
        lines.append(f'#define {model_name.upper()}_OUTPUT_ZERO_POINT {zero}')
    lines.append('')
    lines.append(f'#endif // {model_name.upper()}_H')
    lines.append('')
    header_content = '\n'.join(lines)
    with open(header_path, 'w') as f:
        f.write(header_content)
    return header_path

def analyze_tflite_model(tflite_path: str) -> Dict[str, Any]:
    import tensorflow as tf
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    tensor_details = interpreter.get_tensor_details()
    analysis = {'file_size': os.path.getsize(tflite_path), 'num_tensors': len(tensor_details), 'tensors': [], 'ops': [], 'quantized': False}
    total_params = 0
    for tensor in tensor_details:
        tensor_info = {'name': tensor['name'], 'shape': list(tensor['shape']), 'dtype': str(tensor['dtype'])}
        if tensor['dtype'] in [np.int8, np.uint8]:
            analysis['quantized'] = True
            if 'quantization' in tensor:
                tensor_info['scale'] = float(tensor['quantization'][0])
                tensor_info['zero_point'] = int(tensor['quantization'][1])
        if len(tensor['shape']) >= 2:
            params = int(np.prod(tensor['shape']))
            total_params += params
            tensor_info['params'] = params
        analysis['tensors'].append(tensor_info)
    analysis['total_params'] = total_params
    analysis['input_details'] = interpreter.get_input_details()
    analysis['output_details'] = interpreter.get_output_details()
    return analysis

def validate_tflite_model(tflite_path: str, test_input: np.ndarray, expected_output: np.ndarray=None, tolerance: float=0.01) -> Dict[str, Any]:
    import tensorflow as tf
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    input_data = test_input.astype(np.float32)
    expected_shape = input_details['shape']
    if len(input_data.shape) < len(expected_shape):
        input_data = input_data[np.newaxis, ...]
    if len(input_data.shape) < len(expected_shape):
        input_data = input_data[..., np.newaxis]
    if input_details['dtype'] == np.int8:
        scale, zero = input_details['quantization']
        input_data = (input_data / scale + zero).astype(np.int8)
    elif input_details['dtype'] == np.uint8:
        scale, zero = input_details['quantization']
        input_data = (input_data / scale + zero).astype(np.uint8)
    interpreter.set_tensor(input_details['index'], input_data)
    interpreter.invoke()
    output_data = interpreter.get_tensor(output_details['index'])
    if output_details['dtype'] in [np.int8, np.uint8]:
        scale, zero = output_details['quantization']
        output_data = (output_data.astype(np.float32) - zero) * scale
    result = {'input_shape': list(input_data.shape), 'output_shape': list(output_data.shape), 'output': output_data.tolist(), 'valid': True}
    if expected_output is not None:
        diff = np.abs(output_data - expected_output).max()
        result['max_diff'] = float(diff)
        result['valid'] = diff < tolerance
    return result

class TFLiteModelExporter:

    def __init__(self, model, model_name: str='fault_model', output_dir: str='./exported_models'):
        self.model = model
        self.model_name = model_name
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)

    def export(self, representative_data: np.ndarray=None, quantization: str='int8', generate_header: bool=True, metadata: Dict[str, Any]=None) -> Dict[str, str]:
        outputs = {}
        tflite_path = os.path.join(self.output_dir, f'{self.model_name}.tflite')
        header_path = os.path.join(self.output_dir, f'{self.model_name}.h')
        metadata_path = os.path.join(self.output_dir, f'{self.model_name}_metadata.json')
        stats = convert_to_tflite(self.model, tflite_path, quantization=quantization, representative_data=representative_data, metadata=metadata)
        outputs['tflite'] = tflite_path
        if generate_header:
            generate_c_header(tflite_path, header_path, self.model_name, metadata=metadata)
            outputs['header'] = header_path
        full_metadata = {'model_name': self.model_name, 'export_time': datetime.now().isoformat(), 'quantization': quantization, **stats}
        if metadata:
            full_metadata.update(metadata)
        with open(metadata_path, 'w') as f:
            json.dump(full_metadata, f, indent=2, default=str)
        outputs['metadata'] = metadata_path
        print(f'\nExported model:')
        print(f"  TFLite:   {tflite_path} ({stats['size_kb']:.1f} KB)")
        if generate_header:
            print(f'  Header:   {header_path}')
        print(f'  Metadata: {metadata_path}')
        return outputs