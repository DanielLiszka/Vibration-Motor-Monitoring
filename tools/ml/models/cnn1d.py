import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers, regularizers
from typing import List, Optional, Tuple, Dict

def create_cnn1d_classifier(input_length: int, num_channels: int=1, num_classes: int=5, filters: List[int]=[32, 64, 64], kernel_sizes: List[int]=[7, 5, 3], pool_sizes: List[int]=[2, 2, 2], dense_units: List[int]=[64, 32], dropout_rate: float=0.3, l2_reg: float=0.001, use_batch_norm: bool=True, name: str='cnn1d_classifier') -> keras.Model:
    inputs = keras.Input(shape=(input_length, num_channels), name='input_signal')
    x = inputs
    for i, (filters_i, kernel, pool) in enumerate(zip(filters, kernel_sizes, pool_sizes)):
        x = layers.Conv1D(filters=filters_i, kernel_size=kernel, padding='same', kernel_regularizer=regularizers.l2(l2_reg), name=f'conv_{i}')(x)
        if use_batch_norm:
            x = layers.BatchNormalization(name=f'bn_conv_{i}')(x)
        x = layers.Activation('relu', name=f'act_conv_{i}')(x)
        x = layers.MaxPooling1D(pool_size=pool, name=f'pool_{i}')(x)
        x = layers.Dropout(dropout_rate / 2, name=f'dropout_conv_{i}')(x)
    x = layers.GlobalAveragePooling1D(name='global_pool')(x)
    for i, units in enumerate(dense_units):
        x = layers.Dense(units, kernel_regularizer=regularizers.l2(l2_reg), name=f'dense_{i}')(x)
        if use_batch_norm:
            x = layers.BatchNormalization(name=f'bn_dense_{i}')(x)
        x = layers.Activation('relu', name=f'act_dense_{i}')(x)
        x = layers.Dropout(dropout_rate, name=f'dropout_dense_{i}')(x)
    outputs = layers.Dense(num_classes, activation='softmax', name='output')(x)
    model = keras.Model(inputs=inputs, outputs=outputs, name=name)
    return model

def create_cnn1d_small(input_length: int, num_classes: int=5) -> keras.Model:
    return create_cnn1d_classifier(input_length=input_length, num_classes=num_classes, filters=[16, 32], kernel_sizes=[7, 5], pool_sizes=[4, 4], dense_units=[32], dropout_rate=0.2, l2_reg=0.01, use_batch_norm=False, name='cnn1d_small')

def create_cnn1d_medium(input_length: int, num_classes: int=5) -> keras.Model:
    return create_cnn1d_classifier(input_length=input_length, num_classes=num_classes, filters=[32, 64, 64], kernel_sizes=[7, 5, 3], pool_sizes=[2, 2, 2], dense_units=[64, 32], dropout_rate=0.3, use_batch_norm=True, name='cnn1d_medium')

def create_cnn1d_large(input_length: int, num_classes: int=5) -> keras.Model:
    return create_cnn1d_classifier(input_length=input_length, num_classes=num_classes, filters=[32, 64, 128, 128], kernel_sizes=[11, 7, 5, 3], pool_sizes=[2, 2, 2, 2], dense_units=[128, 64], dropout_rate=0.4, l2_reg=0.0001, use_batch_norm=True, name='cnn1d_large')

def create_cnn1d_separable(input_length: int, num_channels: int=1, num_classes: int=5, filters: List[int]=[32, 64, 64], kernel_sizes: List[int]=[7, 5, 3], name: str='cnn1d_separable') -> keras.Model:
    inputs = keras.Input(shape=(input_length, num_channels), name='input_signal')
    x = inputs
    x = layers.Conv1D(filters[0], kernel_sizes[0], padding='same')(x)
    x = layers.BatchNormalization()(x)
    x = layers.Activation('relu')(x)
    x = layers.MaxPooling1D(2)(x)
    for filters_i, kernel in zip(filters[1:], kernel_sizes[1:]):
        x = layers.SeparableConv1D(filters_i, kernel, padding='same', depthwise_regularizer=regularizers.l2(0.001), pointwise_regularizer=regularizers.l2(0.001))(x)
        x = layers.BatchNormalization()(x)
        x = layers.Activation('relu')(x)
        x = layers.MaxPooling1D(2)(x)
    x = layers.GlobalAveragePooling1D()(x)
    x = layers.Dense(32, activation='relu')(x)
    x = layers.Dropout(0.3)(x)
    outputs = layers.Dense(num_classes, activation='softmax', name='output')(x)
    return keras.Model(inputs=inputs, outputs=outputs, name=name)

class CNN1DClassifier:

    def __init__(self, input_length: int, num_classes: int=5, model_size: str='medium', learning_rate: float=0.001, use_separable: bool=False):
        self.input_length = input_length
        self.num_classes = num_classes
        self.learning_rate = learning_rate
        if use_separable:
            self.model = create_cnn1d_separable(input_length, 1, num_classes)
        elif model_size == 'small':
            self.model = create_cnn1d_small(input_length, num_classes)
        elif model_size == 'large':
            self.model = create_cnn1d_large(input_length, num_classes)
        else:
            self.model = create_cnn1d_medium(input_length, num_classes)
        self._compile()

    def _compile(self):
        self.model.compile(optimizer=keras.optimizers.Adam(learning_rate=self.learning_rate), loss='sparse_categorical_crossentropy', metrics=['accuracy'])

    def fit(self, X_train, y_train, X_val=None, y_val=None, epochs: int=100, batch_size: int=32, early_stopping_patience: int=15, verbose: int=1) -> keras.callbacks.History:
        if len(X_train.shape) == 2:
            X_train = X_train[..., None]
        if X_val is not None and len(X_val.shape) == 2:
            X_val = X_val[..., None]
        callbacks = [keras.callbacks.EarlyStopping(monitor='val_loss' if X_val is not None else 'loss', patience=early_stopping_patience, restore_best_weights=True), keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=5, min_lr=1e-06)]
        validation_data = (X_val, y_val) if X_val is not None else None
        return self.model.fit(X_train, y_train, validation_data=validation_data, epochs=epochs, batch_size=batch_size, callbacks=callbacks, verbose=verbose)

    def predict(self, X):
        if len(X.shape) == 2:
            X = X[..., None]
        return self.model.predict(X).argmax(axis=1)

    def predict_proba(self, X):
        if len(X.shape) == 2:
            X = X[..., None]
        return self.model.predict(X)

    def evaluate(self, X, y) -> Dict[str, float]:
        if len(X.shape) == 2:
            X = X[..., None]
        loss, accuracy = self.model.evaluate(X, y, verbose=0)
        return {'loss': loss, 'accuracy': accuracy}

    def save(self, filepath: str):
        self.model.save(filepath)

    def summary(self):
        self.model.summary()