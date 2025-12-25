import numpy as np
from typing import Tuple, Optional, Dict
from collections import Counter
import warnings

def compute_class_weights(y: np.ndarray, method: str='balanced') -> Dict[int, float]:
    classes, counts = np.unique(y, return_counts=True)
    n_samples = len(y)
    n_classes = len(classes)
    if method == 'balanced':
        weights = n_samples / (n_classes * counts)
    elif method == 'sqrt':
        weights = np.sqrt(n_samples / (n_classes * counts))
    elif method == 'inverse':
        weights = 1.0 / counts
        weights = weights / weights.sum() * n_classes
    else:
        weights = np.ones(n_classes)
    return {int(c): float(w) for c, w in zip(classes, weights)}

def compute_sample_weights(y: np.ndarray, class_weights: Dict[int, float]=None) -> np.ndarray:
    if class_weights is None:
        class_weights = compute_class_weights(y)
    sample_weights = np.array([class_weights[int(label)] for label in y])
    return sample_weights

class RandomOversampler:

    def __init__(self, sampling_strategy: str='auto', random_state: int=42):
        self.sampling_strategy = sampling_strategy
        self.rng = np.random.default_rng(random_state)

    def fit_resample(self, X: np.ndarray, y: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        classes, counts = np.unique(y, return_counts=True)
        max_count = counts.max()
        X_resampled = [X]
        y_resampled = [y]
        for cls, count in zip(classes, counts):
            if count < max_count:
                cls_indices = np.where(y == cls)[0]
                n_to_add = max_count - count
                new_indices = self.rng.choice(cls_indices, size=n_to_add, replace=True)
                X_resampled.append(X[new_indices])
                y_resampled.append(y[new_indices])
        return (np.vstack(X_resampled), np.hstack(y_resampled))

class RandomUndersampler:

    def __init__(self, sampling_strategy: str='auto', random_state: int=42):
        self.sampling_strategy = sampling_strategy
        self.rng = np.random.default_rng(random_state)

    def fit_resample(self, X: np.ndarray, y: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        classes, counts = np.unique(y, return_counts=True)
        min_count = counts.min()
        X_resampled = []
        y_resampled = []
        for cls in classes:
            cls_indices = np.where(y == cls)[0]
            selected = self.rng.choice(cls_indices, size=min_count, replace=False)
            X_resampled.append(X[selected])
            y_resampled.append(y[selected])
        return (np.vstack(X_resampled), np.hstack(y_resampled))

class SMOTE:

    def __init__(self, k_neighbors: int=5, sampling_strategy: str='auto', random_state: int=42):
        self.k_neighbors = k_neighbors
        self.sampling_strategy = sampling_strategy
        self.rng = np.random.default_rng(random_state)

    def _find_neighbors(self, X: np.ndarray, n_neighbors: int) -> np.ndarray:
        from scipy.spatial.distance import cdist
        distances = cdist(X, X, metric='euclidean')
        np.fill_diagonal(distances, np.inf)
        neighbors = np.argsort(distances, axis=1)[:, :n_neighbors]
        return neighbors

    def fit_resample(self, X: np.ndarray, y: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        classes, counts = np.unique(y, return_counts=True)
        max_count = counts.max()
        X_resampled = [X.copy()]
        y_resampled = [y.copy()]
        for cls, count in zip(classes, counts):
            if count < max_count:
                n_to_generate = max_count - count
                cls_indices = np.where(y == cls)[0]
                X_cls = X[cls_indices]
                k = min(self.k_neighbors, len(X_cls) - 1)
                if k < 1:
                    indices = self.rng.choice(len(X_cls), size=n_to_generate, replace=True)
                    X_resampled.append(X_cls[indices])
                    y_resampled.append(np.full(n_to_generate, cls))
                    continue
                neighbors = self._find_neighbors(X_cls, k)
                synthetic_X = []
                for _ in range(n_to_generate):
                    idx = self.rng.integers(0, len(X_cls))
                    sample = X_cls[idx]
                    neighbor_idx = self.rng.choice(neighbors[idx])
                    neighbor = X_cls[neighbor_idx]
                    alpha = self.rng.random()
                    synthetic = sample + alpha * (neighbor - sample)
                    synthetic_X.append(synthetic)
                X_resampled.append(np.array(synthetic_X))
                y_resampled.append(np.full(n_to_generate, cls))
        return (np.vstack(X_resampled), np.hstack(y_resampled))

class BorderlineSMOTE(SMOTE):

    def fit_resample(self, X: np.ndarray, y: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        from scipy.spatial.distance import cdist
        classes, counts = np.unique(y, return_counts=True)
        max_count = counts.max()
        X_resampled = [X.copy()]
        y_resampled = [y.copy()]
        for cls, count in zip(classes, counts):
            if count < max_count:
                n_to_generate = max_count - count
                cls_indices = np.where(y == cls)[0]
                X_cls = X[cls_indices]
                k = min(self.k_neighbors, len(X) - 1)
                distances = cdist(X_cls, X)
                borderline_indices = []
                for i, x_i in enumerate(X_cls):
                    nn_indices = np.argsort(distances[i])[:k + 1]
                    nn_indices = nn_indices[nn_indices != cls_indices[i]][:k]
                    n_different = np.sum(y[nn_indices] != cls)
                    if k / 2 < n_different < k:
                        borderline_indices.append(i)
                if len(borderline_indices) == 0:
                    borderline_indices = list(range(len(X_cls)))
                X_borderline = X_cls[borderline_indices]
                neighbors = self._find_neighbors(X_borderline, min(self.k_neighbors, len(X_borderline) - 1))
                synthetic_X = []
                for _ in range(n_to_generate):
                    idx = self.rng.integers(0, len(X_borderline))
                    sample = X_borderline[idx]
                    if len(neighbors[idx]) > 0:
                        neighbor_idx = self.rng.choice(neighbors[idx])
                        neighbor = X_borderline[neighbor_idx]
                    else:
                        neighbor = sample
                    alpha = self.rng.random()
                    synthetic = sample + alpha * (neighbor - sample)
                    synthetic_X.append(synthetic)
                if len(synthetic_X) > 0:
                    X_resampled.append(np.array(synthetic_X))
                    y_resampled.append(np.full(n_to_generate, cls))
        return (np.vstack(X_resampled), np.hstack(y_resampled))

class BalancedBatchGenerator:

    def __init__(self, batch_size: int=32, random_state: int=42):
        self.batch_size = batch_size
        self.rng = np.random.default_rng(random_state)

    def flow(self, X: np.ndarray, y: np.ndarray, shuffle: bool=True):
        classes = np.unique(y)
        n_classes = len(classes)
        samples_per_class = self.batch_size // n_classes
        class_indices = {c: np.where(y == c)[0] for c in classes}
        if shuffle:
            for c in classes:
                self.rng.shuffle(class_indices[c])
        positions = {c: 0 for c in classes}
        while True:
            batch_X = []
            batch_y = []
            for c in classes:
                indices = class_indices[c]
                pos = positions[c]
                selected_indices = []
                for _ in range(samples_per_class):
                    if pos >= len(indices):
                        if shuffle:
                            self.rng.shuffle(indices)
                        pos = 0
                    selected_indices.append(indices[pos])
                    pos += 1
                positions[c] = pos
                batch_X.append(X[selected_indices])
                batch_y.append(y[selected_indices])
            batch_X = np.vstack(batch_X)
            batch_y = np.hstack(batch_y)
            if shuffle:
                perm = self.rng.permutation(len(batch_X))
                batch_X = batch_X[perm]
                batch_y = batch_y[perm]
            yield (batch_X, batch_y)

def focal_loss(gamma: float=2.0, alpha: float=0.25):
    import tensorflow as tf

    def focal_loss_fn(y_true, y_pred):
        y_pred = tf.clip_by_value(y_pred, 1e-07, 1 - 1e-07)
        ce = -y_true * tf.math.log(y_pred)
        weight = alpha * y_true * tf.pow(1 - y_pred, gamma)
        return tf.reduce_sum(weight * ce, axis=-1)
    return focal_loss_fn

def print_class_distribution(y: np.ndarray, title: str='Class Distribution'):
    classes, counts = np.unique(y, return_counts=True)
    total = len(y)
    print(f'\n{title}')
    print('-' * 40)
    for c, count in zip(classes, counts):
        pct = 100 * count / total
        bar = '█' * int(pct / 2)
        print(f'Class {c}: {count:6d} ({pct:5.1f}%) {bar}')
    print(f'Total:   {total:6d}')