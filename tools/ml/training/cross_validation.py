import numpy as np
from sklearn.model_selection import StratifiedKFold, TimeSeriesSplit, GroupKFold, LeaveOneGroupOut
from typing import List, Tuple, Dict, Generator, Optional, Any, Callable
import warnings

class StratifiedCrossValidator:

    def __init__(self, n_splits: int=5, shuffle: bool=True, random_state: int=42):
        self.n_splits = n_splits
        self.kfold = StratifiedKFold(n_splits=n_splits, shuffle=shuffle, random_state=random_state)

    def split(self, X: np.ndarray, y: np.ndarray) -> Generator[Tuple[np.ndarray, np.ndarray], None, None]:
        for train_idx, test_idx in self.kfold.split(X, y):
            yield (train_idx, test_idx)

    def cross_validate(self, model_factory: Callable, X: np.ndarray, y: np.ndarray, fit_kwargs: Dict=None, verbose: bool=True) -> Dict[str, List[float]]:
        if fit_kwargs is None:
            fit_kwargs = {}
        results = {'train_loss': [], 'train_acc': [], 'val_loss': [], 'val_acc': []}
        for fold, (train_idx, test_idx) in enumerate(self.split(X, y)):
            if verbose:
                print(f'\nFold {fold + 1}/{self.n_splits}')
            X_train, X_val = (X[train_idx], X[test_idx])
            y_train, y_val = (y[train_idx], y[test_idx])
            model = model_factory()
            history = model.fit(X_train, y_train, X_val=X_val, y_val=y_val, verbose=0 if not verbose else 1, **fit_kwargs)
            train_eval = model.evaluate(X_train, y_train)
            val_eval = model.evaluate(X_val, y_val)
            results['train_loss'].append(train_eval['loss'])
            results['train_acc'].append(train_eval['accuracy'])
            results['val_loss'].append(val_eval['loss'])
            results['val_acc'].append(val_eval['accuracy'])
            if verbose:
                print(f"  Train Acc: {train_eval['accuracy']:.4f}")
                print(f"  Val Acc:   {val_eval['accuracy']:.4f}")
        for key in results:
            values = np.array(results[key])
            results[f'{key}_mean'] = float(np.mean(values))
            results[f'{key}_std'] = float(np.std(values))
        if verbose:
            print(f"\nOverall: {results['val_acc_mean']:.4f} ± {results['val_acc_std']:.4f}")
        return results

class TimeSeriesCrossValidator:

    def __init__(self, n_splits: int=5, gap: int=0, test_size: Optional[int]=None, max_train_size: Optional[int]=None):
        self.n_splits = n_splits
        self.splitter = TimeSeriesSplit(n_splits=n_splits, gap=gap, test_size=test_size, max_train_size=max_train_size)

    def split(self, X: np.ndarray, y: np.ndarray=None) -> Generator[Tuple[np.ndarray, np.ndarray], None, None]:
        for train_idx, test_idx in self.splitter.split(X):
            yield (train_idx, test_idx)

    def cross_validate(self, model_factory: Callable, X: np.ndarray, y: np.ndarray, verbose: bool=True) -> Dict[str, List[float]]:
        results = {'val_acc': [], 'val_loss': []}
        for fold, (train_idx, test_idx) in enumerate(self.split(X, y)):
            if verbose:
                print(f'\nFold {fold + 1}/{self.n_splits}')
                print(f'  Train: {len(train_idx)} samples (idx {train_idx[0]}-{train_idx[-1]})')
                print(f'  Test:  {len(test_idx)} samples (idx {test_idx[0]}-{test_idx[-1]})')
            model = model_factory()
            model.fit(X[train_idx], y[train_idx], verbose=0)
            val_eval = model.evaluate(X[test_idx], y[test_idx])
            results['val_acc'].append(val_eval['accuracy'])
            results['val_loss'].append(val_eval['loss'])
        results['val_acc_mean'] = float(np.mean(results['val_acc']))
        results['val_acc_std'] = float(np.std(results['val_acc']))
        return results

class OperatingConditionValidator:

    def __init__(self, use_leave_one_out: bool=False):
        self.use_leave_one_out = use_leave_one_out

    def split(self, X: np.ndarray, y: np.ndarray, groups: np.ndarray) -> Generator[Tuple[np.ndarray, np.ndarray], None, None]:
        if self.use_leave_one_out:
            splitter = LeaveOneGroupOut()
        else:
            n_groups = len(np.unique(groups))
            splitter = GroupKFold(n_splits=min(5, n_groups))
        for train_idx, test_idx in splitter.split(X, y, groups):
            yield (train_idx, test_idx)

    def cross_validate(self, model_factory: Callable, X: np.ndarray, y: np.ndarray, groups: np.ndarray, verbose: bool=True) -> Dict[str, Any]:
        unique_groups = np.unique(groups)
        results = {'per_group': {}, 'val_acc': []}
        for fold, (train_idx, test_idx) in enumerate(self.split(X, y, groups)):
            test_groups = np.unique(groups[test_idx])
            group_name = str(test_groups[0]) if len(test_groups) == 1 else str(test_groups.tolist())
            if verbose:
                print(f'\nFold {fold + 1}: Testing on group(s) {group_name}')
            model = model_factory()
            model.fit(X[train_idx], y[train_idx], verbose=0)
            val_eval = model.evaluate(X[test_idx], y[test_idx])
            results['per_group'][group_name] = val_eval['accuracy']
            results['val_acc'].append(val_eval['accuracy'])
            if verbose:
                print(f"  Accuracy: {val_eval['accuracy']:.4f}")
        results['val_acc_mean'] = float(np.mean(results['val_acc']))
        results['val_acc_std'] = float(np.std(results['val_acc']))
        return results

def nested_cross_validation(model_factory: Callable, param_grid: Dict[str, List], X: np.ndarray, y: np.ndarray, outer_splits: int=5, inner_splits: int=3, scoring: str='accuracy', verbose: bool=True) -> Dict[str, Any]:
    outer_cv = StratifiedKFold(n_splits=outer_splits, shuffle=True, random_state=42)
    inner_cv = StratifiedKFold(n_splits=inner_splits, shuffle=True, random_state=42)
    outer_scores = []
    best_params_per_fold = []
    from itertools import product
    param_names = list(param_grid.keys())
    param_values = list(param_grid.values())
    param_combinations = list(product(*param_values))
    for outer_fold, (train_idx, test_idx) in enumerate(outer_cv.split(X, y)):
        if verbose:
            print(f'\nOuter Fold {outer_fold + 1}/{outer_splits}')
        X_train_outer, X_test = (X[train_idx], X[test_idx])
        y_train_outer, y_test = (y[train_idx], y[test_idx])
        best_inner_score = -np.inf
        best_params = None
        for params in param_combinations:
            param_dict = dict(zip(param_names, params))
            inner_scores = []
            for train_inner_idx, val_inner_idx in inner_cv.split(X_train_outer, y_train_outer):
                X_train_inner = X_train_outer[train_inner_idx]
                X_val_inner = X_train_outer[val_inner_idx]
                y_train_inner = y_train_outer[train_inner_idx]
                y_val_inner = y_train_outer[val_inner_idx]
                model = model_factory(**param_dict)
                model.fit(X_train_inner, y_train_inner, verbose=0)
                eval_result = model.evaluate(X_val_inner, y_val_inner)
                inner_scores.append(eval_result[scoring])
            mean_inner = np.mean(inner_scores)
            if mean_inner > best_inner_score:
                best_inner_score = mean_inner
                best_params = param_dict
        final_model = model_factory(**best_params)
        final_model.fit(X_train_outer, y_train_outer, verbose=0)
        outer_eval = final_model.evaluate(X_test, y_test)
        outer_scores.append(outer_eval[scoring])
        best_params_per_fold.append(best_params)
        if verbose:
            print(f'  Best params: {best_params}')
            print(f'  Test score:  {outer_eval[scoring]:.4f}')
    return {'scores': outer_scores, 'mean_score': float(np.mean(outer_scores)), 'std_score': float(np.std(outer_scores)), 'best_params_per_fold': best_params_per_fold}