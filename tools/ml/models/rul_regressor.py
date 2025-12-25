import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers, regularizers
from typing import List, Optional, Tuple, Dict
import numpy as np

def create_rul_mlp(input_dim: int, hidden_layers: List[int]=[64, 32, 16], dropout_rate: float=0.3, l2_reg: float=0.001, output_activation: str='linear', name: str='rul_mlp') -> keras.Model:
    inputs = keras.Input(shape=(input_dim,), name='input_features')
    x = inputs
    for i, units in enumerate(hidden_layers):
        x = layers.Dense(units, kernel_regularizer=regularizers.l2(l2_reg), name=f'dense_{i}')(x)
        x = layers.BatchNormalization(name=f'bn_{i}')(x)
        x = layers.Activation('relu', name=f'act_{i}')(x)
        x = layers.Dropout(dropout_rate, name=f'dropout_{i}')(x)
    outputs = layers.Dense(1, activation=output_activation, name='rul_output')(x)
    return keras.Model(inputs=inputs, outputs=outputs, name=name)

def create_rul_lstm(sequence_length: int, num_features: int, lstm_units: List[int]=[64, 32], dense_units: List[int]=[32], dropout_rate: float=0.3, name: str='rul_lstm') -> keras.Model:
    inputs = keras.Input(shape=(sequence_length, num_features), name='input_sequence')
    x = inputs
    for i, units in enumerate(lstm_units):
        return_sequences = i < len(lstm_units) - 1
        x = layers.LSTM(units, return_sequences=return_sequences, dropout=dropout_rate, recurrent_dropout=dropout_rate / 2, name=f'lstm_{i}')(x)
    for i, units in enumerate(dense_units):
        x = layers.Dense(units, activation='relu', name=f'dense_{i}')(x)
        x = layers.Dropout(dropout_rate, name=f'dropout_{i}')(x)
    outputs = layers.Dense(1, activation='linear', name='rul_output')(x)
    return keras.Model(inputs=inputs, outputs=outputs, name=name)

def create_rul_cnn_lstm(sequence_length: int, num_features: int, conv_filters: List[int]=[32, 64], lstm_units: int=32, name: str='rul_cnn_lstm') -> keras.Model:
    inputs = keras.Input(shape=(sequence_length, num_features), name='input_sequence')
    x = inputs
    for i, filters in enumerate(conv_filters):
        x = layers.Conv1D(filters, 3, padding='same', activation='relu', name=f'conv_{i}')(x)
        x = layers.MaxPooling1D(2, name=f'pool_{i}')(x)
    x = layers.LSTM(lstm_units, name='lstm')(x)
    x = layers.Dense(32, activation='relu', name='dense')(x)
    x = layers.Dropout(0.3, name='dropout')(x)
    outputs = layers.Dense(1, activation='linear', name='rul_output')(x)
    return keras.Model(inputs=inputs, outputs=outputs, name=name)

def create_multi_task_rul(input_dim: int, num_degradation_stages: int=4, hidden_layers: List[int]=[64, 32], name: str='multi_task_rul') -> keras.Model:
    inputs = keras.Input(shape=(input_dim,), name='input_features')
    x = inputs
    for i, units in enumerate(hidden_layers):
        x = layers.Dense(units, name=f'shared_dense_{i}')(x)
        x = layers.BatchNormalization(name=f'shared_bn_{i}')(x)
        x = layers.Activation('relu', name=f'shared_act_{i}')(x)
        x = layers.Dropout(0.3, name=f'shared_dropout_{i}')(x)
    rul_x = layers.Dense(16, activation='relu', name='rul_dense')(x)
    rul_output = layers.Dense(1, activation='linear', name='rul_output')(rul_x)
    stage_x = layers.Dense(16, activation='relu', name='stage_dense')(x)
    stage_output = layers.Dense(num_degradation_stages, activation='softmax', name='stage_output')(stage_x)
    return keras.Model(inputs=inputs, outputs=[rul_output, stage_output], name=name)

def create_uncertainty_rul(input_dim: int, hidden_layers: List[int]=[64, 32], name: str='uncertainty_rul') -> keras.Model:
    inputs = keras.Input(shape=(input_dim,), name='input_features')
    x = inputs
    for i, units in enumerate(hidden_layers):
        x = layers.Dense(units, activation='relu', name=f'dense_{i}')(x)
        x = layers.Dropout(0.3, name=f'dropout_{i}')(x)
    mean = layers.Dense(1, activation='linear', name='mean')(x)
    log_var = layers.Dense(1, activation='linear', name='log_var')(x)
    outputs = layers.Concatenate(name='output')([mean, log_var])
    return keras.Model(inputs=inputs, outputs=outputs, name=name)

def gaussian_nll_loss(y_true, y_pred):
    mean = y_pred[:, 0:1]
    log_var = y_pred[:, 1:2]
    log_var = tf.clip_by_value(log_var, -10, 10)
    precision = tf.exp(-log_var)
    mse = tf.square(y_true - mean)
    loss = 0.5 * tf.reduce_mean(log_var + mse * precision)
    return loss

class RULPredictor:

    def __init__(self, input_dim: int=None, sequence_length: int=None, num_features: int=None, model_type: str='mlp', with_uncertainty: bool=False, learning_rate: float=0.001):
        self.model_type = model_type
        self.with_uncertainty = with_uncertainty
        self.learning_rate = learning_rate
        if model_type == 'lstm':
            assert sequence_length and num_features
            self.model = create_rul_lstm(sequence_length, num_features)
        elif model_type == 'cnn_lstm':
            assert sequence_length and num_features
            self.model = create_rul_cnn_lstm(sequence_length, num_features)
        elif with_uncertainty:
            assert input_dim
            self.model = create_uncertainty_rul(input_dim)
        else:
            assert input_dim
            self.model = create_rul_mlp(input_dim)
        self._compile()

    def _compile(self):
        if self.with_uncertainty:
            self.model.compile(optimizer=keras.optimizers.Adam(learning_rate=self.learning_rate), loss=gaussian_nll_loss)
        else:
            self.model.compile(optimizer=keras.optimizers.Adam(learning_rate=self.learning_rate), loss='mse', metrics=['mae'])

    def fit(self, X_train, y_train, X_val=None, y_val=None, epochs: int=100, batch_size: int=32, early_stopping_patience: int=15, verbose: int=1) -> keras.callbacks.History:
        callbacks = [keras.callbacks.EarlyStopping(monitor='val_loss' if X_val is not None else 'loss', patience=early_stopping_patience, restore_best_weights=True), keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=5, min_lr=1e-06)]
        validation_data = (X_val, y_val) if X_val is not None else None
        return self.model.fit(X_train, y_train, validation_data=validation_data, epochs=epochs, batch_size=batch_size, callbacks=callbacks, verbose=verbose)

    def predict(self, X) -> np.ndarray:
        predictions = self.model.predict(X)
        if self.with_uncertainty:
            mean = predictions[:, 0]
            log_var = predictions[:, 1]
            std = np.sqrt(np.exp(log_var))
            return (mean, std)
        else:
            return predictions.flatten()

    def predict_with_confidence(self, X, confidence: float=0.95) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        if not self.with_uncertainty:
            mean = self.predict(X)
            return (mean, mean, mean)
        mean, std = self.predict(X)
        from scipy.stats import norm
        z = norm.ppf((1 + confidence) / 2)
        lower = mean - z * std
        upper = mean + z * std
        return (mean, lower, upper)

    def evaluate(self, X, y) -> Dict[str, float]:
        if self.with_uncertainty:
            loss = self.model.evaluate(X, y, verbose=0)
            return {'nll_loss': loss}
        else:
            loss, mae = self.model.evaluate(X, y, verbose=0)
            return {'mse': loss, 'mae': mae}

    def save(self, filepath: str):
        self.model.save(filepath)

    def summary(self):
        self.model.summary()

def piecewise_linear_rul(current_step: int, total_steps: int, max_rul: float=125.0) -> float:
    remaining = total_steps - current_step
    if remaining >= max_rul:
        return max_rul
    else:
        return max(0.0, float(remaining))

def exponential_rul(health_index: float, failure_threshold: float=0.3, time_constant: float=100.0) -> float:
    if health_index <= failure_threshold:
        return 0.0
    import math
    rul = -time_constant * math.log(failure_threshold / health_index)
    return max(0.0, rul)