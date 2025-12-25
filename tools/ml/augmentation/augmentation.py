import numpy as np
from scipy import signal, interpolate
from typing import Optional, Tuple, List, Callable, Union
import warnings

def add_gaussian_noise(data: np.ndarray, snr_db: float=20.0, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    signal_power = np.mean(data ** 2)
    noise_power = signal_power / 10 ** (snr_db / 10)
    noise = rng.normal(0, np.sqrt(noise_power), data.shape)
    return data + noise

def add_colored_noise(data: np.ndarray, snr_db: float=20.0, color: str='pink', random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    n = len(data)
    white_noise = rng.normal(0, 1, n)
    fft_noise = np.fft.rfft(white_noise)
    freqs = np.fft.rfftfreq(n)
    freqs[0] = 1e-10
    if color == 'pink':
        fft_noise = fft_noise / np.sqrt(freqs)
    elif color == 'brown':
        fft_noise = fft_noise / freqs
    elif color == 'blue':
        fft_noise = fft_noise * np.sqrt(freqs)
    colored_noise = np.fft.irfft(fft_noise, n)
    signal_power = np.mean(data ** 2)
    noise_power = np.mean(colored_noise ** 2)
    target_noise_power = signal_power / 10 ** (snr_db / 10)
    scale = np.sqrt(target_noise_power / (noise_power + 1e-10))
    return data + colored_noise * scale

def add_sensor_drift(data: np.ndarray, drift_rate: float=0.001, drift_type: str='linear', random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    n = len(data)
    t = np.arange(n)
    amplitude = np.std(data) * drift_rate * n
    if drift_type == 'linear':
        drift = amplitude * t / n
    elif drift_type == 'exponential':
        drift = amplitude * (np.exp(t / n) - 1) / (np.e - 1)
    elif drift_type == 'sinusoidal':
        period = rng.uniform(0.5, 2.0) * n
        drift = amplitude * np.sin(2 * np.pi * t / period)
    else:
        drift = 0
    if rng.random() < 0.5:
        drift = -drift
    return data + drift

def time_stretch(data: np.ndarray, rate: float=1.0, random_state: Optional[int]=None) -> np.ndarray:
    if abs(rate - 1.0) < 1e-06:
        return data.copy()
    n = len(data)
    t_original = np.arange(n)
    t_stretched = np.arange(n) * rate
    f = interpolate.interp1d(t_original, data, kind='linear', fill_value='extrapolate')
    stretched = f(t_stretched % n)
    return stretched

def time_shift(data: np.ndarray, shift_fraction: Optional[float]=None, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    if shift_fraction is None:
        shift_fraction = rng.uniform(-0.5, 0.5)
    n = len(data)
    shift = int(shift_fraction * n)
    return np.roll(data, shift)

def amplitude_scale(data: np.ndarray, scale_range: Tuple[float, float]=(0.8, 1.2), random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    scale = rng.uniform(scale_range[0], scale_range[1])
    return data * scale

def amplitude_warp(data: np.ndarray, n_knots: int=4, magnitude: float=0.2, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    n = len(data)
    knot_positions = np.linspace(0, n - 1, n_knots)
    knot_values = 1.0 + rng.uniform(-magnitude, magnitude, n_knots)
    knot_values[0] = 1.0
    knot_values[-1] = 1.0
    f = interpolate.interp1d(knot_positions, knot_values, kind='cubic')
    warp_curve = f(np.arange(n))
    return data * warp_curve

def frequency_mask(data: np.ndarray, mask_fraction: float=0.1, num_masks: int=1, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    n = len(data)
    fft_data = np.fft.rfft(data)
    n_freq = len(fft_data)
    mask_width = int(n_freq * mask_fraction)
    for _ in range(num_masks):
        start = rng.integers(0, n_freq - mask_width)
        fft_data[start:start + mask_width] = 0
    return np.fft.irfft(fft_data, n)

def frequency_shift(data: np.ndarray, shift_hz: float=0.0, sample_rate: float=1000.0, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    if shift_hz == 0.0:
        shift_hz = rng.uniform(-10, 10)
    n = len(data)
    t = np.arange(n) / sample_rate
    shifted = data * np.cos(2 * np.pi * shift_hz * t)
    return shifted

def simulate_resonance(data: np.ndarray, resonance_freq: float=200.0, q_factor: float=10.0, gain: float=2.0, sample_rate: float=1000.0) -> np.ndarray:
    nyquist = sample_rate / 2
    if resonance_freq >= nyquist:
        return data.copy()
    bandwidth = resonance_freq / q_factor
    low = max((resonance_freq - bandwidth / 2) / nyquist, 0.01)
    high = min((resonance_freq + bandwidth / 2) / nyquist, 0.99)
    if low >= high:
        return data.copy()
    b, a = signal.butter(2, [low, high], btype='band')
    resonance_component = signal.filtfilt(b, a, data)
    return data + resonance_component * (gain - 1)

def add_harmonics(data: np.ndarray, fundamental_freq: float=60.0, harmonic_gains: List[float]=[0.0, 0.1, 0.05, 0.02], sample_rate: float=1000.0, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    n = len(data)
    t = np.arange(n) / sample_rate
    augmented = data.copy()
    amplitude = np.std(data)
    for i, gain in enumerate(harmonic_gains):
        if gain > 0:
            freq = (i + 1) * fundamental_freq
            phase = rng.uniform(0, 2 * np.pi)
            harmonic = amplitude * gain * np.sin(2 * np.pi * freq * t + phase)
            augmented = augmented + harmonic
    return augmented

def add_impulse_noise(data: np.ndarray, impulse_rate: float=0.001, impulse_amplitude: float=3.0, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    n = len(data)
    impulses = rng.random(n) < impulse_rate
    amplitude = np.std(data) * impulse_amplitude
    impulse_values = rng.choice([-1, 1], n) * amplitude * impulses
    return data + impulse_values

def mixup(data1: np.ndarray, data2: np.ndarray, alpha: float=0.2, random_state: Optional[int]=None) -> Tuple[np.ndarray, float]:
    rng = np.random.default_rng(random_state)
    lam = rng.beta(alpha, alpha)
    min_len = min(len(data1), len(data2))
    mixed = lam * data1[:min_len] + (1 - lam) * data2[:min_len]
    return (mixed, lam)

def cutout(data: np.ndarray, mask_fraction: float=0.1, num_masks: int=1, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    augmented = data.copy()
    n = len(data)
    mask_length = int(n * mask_fraction)
    for _ in range(num_masks):
        start = rng.integers(0, n - mask_length)
        augmented[start:start + mask_length] = 0
    return augmented

def jitter(data: np.ndarray, sigma: float=0.03, random_state: Optional[int]=None) -> np.ndarray:
    rng = np.random.default_rng(random_state)
    noise = rng.normal(0, sigma * np.std(data), data.shape)
    return data + noise

class VibrationAugmenter:

    def __init__(self, sample_rate: float=1000.0, random_state: Optional[int]=None):
        self.sample_rate = sample_rate
        self.rng = np.random.default_rng(random_state)
        self.augmentations = {'gaussian_noise': {'enabled': True, 'prob': 0.5, 'params': {'snr_db': (15, 30)}}, 'amplitude_scale': {'enabled': True, 'prob': 0.5, 'params': {'scale_range': (0.8, 1.2)}}, 'time_shift': {'enabled': True, 'prob': 0.3, 'params': {'shift_fraction': (-0.2, 0.2)}}, 'time_stretch': {'enabled': True, 'prob': 0.3, 'params': {'rate': (0.9, 1.1)}}, 'sensor_drift': {'enabled': True, 'prob': 0.2, 'params': {'drift_rate': (0.0005, 0.002)}}, 'frequency_mask': {'enabled': True, 'prob': 0.2, 'params': {'mask_fraction': 0.1, 'num_masks': 1}}, 'jitter': {'enabled': True, 'prob': 0.3, 'params': {'sigma': (0.01, 0.05)}}}

    def configure(self, augmentation_name: str, **kwargs):
        if augmentation_name in self.augmentations:
            self.augmentations[augmentation_name].update(kwargs)
        else:
            warnings.warn(f'Unknown augmentation: {augmentation_name}')

    def enable_all(self):
        for aug in self.augmentations.values():
            aug['enabled'] = True

    def disable_all(self):
        for aug in self.augmentations.values():
            aug['enabled'] = False

    def set_probability(self, prob: float):
        for aug in self.augmentations.values():
            aug['prob'] = prob

    def augment(self, data: np.ndarray) -> np.ndarray:
        augmented = data.copy()
        for name, config in self.augmentations.items():
            if not config['enabled']:
                continue
            if self.rng.random() > config['prob']:
                continue
            params = config['params']
            if name == 'gaussian_noise':
                snr = self.rng.uniform(*params['snr_db'])
                augmented = add_gaussian_noise(augmented, snr)
            elif name == 'amplitude_scale':
                augmented = amplitude_scale(augmented, params['scale_range'], random_state=self.rng.integers(0, 2 ** 31))
            elif name == 'time_shift':
                shift = self.rng.uniform(*params['shift_fraction'])
                augmented = time_shift(augmented, shift)
            elif name == 'time_stretch':
                rate = self.rng.uniform(*params['rate'])
                augmented = time_stretch(augmented, rate)
            elif name == 'sensor_drift':
                drift = self.rng.uniform(*params['drift_rate'])
                augmented = add_sensor_drift(augmented, drift, random_state=self.rng.integers(0, 2 ** 31))
            elif name == 'frequency_mask':
                augmented = frequency_mask(augmented, params['mask_fraction'], params['num_masks'], random_state=self.rng.integers(0, 2 ** 31))
            elif name == 'jitter':
                sigma = self.rng.uniform(*params['sigma'])
                augmented = jitter(augmented, sigma, random_state=self.rng.integers(0, 2 ** 31))
        return augmented

    def augment_batch(self, data_batch: np.ndarray, n_augmentations: int=1) -> np.ndarray:
        n_samples, signal_length = data_batch.shape
        total_samples = n_samples * n_augmentations
        augmented_batch = np.zeros((total_samples, signal_length), dtype=data_batch.dtype)
        for i in range(n_samples):
            for j in range(n_augmentations):
                idx = i * n_augmentations + j
                augmented_batch[idx] = self.augment(data_batch[i])
        return augmented_batch

    def augment_with_labels(self, data: np.ndarray, labels: np.ndarray, n_augmentations: int=1) -> Tuple[np.ndarray, np.ndarray]:
        augmented_data = self.augment_batch(data, n_augmentations)
        augmented_labels = np.repeat(labels, n_augmentations)
        return (augmented_data, augmented_labels)

    def create_training_pipeline(self, severity: str='medium') -> 'VibrationAugmenter':
        if severity == 'light':
            self.set_probability(0.3)
            self.configure('gaussian_noise', params={'snr_db': (25, 35)})
            self.configure('amplitude_scale', params={'scale_range': (0.9, 1.1)})
        elif severity == 'medium':
            self.set_probability(0.5)
            self.configure('gaussian_noise', params={'snr_db': (15, 30)})
            self.configure('amplitude_scale', params={'scale_range': (0.8, 1.2)})
        elif severity == 'heavy':
            self.set_probability(0.7)
            self.configure('gaussian_noise', params={'snr_db': (10, 25)})
            self.configure('amplitude_scale', params={'scale_range': (0.7, 1.3)})
            self.configure('sensor_drift', prob=0.4)
            self.configure('frequency_mask', prob=0.4)
        return self