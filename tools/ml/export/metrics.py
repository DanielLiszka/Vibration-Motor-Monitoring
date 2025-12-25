import numpy as np
from typing import Dict, List, Optional, Tuple, Any
from collections import defaultdict

def accuracy(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    return float(np.mean(y_true == y_pred))

def confusion_matrix(y_true: np.ndarray, y_pred: np.ndarray, num_classes: int=None) -> np.ndarray:
    if num_classes is None:
        num_classes = max(y_true.max(), y_pred.max()) + 1
    cm = np.zeros((num_classes, num_classes), dtype=np.int64)
    for t, p in zip(y_true, y_pred):
        cm[int(t), int(p)] += 1
    return cm

def precision_recall_f1(y_true: np.ndarray, y_pred: np.ndarray, average: str='macro') -> Tuple[float, float, float]:
    cm = confusion_matrix(y_true, y_pred)
    num_classes = cm.shape[0]
    precisions = []
    recalls = []
    f1s = []
    supports = []
    for i in range(num_classes):
        tp = cm[i, i]
        fp = cm[:, i].sum() - tp
        fn = cm[i, :].sum() - tp
        precision = tp / (tp + fp) if tp + fp > 0 else 0.0
        recall = tp / (tp + fn) if tp + fn > 0 else 0.0
        f1 = 2 * precision * recall / (precision + recall) if precision + recall > 0 else 0.0
        precisions.append(precision)
        recalls.append(recall)
        f1s.append(f1)
        supports.append(cm[i, :].sum())
    if average == 'micro':
        tp_total = np.trace(cm)
        total = cm.sum()
        precision = tp_total / total if total > 0 else 0.0
        recall = precision
        f1 = precision
    elif average == 'weighted':
        total = sum(supports)
        if total > 0:
            precision = sum((p * s for p, s in zip(precisions, supports))) / total
            recall = sum((r * s for r, s in zip(recalls, supports))) / total
            f1 = sum((f * s for f, s in zip(f1s, supports))) / total
        else:
            precision = recall = f1 = 0.0
    else:
        precision = np.mean(precisions)
        recall = np.mean(recalls)
        f1 = np.mean(f1s)
    return (float(precision), float(recall), float(f1))

def classification_report(y_true: np.ndarray, y_pred: np.ndarray, class_names: List[str]=None) -> Dict[str, Any]:
    cm = confusion_matrix(y_true, y_pred)
    num_classes = cm.shape[0]
    if class_names is None:
        class_names = [f'Class {i}' for i in range(num_classes)]
    report = {'classes': {}, 'overall': {}, 'confusion_matrix': cm.tolist()}
    for i in range(num_classes):
        tp = cm[i, i]
        fp = cm[:, i].sum() - tp
        fn = cm[i, :].sum() - tp
        tn = cm.sum() - tp - fp - fn
        precision = tp / (tp + fp) if tp + fp > 0 else 0.0
        recall = tp / (tp + fn) if tp + fn > 0 else 0.0
        f1 = 2 * precision * recall / (precision + recall) if precision + recall > 0 else 0.0
        specificity = tn / (tn + fp) if tn + fp > 0 else 0.0
        report['classes'][class_names[i]] = {'precision': float(precision), 'recall': float(recall), 'f1_score': float(f1), 'specificity': float(specificity), 'support': int(cm[i, :].sum()), 'true_positives': int(tp), 'false_positives': int(fp), 'false_negatives': int(fn), 'true_negatives': int(tn)}
    report['overall']['accuracy'] = accuracy(y_true, y_pred)
    macro_p, macro_r, macro_f1 = precision_recall_f1(y_true, y_pred, 'macro')
    report['overall']['macro_precision'] = macro_p
    report['overall']['macro_recall'] = macro_r
    report['overall']['macro_f1'] = macro_f1
    weighted_p, weighted_r, weighted_f1 = precision_recall_f1(y_true, y_pred, 'weighted')
    report['overall']['weighted_precision'] = weighted_p
    report['overall']['weighted_recall'] = weighted_r
    report['overall']['weighted_f1'] = weighted_f1
    report['overall']['total_samples'] = len(y_true)
    return report

def print_classification_report(report: Dict[str, Any]):
    print('\n' + '=' * 70)
    print('CLASSIFICATION REPORT')
    print('=' * 70)
    print(f"\n{'Class':<15} {'Precision':>10} {'Recall':>10} {'F1':>10} {'Support':>10}")
    print('-' * 55)
    for class_name, metrics in report['classes'].items():
        print(f"{class_name:<15} {metrics['precision']:>10.4f} {metrics['recall']:>10.4f} {metrics['f1_score']:>10.4f} {metrics['support']:>10}")
    print('-' * 55)
    overall = report['overall']
    print(f"\n{'Accuracy:':<20} {overall['accuracy']:.4f}")
    print(f"{'Macro F1:':<20} {overall['macro_f1']:.4f}")
    print(f"{'Weighted F1:':<20} {overall['weighted_f1']:.4f}")
    print(f"{'Total samples:':<20} {overall['total_samples']}")
    print('\nConfusion Matrix:')
    cm = np.array(report['confusion_matrix'])
    print(cm)

def rul_metrics(y_true: np.ndarray, y_pred: np.ndarray) -> Dict[str, float]:
    errors = y_pred - y_true
    metrics = {'mae': float(np.mean(np.abs(errors))), 'mse': float(np.mean(errors ** 2)), 'rmse': float(np.sqrt(np.mean(errors ** 2))), 'mape': float(np.mean(np.abs(errors / (y_true + 1e-10))) * 100), 'r2': float(1 - np.sum(errors ** 2) / np.sum((y_true - y_true.mean()) ** 2)), 'mean_error': float(np.mean(errors)), 'std_error': float(np.std(errors))}
    scores = []
    for e in errors:
        if e < 0:
            scores.append(np.exp(-e / 13) - 1)
        else:
            scores.append(np.exp(e / 10) - 1)
    metrics['phm_score'] = float(np.sum(scores))
    metrics['phm_avg'] = float(np.mean(scores))
    return metrics

def compute_pr_curve(y_true: np.ndarray, y_scores: np.ndarray, positive_class: int=1) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    y_binary = (y_true == positive_class).astype(int)
    if len(y_scores.shape) > 1:
        scores = y_scores[:, positive_class]
    else:
        scores = y_scores
    sorted_indices = np.argsort(scores)[::-1]
    sorted_scores = scores[sorted_indices]
    sorted_labels = y_binary[sorted_indices]
    tp_cumsum = np.cumsum(sorted_labels)
    fp_cumsum = np.cumsum(1 - sorted_labels)
    precisions = tp_cumsum / (tp_cumsum + fp_cumsum)
    recalls = tp_cumsum / sorted_labels.sum()
    thresholds = sorted_scores
    return (precisions, recalls, thresholds)

def compute_roc_curve(y_true: np.ndarray, y_scores: np.ndarray, positive_class: int=1) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    y_binary = (y_true == positive_class).astype(int)
    if len(y_scores.shape) > 1:
        scores = y_scores[:, positive_class]
    else:
        scores = y_scores
    sorted_indices = np.argsort(scores)[::-1]
    sorted_scores = scores[sorted_indices]
    sorted_labels = y_binary[sorted_indices]
    n_pos = sorted_labels.sum()
    n_neg = len(sorted_labels) - n_pos
    tp_cumsum = np.cumsum(sorted_labels)
    fp_cumsum = np.cumsum(1 - sorted_labels)
    tpr = tp_cumsum / n_pos if n_pos > 0 else np.zeros_like(tp_cumsum)
    fpr = fp_cumsum / n_neg if n_neg > 0 else np.zeros_like(fp_cumsum)
    tpr = np.concatenate([[0], tpr])
    fpr = np.concatenate([[0], fpr])
    thresholds = np.concatenate([[sorted_scores[0] + 1], sorted_scores])
    return (fpr, tpr, thresholds)

def compute_auc(x: np.ndarray, y: np.ndarray) -> float:
    return float(np.trapz(y, x))

def multiclass_roc_auc(y_true: np.ndarray, y_scores: np.ndarray, average: str='macro') -> float:
    classes = np.unique(y_true)
    aucs = []
    supports = []
    for c in classes:
        fpr, tpr, _ = compute_roc_curve(y_true, y_scores, c)
        auc = compute_auc(fpr, tpr)
        aucs.append(auc)
        supports.append(np.sum(y_true == c))
    if average == 'weighted':
        total = sum(supports)
        return sum((a * s / total for a, s in zip(aucs, supports)))
    else:
        return float(np.mean(aucs))

class MetricsLogger:

    def __init__(self):
        self.history = defaultdict(list)
        self.best = {}

    def log(self, epoch: int, metrics: Dict[str, float]):
        self.history['epoch'].append(epoch)
        for key, value in metrics.items():
            self.history[key].append(value)
            if key not in self.best or value > self.best[key]['value']:
                self.best[key] = {'value': value, 'epoch': epoch}

    def get_best(self, metric: str) -> Dict:
        return self.best.get(metric, None)

    def get_history(self, metric: str) -> List[float]:
        return self.history.get(metric, [])

    def summary(self) -> Dict[str, Any]:
        return {'best': dict(self.best), 'final': {k: v[-1] if v else None for k, v in self.history.items()}, 'n_epochs': len(self.history.get('epoch', []))}

    def to_dict(self) -> Dict:
        return dict(self.history)

def evaluate_model(model, X_test: np.ndarray, y_test: np.ndarray, class_names: List[str]=None) -> Dict[str, Any]:
    y_pred = model.predict(X_test)
    y_proba = None
    if hasattr(model, 'predict_proba'):
        y_proba = model.predict_proba(X_test)
    report = classification_report(y_test, y_pred, class_names)
    if y_proba is not None:
        report['overall']['roc_auc'] = multiclass_roc_auc(y_test, y_proba)
    return report