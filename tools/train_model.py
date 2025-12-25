import numpy as np
import tensorflow as tf
from tensorflow import keras
import argparse
import os
import json
from datetime import datetime
INPUT_FEATURES = 10
OUTPUT_CLASSES = 5
HIDDEN_NEURONS = 32
CLASS_NAMES = ['Normal', 'Imbalance', 'Misalignment', 'Bearing Fault', 'Looseness']

def generate_synthetic_data(num_samples=1000, noise_level=0.1):
    X = []
    y = []
    for _ in range(num_samples):
        label = np.random.randint(0, OUTPUT_CLASSES)
        features = generate_class_features(label, noise_level)
        X.append(features)
        y.append(label)
    return (np.array(X, dtype=np.float32), np.array(y, dtype=np.int32))

def generate_class_features(label, noise_level):
    features = np.zeros(INPUT_FEATURES)
    if label == 0:
        features[0] = 0.2 + np.random.randn() * noise_level
        features[1] = 0.4 + np.random.randn() * noise_level
        features[2] = 3.0 + np.random.randn() * noise_level * 2
        features[3] = 0.0 + np.random.randn() * noise_level
        features[4] = 3.0 + np.random.randn() * noise_level
        features[5] = 0.04 + np.random.randn() * noise_level * 0.01
        features[6] = 0.5 + np.random.randn() * noise_level
        features[7] = 0.3 + np.random.randn() * noise_level
        features[8] = 0.5 + np.random.randn() * noise_level
        features[9] = 0.3 + np.random.randn() * noise_level
    elif label == 1:
        features[0] = 0.8 + np.random.randn() * noise_level
        features[1] = 1.5 + np.random.randn() * noise_level
        features[2] = 3.5 + np.random.randn() * noise_level * 2
        features[3] = 0.5 + np.random.randn() * noise_level
        features[4] = 3.5 + np.random.randn() * noise_level
        features[5] = 0.1 + np.random.randn() * noise_level * 0.05
        features[6] = 0.3 + np.random.randn() * noise_level
        features[7] = 0.2 + np.random.randn() * noise_level
        features[8] = 0.3 + np.random.randn() * noise_level
        features[9] = 0.25 + np.random.randn() * noise_level
    elif label == 2:
        features[0] = 0.6 + np.random.randn() * noise_level
        features[1] = 1.2 + np.random.randn() * noise_level
        features[2] = 4.0 + np.random.randn() * noise_level * 2
        features[3] = -0.3 + np.random.randn() * noise_level
        features[4] = 4.0 + np.random.randn() * noise_level
        features[5] = 0.08 + np.random.randn() * noise_level * 0.02
        features[6] = 0.4 + np.random.randn() * noise_level
        features[7] = 0.35 + np.random.randn() * noise_level
        features[8] = 1.5 + np.random.randn() * noise_level
        features[9] = 0.5 + np.random.randn() * noise_level
    elif label == 3:
        features[0] = 1.0 + np.random.randn() * noise_level
        features[1] = 2.0 + np.random.randn() * noise_level
        features[2] = 6.0 + np.random.randn() * noise_level * 2
        features[3] = 0.8 + np.random.randn() * noise_level
        features[4] = 5.0 + np.random.randn() * noise_level
        features[5] = 0.2 + np.random.randn() * noise_level * 0.1
        features[6] = 2.0 + np.random.randn() * noise_level
        features[7] = 1.5 + np.random.randn() * noise_level
        features[8] = 2.5 + np.random.randn() * noise_level
        features[9] = 5.0 + np.random.randn() * noise_level
    elif label == 4:
        features[0] = 0.7 + np.random.randn() * noise_level
        features[1] = 1.8 + np.random.randn() * noise_level
        features[2] = 5.0 + np.random.randn() * noise_level * 2
        features[3] = 0.2 + np.random.randn() * noise_level
        features[4] = 4.5 + np.random.randn() * noise_level
        features[5] = 0.15 + np.random.randn() * noise_level * 0.05
        features[6] = 1.5 + np.random.randn() * noise_level
        features[7] = 1.0 + np.random.randn() * noise_level
        features[8] = 2.0 + np.random.randn() * noise_level
        features[9] = 1.0 + np.random.randn() * noise_level
    return features

def create_model(hidden_units=HIDDEN_NEURONS, dropout_rate=0.2):
    model = keras.Sequential([keras.layers.Input(shape=(INPUT_FEATURES,)), keras.layers.Dense(hidden_units, activation='relu'), keras.layers.Dropout(dropout_rate), keras.layers.Dense(hidden_units // 2, activation='relu'), keras.layers.Dense(OUTPUT_CLASSES, activation='softmax')])
    model.compile(optimizer=keras.optimizers.Adam(learning_rate=0.001), loss='sparse_categorical_crossentropy', metrics=['accuracy'])
    return model

def train_model(X_train, y_train, X_val, y_val, epochs=100, batch_size=32):
    model = create_model()
    early_stop = keras.callbacks.EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True)
    history = model.fit(X_train, y_train, validation_data=(X_val, y_val), epochs=epochs, batch_size=batch_size, callbacks=[early_stop], verbose=1)
    return (model, history)

def convert_to_tflite(model, output_path, quantize=True):
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    if quantize:
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]
    tflite_model = converter.convert()
    with open(output_path, 'wb') as f:
        f.write(tflite_model)
    print(f'Model saved to {output_path}')
    print(f'Model size: {len(tflite_model)} bytes')
    return tflite_model

def convert_to_c_array(tflite_model, output_path, var_name='model_data'):
    with open(output_path, 'w') as f:
        f.write('#ifndef MODEL_DATA_H\n')
        f.write('#define MODEL_DATA_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'const unsigned int {var_name}_len = {len(tflite_model)};\n\n')
        f.write(f'alignas(8) const uint8_t {var_name}[] = {{\n')
        for i, byte in enumerate(tflite_model):
            if i % 12 == 0:
                f.write('    ')
            f.write(f'0x{byte:02x}')
            if i < len(tflite_model) - 1:
                f.write(', ')
            if (i + 1) % 12 == 0:
                f.write('\n')
        f.write('\n};\n\n')
        f.write('#endif\n')
    print(f'C array saved to {output_path}')

def evaluate_model(model, X_test, y_test):
    predictions = model.predict(X_test)
    predicted_classes = np.argmax(predictions, axis=1)
    accuracy = np.mean(predicted_classes == y_test)
    print(f'\nTest Accuracy: {accuracy * 100:.2f}%')
    print('\nConfusion Matrix:')
    confusion = np.zeros((OUTPUT_CLASSES, OUTPUT_CLASSES), dtype=np.int32)
    for true, pred in zip(y_test, predicted_classes):
        confusion[true][pred] += 1
    print('         ', end='')
    for name in CLASS_NAMES:
        print(f'{name[:8]:>10}', end='')
    print()
    for i, name in enumerate(CLASS_NAMES):
        print(f'{name[:8]:>8} ', end='')
        for j in range(OUTPUT_CLASSES):
            print(f'{confusion[i][j]:>10}', end='')
        print()
    print('\nPer-class Accuracy:')
    for i, name in enumerate(CLASS_NAMES):
        class_total = np.sum(y_test == i)
        class_correct = confusion[i][i]
        class_acc = class_correct / class_total if class_total > 0 else 0
        print(f'  {name}: {class_acc * 100:.2f}%')
    return accuracy

def main():
    parser = argparse.ArgumentParser(description='Train fault detection model')
    parser.add_argument('--samples', type=int, default=5000, help='Number of training samples')
    parser.add_argument('--epochs', type=int, default=100, help='Training epochs')
    parser.add_argument('--batch-size', type=int, default=32, help='Batch size')
    parser.add_argument('--output-dir', type=str, default='../data', help='Output directory')
    parser.add_argument('--quantize', action='store_true', help='Quantize model')
    parser.add_argument('--generate-c-header', action='store_true', help='Generate C header file')
    args = parser.parse_args()
    os.makedirs(args.output_dir, exist_ok=True)
    print('Generating synthetic training data...')
    X, y = generate_synthetic_data(args.samples)
    indices = np.random.permutation(len(X))
    X, y = (X[indices], y[indices])
    train_split = int(0.7 * len(X))
    val_split = int(0.85 * len(X))
    X_train, y_train = (X[:train_split], y[:train_split])
    X_val, y_val = (X[train_split:val_split], y[train_split:val_split])
    X_test, y_test = (X[val_split:], y[val_split:])
    print(f'Training samples: {len(X_train)}')
    print(f'Validation samples: {len(X_val)}')
    print(f'Test samples: {len(X_test)}')
    print('\nTraining model...')
    model, history = train_model(X_train, y_train, X_val, y_val, args.epochs, args.batch_size)
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
    metadata = {'created': datetime.now().isoformat(), 'samples': args.samples, 'epochs': args.epochs, 'accuracy': float(accuracy), 'input_features': INPUT_FEATURES, 'output_classes': OUTPUT_CLASSES, 'class_names': CLASS_NAMES, 'model_size': len(tflite_model), 'quantized': args.quantize}
    metadata_path = os.path.join(args.output_dir, 'model_metadata.json')
    with open(metadata_path, 'w') as f:
        json.dump(metadata, f, indent=2)
    print(f'Metadata saved to {metadata_path}')
    print('\nTraining complete!')
if __name__ == '__main__':
    main()