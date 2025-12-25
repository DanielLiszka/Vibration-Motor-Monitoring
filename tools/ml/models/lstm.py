import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers, regularizers
from typing import List, Optional, Tuple, Dict

def create_lstm_classifier(sequence_length: int, num_features: int, num_classes: int=5, lstm_units: List[int]=[64, 32], dense_units: List[int]=[32], dropout_rate: float=0.3, recurrent_dropout: float=0.2, bidirectional: bool=False, l2_reg: float=0.001, name: str='lstm_classifier') -> keras.Model:
    inputs = keras.Input(shape=(sequence_length, num_features), name='input_sequence')
    x = inputs
    for i, units in enumerate(lstm_units):
        return_sequences = i < len(lstm_units) - 1
        lstm_layer = layers.LSTM(units, return_sequences=return_sequences, dropout=dropout_rate, recurrent_dropout=recurrent_dropout, kernel_regularizer=regularizers.l2(l2_reg), name=f'lstm_{i}')
        if bidirectional:
            x = layers.Bidirectional(lstm_layer, name=f'bilstm_{i}')(x)
        else:
            x = lstm_layer(x)
    for i, units in enumerate(dense_units):
        x = layers.Dense(units, kernel_regularizer=regularizers.l2(l2_reg), name=f'dense_{i}')(x)
        x = layers.BatchNormalization(name=f'bn_{i}')(x)
        x = layers.Activation('relu', name=f'act_{i}')(x)
        x = layers.Dropout(dropout_rate, name=f'dropout_{i}')(x)
    outputs = layers.Dense(num_classes, activation='softmax', name='output')(x)
    model = keras.Model(inputs=inputs, outputs=outputs, name=name)
    return model

def create_lstm_small(sequence_length: int, num_features: int, num_classes: int=5) -> keras.Model:
    return create_lstm_classifier(sequence_length=sequence_length, num_features=num_features, num_classes=num_classes, lstm_units=[32], dense_units=[16], dropout_rate=0.2, recurrent_dropout=0.1, bidirectional=False, name='lstm_small')

def create_lstm_medium(sequence_length: int, num_features: int, num_classes: int=5) -> keras.Model:
    return create_lstm_classifier(sequence_length=sequence_length, num_features=num_features, num_classes=num_classes, lstm_units=[64, 32], dense_units=[32], dropout_rate=0.3, recurrent_dropout=0.2, bidirectional=False, name='lstm_medium')

def create_lstm_large(sequence_length: int, num_features: int, num_classes: int=5) -> keras.Model:
    return create_lstm_classifier(sequence_length=sequence_length, num_features=num_features, num_classes=num_classes, lstm_units=[64, 32], dense_units=[64, 32], dropout_rate=0.4, recurrent_dropout=0.2, bidirectional=True, name='lstm_large')

def create_gru_classifier(sequence_length: int, num_features: int, num_classes: int=5, gru_units: List[int]=[64, 32], dense_units: List[int]=[32], dropout_rate: float=0.3, name: str='gru_classifier') -> keras.Model:
    inputs = keras.Input(shape=(sequence_length, num_features), name='input_sequence')
    x = inputs
    for i, units in enumerate(gru_units):
        return_sequences = i < len(gru_units) - 1
        x = layers.GRU(units, return_sequences=return_sequences, dropout=dropout_rate, recurrent_dropout=dropout_rate / 2, name=f'gru_{i}')(x)
    for i, units in enumerate(dense_units):
        x = layers.Dense(units, activation='relu', name=f'dense_{i}')(x)
        x = layers.Dropout(dropout_rate, name=f'dropout_{i}')(x)
    outputs = layers.Dense(num_classes, activation='softmax', name='output')(x)
    return keras.Model(inputs=inputs, outputs=outputs, name=name)

class LSTMClassifier:

    def __init__(self, sequence_length: int, num_features: int, num_classes: int=5, model_size: str='medium', learning_rate: float=0.001, use_gru: bool=False):
        self.sequence_length = sequence_length
        self.num_features = num_features
        self.num_classes = num_classes
        self.learning_rate = learning_rate
        if use_gru:
            gru_units = [32] if model_size == 'small' else [64, 32]
            self.model = create_gru_classifier(sequence_length, num_features, num_classes, gru_units)
        elif model_size == 'small':
            self.model = create_lstm_small(sequence_length, num_features, num_classes)
        elif model_size == 'large':
            self.model = create_lstm_large(sequence_length, num_features, num_classes)
        else:
            self.model = create_lstm_medium(sequence_length, num_features, num_classes)
        self._compile()

    def _compile(self):
        self.model.compile(optimizer=keras.optimizers.Adam(learning_rate=self.learning_rate), loss='sparse_categorical_crossentropy', metrics=['accuracy'])

    def fit(self, X_train, y_train, X_val=None, y_val=None, epochs: int=100, batch_size: int=32, early_stopping_patience: int=15, verbose: int=1) -> keras.callbacks.History:
        callbacks = [keras.callbacks.EarlyStopping(monitor='val_loss' if X_val is not None else 'loss', patience=early_stopping_patience, restore_best_weights=True), keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=5, min_lr=1e-06)]
        validation_data = (X_val, y_val) if X_val is not None else None
        return self.model.fit(X_train, y_train, validation_data=validation_data, epochs=epochs, batch_size=batch_size, callbacks=callbacks, verbose=verbose)

    def predict(self, X):
        return self.model.predict(X).argmax(axis=1)

    def predict_proba(self, X):
        return self.model.predict(X)

    def evaluate(self, X, y) -> Dict[str, float]:
        loss, accuracy = self.model.evaluate(X, y, verbose=0)
        return {'loss': loss, 'accuracy': accuracy}

    def save(self, filepath: str):
        self.model.save(filepath)

    def summary(self):
        self.model.summary()

def create_stateful_lstm(num_features: int, batch_size: int, lstm_units: int=32, num_classes: int=5) -> keras.Model:
    inputs = keras.Input(batch_shape=(batch_size, 1, num_features), name='input')
    x = layers.LSTM(lstm_units, stateful=True, return_sequences=False, name='lstm')(inputs)
    x = layers.Dense(16, activation='relu', name='dense')(x)
    outputs = layers.Dense(num_classes, activation='softmax', name='output')(x)
    model = keras.Model(inputs=inputs, outputs=outputs, name='stateful_lstm')
    return model