import numpy as np
from typing import Dict, List, Any, Callable, Optional, Tuple
from itertools import product
import warnings
import time
import json

class GridSearch:

    def __init__(self, param_grid: Dict[str, List], scoring: str='accuracy', cv: int=3, verbose: bool=True):
        self.param_grid = param_grid
        self.scoring = scoring
        self.cv = cv
        self.verbose = verbose
        self.results_ = []
        self.best_params_ = None
        self.best_score_ = -np.inf

    def _generate_combinations(self) -> List[Dict]:
        keys = list(self.param_grid.keys())
        values = list(self.param_grid.values())
        combinations = []
        for combo in product(*values):
            combinations.append(dict(zip(keys, combo)))
        return combinations

    def fit(self, model_factory: Callable, X: np.ndarray, y: np.ndarray) -> 'GridSearch':
        from sklearn.model_selection import StratifiedKFold
        combinations = self._generate_combinations()
        n_combinations = len(combinations)
        if self.verbose:
            print(f'Grid Search: {n_combinations} combinations, {self.cv}-fold CV')
            print(f'Parameters: {list(self.param_grid.keys())}')
            print('-' * 50)
        kfold = StratifiedKFold(n_splits=self.cv, shuffle=True, random_state=42)
        for i, params in enumerate(combinations):
            start_time = time.time()
            scores = []
            for train_idx, val_idx in kfold.split(X, y):
                model = model_factory(**params)
                model.fit(X[train_idx], y[train_idx], verbose=0)
                eval_result = model.evaluate(X[val_idx], y[val_idx])
                scores.append(eval_result[self.scoring])
            mean_score = np.mean(scores)
            std_score = np.std(scores)
            elapsed = time.time() - start_time
            result = {'params': params, 'mean_score': mean_score, 'std_score': std_score, 'scores': scores, 'time': elapsed}
            self.results_.append(result)
            if mean_score > self.best_score_:
                self.best_score_ = mean_score
                self.best_params_ = params
            if self.verbose:
                print(f'[{i + 1}/{n_combinations}] {self.scoring}={mean_score:.4f}±{std_score:.4f} ({elapsed:.1f}s) {params}')
        if self.verbose:
            print('-' * 50)
            print(f'Best: {self.best_score_:.4f} with {self.best_params_}')
        return self

    def get_results_df(self):
        import pandas as pd
        rows = []
        for r in self.results_:
            row = r['params'].copy()
            row['mean_score'] = r['mean_score']
            row['std_score'] = r['std_score']
            row['time'] = r['time']
            rows.append(row)
        return pd.DataFrame(rows).sort_values('mean_score', ascending=False)

class RandomSearch:

    def __init__(self, param_distributions: Dict[str, Any], n_iter: int=20, scoring: str='accuracy', cv: int=3, random_state: int=42, verbose: bool=True):
        self.param_distributions = param_distributions
        self.n_iter = n_iter
        self.scoring = scoring
        self.cv = cv
        self.rng = np.random.default_rng(random_state)
        self.verbose = verbose
        self.results_ = []
        self.best_params_ = None
        self.best_score_ = -np.inf

    def _sample_params(self) -> Dict:
        params = {}
        for name, dist in self.param_distributions.items():
            if isinstance(dist, list):
                params[name] = self.rng.choice(dist)
            elif hasattr(dist, 'rvs'):
                params[name] = dist.rvs(random_state=self.rng)
            elif callable(dist):
                params[name] = dist(self.rng)
            else:
                params[name] = dist
        return params

    def fit(self, model_factory: Callable, X: np.ndarray, y: np.ndarray) -> 'RandomSearch':
        from sklearn.model_selection import StratifiedKFold
        if self.verbose:
            print(f'Random Search: {self.n_iter} iterations, {self.cv}-fold CV')
            print('-' * 50)
        kfold = StratifiedKFold(n_splits=self.cv, shuffle=True, random_state=42)
        for i in range(self.n_iter):
            params = self._sample_params()
            start_time = time.time()
            scores = []
            for train_idx, val_idx in kfold.split(X, y):
                try:
                    model = model_factory(**params)
                    model.fit(X[train_idx], y[train_idx], verbose=0)
                    eval_result = model.evaluate(X[val_idx], y[val_idx])
                    scores.append(eval_result[self.scoring])
                except Exception as e:
                    if self.verbose:
                        print(f'  Error with params {params}: {e}')
                    scores = []
                    break
            if len(scores) == 0:
                continue
            mean_score = np.mean(scores)
            std_score = np.std(scores)
            elapsed = time.time() - start_time
            result = {'params': params, 'mean_score': mean_score, 'std_score': std_score, 'time': elapsed}
            self.results_.append(result)
            if mean_score > self.best_score_:
                self.best_score_ = mean_score
                self.best_params_ = params
            if self.verbose:
                print(f'[{i + 1}/{self.n_iter}] {self.scoring}={mean_score:.4f}±{std_score:.4f} ({elapsed:.1f}s)')
        if self.verbose:
            print('-' * 50)
            print(f'Best: {self.best_score_:.4f} with {self.best_params_}')
        return self

class BayesianOptimization:

    def __init__(self, param_space: Dict[str, Tuple], n_iter: int=30, n_initial: int=5, scoring: str='accuracy', cv: int=3, random_state: int=42, verbose: bool=True):
        self.param_space = param_space
        self.n_iter = n_iter
        self.n_initial = n_initial
        self.scoring = scoring
        self.cv = cv
        self.random_state = random_state
        self.verbose = verbose
        self.results_ = []
        self.best_params_ = None
        self.best_score_ = -np.inf

    def fit(self, model_factory: Callable, X: np.ndarray, y: np.ndarray) -> 'BayesianOptimization':
        try:
            from skopt import gp_minimize
            from skopt.space import Real, Integer, Categorical
        except ImportError:
            warnings.warn('scikit-optimize not installed. Using RandomSearch instead.')
            param_distributions = {}
            for name, space in self.param_space.items():
                if isinstance(space, tuple) and len(space) >= 2:
                    param_distributions[name] = list(np.linspace(space[0], space[1], 10))
                else:
                    param_distributions[name] = space
            rs = RandomSearch(param_distributions, self.n_iter, self.scoring, self.cv)
            rs.fit(model_factory, X, y)
            self.results_ = rs.results_
            self.best_params_ = rs.best_params_
            self.best_score_ = rs.best_score_
            return self
        from sklearn.model_selection import StratifiedKFold
        dimensions = []
        param_names = []
        for name, space in self.param_space.items():
            param_names.append(name)
            if isinstance(space, tuple):
                if len(space) == 3 and space[2] == 'log':
                    dimensions.append(Real(space[0], space[1], prior='log-uniform', name=name))
                elif isinstance(space[0], int):
                    dimensions.append(Integer(space[0], space[1], name=name))
                else:
                    dimensions.append(Real(space[0], space[1], name=name))
            elif isinstance(space, list):
                dimensions.append(Categorical(space, name=name))
            else:
                dimensions.append(Categorical([space], name=name))
        kfold = StratifiedKFold(n_splits=self.cv, shuffle=True, random_state=42)

        def objective(params_list):
            params = dict(zip(param_names, params_list))
            scores = []
            for train_idx, val_idx in kfold.split(X, y):
                try:
                    model = model_factory(**params)
                    model.fit(X[train_idx], y[train_idx], verbose=0)
                    eval_result = model.evaluate(X[val_idx], y[val_idx])
                    scores.append(eval_result[self.scoring])
                except Exception:
                    return 0.0
            mean_score = np.mean(scores)
            self.results_.append({'params': params, 'mean_score': mean_score, 'std_score': np.std(scores)})
            if mean_score > self.best_score_:
                self.best_score_ = mean_score
                self.best_params_ = params
            if self.verbose:
                print(f'[{len(self.results_)}/{self.n_iter}] {self.scoring}={mean_score:.4f}')
            return -mean_score
        result = gp_minimize(objective, dimensions, n_calls=self.n_iter, n_initial_points=self.n_initial, random_state=self.random_state, verbose=False)
        if self.verbose:
            print('-' * 50)
            print(f'Best: {self.best_score_:.4f} with {self.best_params_}')
        return self

def learning_rate_finder(model_factory: Callable, X: np.ndarray, y: np.ndarray, min_lr: float=1e-07, max_lr: float=1.0, num_steps: int=100, batch_size: int=32, verbose: bool=True) -> Tuple[List[float], List[float]]:
    import tensorflow as tf
    lrs = np.logspace(np.log10(min_lr), np.log10(max_lr), num_steps)
    losses = []
    model = model_factory(learning_rate=min_lr)
    keras_model = model.model if hasattr(model, 'model') else model
    if len(X.shape) == 2 and hasattr(keras_model.input_shape, '__len__') and (len(keras_model.input_shape) == 3):
        X = X[..., np.newaxis]
    dataset = tf.data.Dataset.from_tensor_slices((X, y))
    dataset = dataset.shuffle(len(X)).batch(batch_size).repeat()
    iterator = iter(dataset)
    for i, lr in enumerate(lrs):
        keras_model.optimizer.learning_rate.assign(lr)
        batch_x, batch_y = next(iterator)
        loss = keras_model.train_on_batch(batch_x, batch_y)
        if isinstance(loss, list):
            loss = loss[0]
        losses.append(loss)
        if verbose and (i + 1) % 10 == 0:
            print(f'Step {i + 1}/{num_steps}: LR={lr:.2e}, Loss={loss:.4f}')
        if np.isnan(loss) or loss > 100 * losses[0]:
            break
    return (lrs[:len(losses)], losses)

def suggest_learning_rate(lrs: List[float], losses: List[float]) -> float:
    from scipy.ndimage import uniform_filter1d
    smoothed = uniform_filter1d(losses, size=5)
    gradients = np.gradient(smoothed)
    min_grad_idx = np.argmin(gradients)
    suggested_idx = max(0, min_grad_idx - 5)
    suggested_lr = lrs[suggested_idx]
    return suggested_lr

def save_search_results(results: List[Dict], filepath: str):
    serializable = []
    for r in results:
        item = {'params': {k: float(v) if isinstance(v, (np.floating, np.integer)) else v for k, v in r['params'].items()}, 'mean_score': float(r['mean_score']), 'std_score': float(r['std_score'])}
        serializable.append(item)
    with open(filepath, 'w') as f:
        json.dump(serializable, f, indent=2)