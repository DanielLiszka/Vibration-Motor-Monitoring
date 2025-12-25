import numpy as np
from typing import Optional, Callable, Dict, Any, Tuple
import warnings

def apply_post_training_quantization(model, representative_dataset: Callable=None, quantization_type: str='int8', output_path: str=None) -> bytes:
    import tensorflow as tf
    if isinstance(model, str):
        converter = tf.lite.TFLiteConverter.from_saved_model(model)
    else:
        converter = tf.lite.TFLiteConverter.from_keras_model(model)
    if quantization_type == 'int8':
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        if representative_dataset is not None:
            converter.representative_dataset = representative_dataset
            converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
            converter.inference_input_type = tf.int8
            converter.inference_output_type = tf.int8
    elif quantization_type == 'float16':
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]
    elif quantization_type == 'dynamic':
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()
    if output_path:
        with open(output_path, 'wb') as f:
            f.write(tflite_model)
    return tflite_model

def create_representative_dataset(X: np.ndarray, n_samples: int=100, batch_size: int=1) -> Callable:
    indices = np.random.choice(len(X), min(n_samples, len(X)), replace=False)
    calibration_data = X[indices].astype(np.float32)

    def representative_data_gen():
        for i in range(len(calibration_data)):
            sample = calibration_data[i:i + 1]
            if len(sample.shape) == 2:
                sample = sample[..., np.newaxis]
            yield [sample]
    return representative_data_gen

class QuantizationAwareTraining:

    def __init__(self, model):
        self.original_model = model
        self.qat_model = None

    def prepare_model(self) -> Any:
        try:
            import tensorflow_model_optimization as tfmot
        except ImportError:
            warnings.warn('tensorflow-model-optimization not installed. Install with: pip install tensorflow-model-optimization')
            return self.original_model
        quantize_model = tfmot.quantization.keras.quantize_model
        self.qat_model = quantize_model(self.original_model)
        return self.qat_model

    def train(self, X_train: np.ndarray, y_train: np.ndarray, X_val: np.ndarray=None, y_val: np.ndarray=None, epochs: int=10, batch_size: int=32, learning_rate: float=0.0001):
        import tensorflow as tf
        if self.qat_model is None:
            self.prepare_model()
        self.qat_model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=learning_rate), loss='sparse_categorical_crossentropy', metrics=['accuracy'])
        callbacks = [tf.keras.callbacks.EarlyStopping(patience=5, restore_best_weights=True)]
        validation_data = (X_val, y_val) if X_val is not None else None
        self.qat_model.fit(X_train, y_train, validation_data=validation_data, epochs=epochs, batch_size=batch_size, callbacks=callbacks)

    def convert_to_tflite(self, representative_dataset: Callable=None, output_path: str=None) -> bytes:
        import tensorflow as tf
        try:
            import tensorflow_model_optimization as tfmot
        except ImportError:
            return apply_post_training_quantization(self.qat_model or self.original_model, representative_dataset, 'int8', output_path)
        model_for_export = tfmot.quantization.keras.strip_quantize(self.qat_model)
        converter = tf.lite.TFLiteConverter.from_keras_model(model_for_export)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        if representative_dataset:
            converter.representative_dataset = representative_dataset
            converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
            converter.inference_input_type = tf.int8
            converter.inference_output_type = tf.int8
        tflite_model = converter.convert()
        if output_path:
            with open(output_path, 'wb') as f:
                f.write(tflite_model)
        return tflite_model

def evaluate_quantized_model(tflite_model: bytes, X_test: np.ndarray, y_test: np.ndarray) -> Dict[str, float]:
    import tensorflow as tf
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    input_scale = input_details[0].get('quantization', (1.0, 0))[0]
    input_zero = input_details[0].get('quantization', (1.0, 0))[1]
    input_dtype = input_details[0]['dtype']
    predictions = []
    for i in range(len(X_test)):
        sample = X_test[i:i + 1].astype(np.float32)
        expected_shape = input_details[0]['shape']
        if len(sample.shape) < len(expected_shape):
            sample = sample[..., np.newaxis]
        if input_dtype == np.int8:
            sample = (sample / input_scale + input_zero).astype(np.int8)
        elif input_dtype == np.uint8:
            sample = (sample / input_scale + input_zero).astype(np.uint8)
        interpreter.set_tensor(input_details[0]['index'], sample)
        interpreter.invoke()
        output = interpreter.get_tensor(output_details[0]['index'])
        predictions.append(np.argmax(output))
    predictions = np.array(predictions)
    accuracy = np.mean(predictions == y_test)
    return {'accuracy': float(accuracy), 'n_samples': len(y_test)}

def compare_model_sizes(keras_model, tflite_float: bytes=None, tflite_int8: bytes=None) -> Dict[str, int]:
    import tempfile
    import os
    sizes = {}
    with tempfile.NamedTemporaryFile(suffix='.h5', delete=False) as f:
        keras_model.save(f.name)
        sizes['keras_h5'] = os.path.getsize(f.name)
        os.unlink(f.name)
    if tflite_float:
        sizes['tflite_float'] = len(tflite_float)
    if tflite_int8:
        sizes['tflite_int8'] = len(tflite_int8)
    print('\nModel Size Comparison:')
    print('-' * 40)
    for name, size in sizes.items():
        print(f'{name:15s}: {size:10d} bytes ({size / 1024:.1f} KB)')
    if 'tflite_float' in sizes and 'tflite_int8' in sizes:
        reduction = (1 - sizes['tflite_int8'] / sizes['tflite_float']) * 100
        print(f'\nINT8 reduction: {reduction:.1f}%')
    return sizes

def quantization_error_analysis(keras_model, tflite_model: bytes, X_test: np.ndarray, n_samples: int=100) -> Dict[str, float]:
    import tensorflow as tf
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    indices = np.random.choice(len(X_test), min(n_samples, len(X_test)), replace=False)
    errors = []
    class_mismatches = 0
    for idx in indices:
        sample = X_test[idx:idx + 1].astype(np.float32)
        keras_pred = keras_model.predict(sample, verbose=0)
        if len(sample.shape) < len(input_details[0]['shape']):
            tflite_input = sample[..., np.newaxis]
        else:
            tflite_input = sample
        input_dtype = input_details[0]['dtype']
        if input_dtype in [np.int8, np.uint8]:
            scale, zero = input_details[0].get('quantization', (1.0, 0))
            tflite_input = (tflite_input / scale + zero).astype(input_dtype)
        interpreter.set_tensor(input_details[0]['index'], tflite_input)
        interpreter.invoke()
        tflite_pred = interpreter.get_tensor(output_details[0]['index'])
        output_dtype = output_details[0]['dtype']
        if output_dtype in [np.int8, np.uint8]:
            scale, zero = output_details[0].get('quantization', (1.0, 0))
            tflite_pred = (tflite_pred.astype(np.float32) - zero) * scale
        error = np.abs(keras_pred - tflite_pred).mean()
        errors.append(error)
        if np.argmax(keras_pred) != np.argmax(tflite_pred):
            class_mismatches += 1
    return {'mean_output_error': float(np.mean(errors)), 'max_output_error': float(np.max(errors)), 'std_output_error': float(np.std(errors)), 'class_mismatch_rate': class_mismatches / len(indices), 'n_samples': len(indices)}