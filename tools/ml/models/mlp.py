import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers, regularizers
from typing import List, Optional, Tuple, Dict, Any

def create_mlp_classifier(input_dim: int, num_classes: int=5, hidden_layers: List[int]=[64, 32, 16], dropout_rate: float=0.3, l2_reg: float=0.001, activation: str='relu', use_batch_norm: bool=True, name: str='mlp_classifier') -> keras.Model:
    inputs = keras.Input(shape=(input_dim,), name='input_features')
    x = inputs
    for i, units in enumerate(hidden_layers):
        x = layers.Dense(units, kernel_regularizer=regularizers.l2(l2_reg), name=f'dense_{i}')(x)
        if use_batch_norm:
            x = layers.BatchNormalization(name=f'bn_{i}')(x)
        x = layers.Activation(activation, name=f'act_{i}')(x)
        x = layers.Dropout(dropout_rate, name=f'dropout_{i}')(x)
    outputs = layers.Dense(num_classes, activation='softmax', name='output')(x)
    model = keras.Model(inputs=inputs, outputs=outputs, name=name)
    return model

def create_mlp_small(input_dim: int, num_classes: int=5) -> keras.Model:
    return create_mlp_classifier(input_dim=input_dim, num_classes=num_classes, hidden_layers=[32, 16], dropout_rate=0.2, l2_reg=0.01, use_batch_norm=False, name='mlp_small')

def create_mlp_medium(input_dim: int, num_classes: int=5) -> keras.Model:
    return create_mlp_classifier(input_dim=input_dim, num_classes=num_classes, hidden_layers=[64, 32, 16], dropout_rate=0.3, l2_reg=0.001, use_batch_norm=True, name='mlp_medium')

def create_mlp_large(input_dim: int, num_classes: int=5) -> keras.Model:
    return create_mlp_classifier(input_dim=input_dim, num_classes=num_classes, hidden_layers=[128, 64, 32, 16], dropout_rate=0.4, l2_reg=0.0001, use_batch_norm=True, name='mlp_large')

class MLPClassifier:

    def __init__(self, input_dim: int, num_classes: int=5, model_size: str='medium', learning_rate: float=0.001):
        self.input_dim = input_dim
        self.num_classes = num_classes
        self.learning_rate = learning_rate
        if model_size == 'small':
            self.model = create_mlp_small(input_dim, num_classes)
        elif model_size == 'large':
            self.model = create_mlp_large(input_dim, num_classes)
        else:
            self.model = create_mlp_medium(input_dim, num_classes)
        self._compile()

    def _compile(self):
        self.model.compile(optimizer=keras.optimizers.Adam(learning_rate=self.learning_rate), loss='sparse_categorical_crossentropy', metrics=['accuracy'])

    def fit(self, X_train, y_train, X_val=None, y_val=None, epochs: int=100, batch_size: int=32, early_stopping_patience: int=10, reduce_lr_patience: int=5, verbose: int=1) -> keras.callbacks.History:
        callbacks = [keras.callbacks.EarlyStopping(monitor='val_loss' if X_val is not None else 'loss', patience=early_stopping_patience, restore_best_weights=True), keras.callbacks.ReduceLROnPlateau(monitor='val_loss' if X_val is not None else 'loss', factor=0.5, patience=reduce_lr_patience, min_lr=1e-06)]
        validation_data = (X_val, y_val) if X_val is not None else None
        history = self.model.fit(X_train, y_train, validation_data=validation_data, epochs=epochs, batch_size=batch_size, callbacks=callbacks, verbose=verbose)
        return history

    def predict(self, X):
        return self.model.predict(X).argmax(axis=1)

    def predict_proba(self, X):
        return self.model.predict(X)

    def evaluate(self, X, y) -> Dict[str, float]:
        loss, accuracy = self.model.evaluate(X, y, verbose=0)
        return {'loss': loss, 'accuracy': accuracy}

    def save(self, filepath: str):
        self.model.save(filepath)

    def load(self, filepath: str):
        self.model = keras.models.load_model(filepath)

    def get_model_size(self) -> int:
        total_params = self.model.count_params()
        return total_params

    def summary(self):
        self.model.summary()