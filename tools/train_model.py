import argparse
import csv
import json
import os
import sqlite3
from datetime import datetime

import numpy as np

INPUT_FEATURES = 10
OUTPUT_CLASSES = 5
HIDDEN_NEURONS = 32
CLASS_NAMES = ['Normal', 'Imbalance', 'Misalignment', 'Bearing Fault', 'Looseness']


def _import_tensorflow():
    import tensorflow as tf
    from tensorflow import keras

    return tf, keras


def generate_synthetic_data(num_samples=1000, noise_level=0.1):
    X = []
    y = []
    for _ in range(num_samples):
        label = np.random.randint(0, OUTPUT_CLASSES)
        X.append(generate_class_features(label, noise_level))
        y.append(label)
    return np.array(X, dtype=np.float32), np.array(y, dtype=np.int32)


def generate_class_features(label, noise_level):
    features = np.zeros(INPUT_FEATURES)
    presets = {
        0: [0.2, 0.4, 3.0, 0.0, 3.0, 0.04, 0.5, 0.3, 0.5, 0.3],
        1: [0.8, 1.5, 3.5, 0.5, 3.5, 0.1, 0.3, 0.2, 0.3, 0.25],
        2: [0.6, 1.2, 4.0, -0.3, 4.0, 0.08, 0.4, 0.35, 1.5, 0.5],
        3: [1.0, 2.0, 6.0, 0.8, 5.0, 0.2, 2.0, 1.5, 2.5, 5.0],
        4: [0.7, 1.8, 5.0, 0.2, 4.5, 0.15, 1.5, 1.0, 2.0, 1.0],
    }
    for index, value in enumerate(presets[label]):
        features[index] = value + np.random.randn() * noise_level
    return features


def _normalize_feature_vector(raw_features):
    if len(raw_features) < INPUT_FEATURES:
        raise ValueError(f'Expected at least {INPUT_FEATURES} features, got {len(raw_features)}')
    return [float(value) for value in raw_features[:INPUT_FEATURES]]


def load_dataset_from_sqlite(database_path):
    conn = sqlite3.connect(database_path)
    cursor = conn.cursor()
    cursor.execute(
        '''
        SELECT features, COALESCE(true_label, predicted_label)
        FROM samples
        WHERE true_label IS NOT NULL
        ORDER BY id
        '''
    )
    rows = cursor.fetchall()
    conn.close()

    if not rows:
        raise ValueError('No labeled samples found in SQLite database')

    X = []
    y = []
    for features_json, label in rows:
        X.append(_normalize_feature_vector(json.loads(features_json)))
        y.append(int(label))
    return np.array(X, dtype=np.float32), np.array(y, dtype=np.int32)


def load_dataset_from_csv(csv_path, label_column='label'):
    with open(csv_path, newline='') as file_obj:
        reader = csv.DictReader(file_obj)
        rows = list(reader)

    if not rows:
        raise ValueError('CSV file is empty')
    if label_column not in rows[0]:
        raise ValueError(f'Missing label column: {label_column}')

    def feature_columns(sample_row):
        preferred = [f'f{i}' for i in range(INPUT_FEATURES)]
        if all(column in sample_row for column in preferred):
            return preferred
        candidates = [
            key for key in sample_row.keys()
            if key != label_column and key not in {'timestamp', 'sample_id', 'device_id'}
        ]
        numeric_candidates = []
        for key in candidates:
            try:
                float(sample_row[key])
                numeric_candidates.append(key)
            except (TypeError, ValueError):
                continue
        if len(numeric_candidates) < INPUT_FEATURES:
            raise ValueError(f'Could not find {INPUT_FEATURES} numeric feature columns')
        return numeric_candidates[:INPUT_FEATURES]

    columns = feature_columns(rows[0])
    X = []
    y = []
    for row in rows:
        X.append(_normalize_feature_vector([row[column] for column in columns]))
        y.append(int(row[label_column]))
    return np.array(X, dtype=np.float32), np.array(y, dtype=np.int32)


def load_training_data(args):
    if args.sqlite_db:
        print(f'Loading labeled samples from SQLite: {args.sqlite_db}')
        return load_dataset_from_sqlite(args.sqlite_db), 'sqlite'
    if args.csv_file:
        print(f'Loading labeled samples from CSV: {args.csv_file}')
        return load_dataset_from_csv(args.csv_file, args.label_column), 'csv'
    print('Generating synthetic training data...')
    return generate_synthetic_data(args.samples), 'synthetic'


def create_model(hidden_units=HIDDEN_NEURONS, dropout_rate=0.2):
    _, keras = _import_tensorflow()
    model = keras.Sequential([
        keras.layers.Input(shape=(INPUT_FEATURES,)),
        keras.layers.Dense(hidden_units, activation='relu'),
        keras.layers.Dropout(dropout_rate),
        keras.layers.Dense(hidden_units // 2, activation='relu'),
        keras.layers.Dense(OUTPUT_CLASSES, activation='softmax'),
    ])
    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=0.001),
        loss='sparse_categorical_crossentropy',
        metrics=['accuracy'],
    )
    return model


def train_model(X_train, y_train, X_val, y_val, epochs=100, batch_size=32):
    _, keras = _import_tensorflow()
    model = create_model()
    callbacks = [keras.callbacks.EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True)]
    history = model.fit(
        X_train,
        y_train,
        validation_data=(X_val, y_val),
        epochs=epochs,
        batch_size=batch_size,
        callbacks=callbacks,
        verbose=1,
    )
    return model, history


def convert_to_tflite(model, output_path, quantize=True):
    tf, _ = _import_tensorflow()
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    if quantize:
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]
    tflite_model = converter.convert()
    with open(output_path, 'wb') as file_obj:
        file_obj.write(tflite_model)
    print(f'Model saved to {output_path}')
    print(f'Model size: {len(tflite_model)} bytes')
    return tflite_model


def convert_to_c_array(tflite_model, output_path, var_name='model_data'):
    with open(output_path, 'w') as file_obj:
        file_obj.write('#ifndef MODEL_DATA_H\n')
        file_obj.write('#define MODEL_DATA_H\n\n')
        file_obj.write('#include <stdint.h>\n\n')
        file_obj.write(f'const unsigned int {var_name}_len = {len(tflite_model)};\n\n')
        file_obj.write(f'alignas(8) const uint8_t {var_name}[] = {{\n')
        for index, byte in enumerate(tflite_model):
            if index % 12 == 0:
                file_obj.write('    ')
            file_obj.write(f'0x{byte:02x}')
            if index < len(tflite_model) - 1:
                file_obj.write(', ')
            if (index + 1) % 12 == 0:
                file_obj.write('\n')
        file_obj.write('\n};\n\n')
        file_obj.write('#endif\n')
    print(f'C array saved to {output_path}')


def evaluate_model(model, X_test, y_test):
    predictions = model.predict(X_test, verbose=0)
    predicted_classes = np.argmax(predictions, axis=1)
    accuracy = np.mean(predicted_classes == y_test)
    print(f'\nTest Accuracy: {accuracy * 100:.2f}%')
    return accuracy


def split_dataset(X, y):
    indices = np.random.permutation(len(X))
    X = X[indices]
    y = y[indices]
    train_split = int(0.7 * len(X))
    val_split = int(0.85 * len(X))
    return (
        X[:train_split],
        y[:train_split],
        X[train_split:val_split],
        y[train_split:val_split],
        X[val_split:],
        y[val_split:],
    )


def main():
    parser = argparse.ArgumentParser(description='Train fault detection model')
    parser.add_argument('--samples', type=int, default=5000, help='Synthetic sample count')
    parser.add_argument('--epochs', type=int, default=100, help='Training epochs')
    parser.add_argument('--batch-size', type=int, default=32, help='Batch size')
    parser.add_argument('--output-dir', type=str, default='../data', help='Output directory')
    parser.add_argument('--sqlite-db', type=str, help='Train from labeled samples in a SQLite database')
    parser.add_argument('--csv-file', type=str, help='Train from a labeled CSV file')
    parser.add_argument('--label-column', type=str, default='label', help='CSV label column')
    parser.add_argument('--quantize', action='store_true', help='Quantize model')
    parser.add_argument('--generate-c-header', action='store_true', help='Generate C header file')
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    (X, y), source = load_training_data(args)
    if len(X) < 10:
        raise ValueError('Need at least 10 samples to train a model')

    X_train, y_train, X_val, y_val, X_test, y_test = split_dataset(X, y)

    print(f'Training source: {source}')
    print(f'Training samples: {len(X_train)}')
    print(f'Validation samples: {len(X_val)}')
    print(f'Test samples: {len(X_test)}')

    print('\nTraining model...')
    model, _ = train_model(X_train, y_train, X_val, y_val, args.epochs, args.batch_size)

    print('\nEvaluating model...')
    accuracy = evaluate_model(model, X_test, y_test)

    keras_path = os.path.join(args.output_dir, 'fault_classifier.keras')
    model.save(keras_path)
    print(f'\nKeras model saved to {keras_path}')

    tflite_path = os.path.join(args.output_dir, 'fault_classifier.tflite')
    tflite_model = convert_to_tflite(model, tflite_path, args.quantize)

    if args.generate_c_header:
        header_path = os.path.join(args.output_dir, 'model_data.h')
        convert_to_c_array(tflite_model, header_path)

    metadata = {
        'created': datetime.now().isoformat(),
        'source': source,
        'samples': int(len(X)),
        'epochs': args.epochs,
        'accuracy': float(accuracy),
        'input_features': INPUT_FEATURES,
        'output_classes': OUTPUT_CLASSES,
        'class_names': CLASS_NAMES,
        'model_size': len(tflite_model),
        'quantized': args.quantize,
    }
    metadata_path = os.path.join(args.output_dir, 'model_metadata.json')
    with open(metadata_path, 'w') as file_obj:
        json.dump(metadata, file_obj, indent=2)
    print(f'Metadata saved to {metadata_path}')
    print('\nTraining complete!')


if __name__ == '__main__':
    main()
