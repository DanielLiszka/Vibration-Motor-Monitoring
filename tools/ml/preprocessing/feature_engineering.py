import numpy as np
from scipy import signal, stats
from scipy.fft import rfft, rfftfreq
from typing import Dict, List, Optional, Tuple, Union
import warnings

def extract_time_domain_features(data: np.ndarray) -> Dict[str, float]:
    if len(data) == 0:
        return {}
    features = {}
    features['mean'] = float(np.mean(data))
    features['std'] = float(np.std(data))
    features['variance'] = float(np.var(data))
    features['rms'] = float(np.sqrt(np.mean(data ** 2)))
    features['peak'] = float(np.max(np.abs(data)))
    features['peak_positive'] = float(np.max(data))
    features['peak_negative'] = float(np.min(data))
    features['peak_to_peak'] = features['peak_positive'] - features['peak_negative']
    if features['rms'] > 1e-10:
        features['crest_factor'] = features['peak'] / features['rms']
    else:
        features['crest_factor'] = 0.0
    mean_abs = np.mean(np.abs(data))
    if mean_abs > 1e-10:
        features['shape_factor'] = features['rms'] / mean_abs
        features['impulse_factor'] = features['peak'] / mean_abs
    else:
        features['shape_factor'] = 0.0
        features['impulse_factor'] = 0.0
    mean_sqrt = np.mean(np.sqrt(np.abs(data)))
    if mean_sqrt > 1e-10:
        features['clearance_factor'] = features['peak'] / mean_sqrt ** 2
    else:
        features['clearance_factor'] = 0.0
    features['skewness'] = float(stats.skew(data))
    features['kurtosis'] = float(stats.kurtosis(data))
    zero_crossings = np.sum(np.diff(np.signbit(data).astype(int)) != 0)
    features['zero_crossing_rate'] = zero_crossings / len(data)
    features['energy'] = float(np.sum(data ** 2))
    return features

def extract_frequency_domain_features(data: np.ndarray, sample_rate: float=1000.0, nperseg: int=256) -> Dict[str, float]:
    if len(data) < nperseg:
        nperseg = len(data)
    features = {}
    n = len(data)
    fft_vals = rfft(data)
    fft_freqs = rfftfreq(n, 1.0 / sample_rate)
    magnitude = np.abs(fft_vals) / n
    dominant_idx = np.argmax(magnitude[1:]) + 1
    features['dominant_frequency'] = float(fft_freqs[dominant_idx])
    features['dominant_magnitude'] = float(magnitude[dominant_idx])
    if np.sum(magnitude) > 1e-10:
        features['spectral_centroid'] = float(np.sum(fft_freqs * magnitude) / np.sum(magnitude))
    else:
        features['spectral_centroid'] = 0.0
    if features['spectral_centroid'] > 0 and np.sum(magnitude) > 1e-10:
        features['spectral_bandwidth'] = float(np.sqrt(np.sum(magnitude * (fft_freqs - features['spectral_centroid']) ** 2) / np.sum(magnitude)))
    else:
        features['spectral_bandwidth'] = 0.0
    cumsum = np.cumsum(magnitude ** 2)
    rolloff_threshold = 0.85 * cumsum[-1]
    rolloff_idx = np.searchsorted(cumsum, rolloff_threshold)
    features['spectral_rolloff'] = float(fft_freqs[min(rolloff_idx, len(fft_freqs) - 1)])
    magnitude_positive = magnitude[magnitude > 1e-10]
    if len(magnitude_positive) > 0:
        geometric_mean = np.exp(np.mean(np.log(magnitude_positive)))
        arithmetic_mean = np.mean(magnitude_positive)
        if arithmetic_mean > 1e-10:
            features['spectral_flatness'] = float(geometric_mean / arithmetic_mean)
        else:
            features['spectral_flatness'] = 0.0
    else:
        features['spectral_flatness'] = 0.0
    psd = magnitude ** 2
    psd_norm = psd / (np.sum(psd) + 1e-10)
    psd_norm = psd_norm[psd_norm > 1e-10]
    features['spectral_entropy'] = float(-np.sum(psd_norm * np.log2(psd_norm)))
    freqs, psd_welch = signal.welch(data, sample_rate, nperseg=nperseg)
    low_mask = freqs < 50
    mid_mask = (freqs >= 50) & (freqs < 200)
    high_mask = (freqs >= 200) & (freqs < 500)
    features['power_low'] = float(np.sum(psd_welch[low_mask]))
    features['power_mid'] = float(np.sum(psd_welch[mid_mask]))
    features['power_high'] = float(np.sum(psd_welch[high_mask]))
    total_power = features['power_low'] + features['power_mid'] + features['power_high']
    if total_power > 1e-10:
        features['power_ratio_low'] = features['power_low'] / total_power
        features['power_ratio_mid'] = features['power_mid'] / total_power
        features['power_ratio_high'] = features['power_high'] / total_power
    else:
        features['power_ratio_low'] = 0.0
        features['power_ratio_mid'] = 0.0
        features['power_ratio_high'] = 0.0
    return features

def extract_statistical_features(data: np.ndarray) -> Dict[str, float]:
    features = {}
    percentiles = [5, 10, 25, 50, 75, 90, 95]
    for p in percentiles:
        features[f'percentile_{p}'] = float(np.percentile(data, p))
    features['iqr'] = features['percentile_75'] - features['percentile_25']
    features['mad'] = float(np.median(np.abs(data - np.median(data))))
    mean_val = np.mean(data)
    if abs(mean_val) > 1e-10:
        features['cv'] = float(np.std(data) / abs(mean_val))
    else:
        features['cv'] = 0.0
    features['range'] = float(np.max(data) - np.min(data))
    hist, _ = np.histogram(data, bins=50, density=True)
    hist = hist[hist > 0]
    features['histogram_entropy'] = float(-np.sum(hist * np.log2(hist + 1e-10)))
    return features

def compute_spectral_features(data: np.ndarray, sample_rate: float=1000.0, motor_frequency: float=60.0) -> Dict[str, float]:
    features = {}
    n = len(data)
    fft_vals = rfft(data)
    fft_freqs = rfftfreq(n, 1.0 / sample_rate)
    magnitude = np.abs(fft_vals) / n
    freq_resolution = sample_rate / n
    harmonics = []
    for h in range(1, 6):
        target_freq = h * motor_frequency
        idx = int(target_freq / freq_resolution)
        if idx < len(magnitude):
            window_start = max(0, idx - 2)
            window_end = min(len(magnitude), idx + 3)
            harmonic_mag = np.max(magnitude[window_start:window_end])
            harmonics.append(harmonic_mag)
            features[f'harmonic_{h}_mag'] = float(harmonic_mag)
        else:
            features[f'harmonic_{h}_mag'] = 0.0
    if len(harmonics) >= 2 and harmonics[0] > 1e-10:
        features['harmonic_ratio_2_1'] = harmonics[1] / harmonics[0]
        if len(harmonics) >= 3:
            features['harmonic_ratio_3_1'] = harmonics[2] / harmonics[0]
    else:
        features['harmonic_ratio_2_1'] = 0.0
        features['harmonic_ratio_3_1'] = 0.0
    if len(harmonics) > 1 and harmonics[0] > 1e-10:
        thd = np.sqrt(np.sum(np.array(harmonics[1:]) ** 2)) / harmonics[0]
        features['thd'] = float(thd)
    else:
        features['thd'] = 0.0
    sub_freq = motor_frequency / 2
    sub_idx = int(sub_freq / freq_resolution)
    if sub_idx < len(magnitude):
        features['sub_harmonic_mag'] = float(magnitude[sub_idx])
    else:
        features['sub_harmonic_mag'] = 0.0
    return features

def compute_envelope_spectrum(data: np.ndarray, sample_rate: float=1000.0, low_cutoff: float=500.0, high_cutoff: float=2000.0) -> Tuple[np.ndarray, np.ndarray]:
    nyquist = sample_rate / 2
    low_cutoff = min(low_cutoff, nyquist * 0.9)
    high_cutoff = min(high_cutoff, nyquist * 0.95)
    if low_cutoff >= high_cutoff:
        low_cutoff = high_cutoff * 0.5
    b, a = signal.butter(4, [low_cutoff / nyquist, high_cutoff / nyquist], btype='band')
    filtered = signal.filtfilt(b, a, data)
    analytic_signal = signal.hilbert(filtered)
    envelope = np.abs(analytic_signal)
    envelope = envelope - np.mean(envelope)
    n = len(envelope)
    fft_vals = rfft(envelope)
    fft_freqs = rfftfreq(n, 1.0 / sample_rate)
    envelope_spectrum = np.abs(fft_vals) / n
    return (fft_freqs, envelope_spectrum)

def extract_bearing_features(data: np.ndarray, sample_rate: float=1000.0, shaft_freq: float=30.0, n_balls: int=9, ball_diameter: float=7.94, pitch_diameter: float=38.5, contact_angle: float=0.0) -> Dict[str, float]:
    features = {}
    cos_angle = np.cos(contact_angle)
    bd_pd = ball_diameter / pitch_diameter
    bpfo = n_balls / 2 * shaft_freq * (1 - bd_pd * cos_angle)
    bpfi = n_balls / 2 * shaft_freq * (1 + bd_pd * cos_angle)
    bsf = pitch_diameter / (2 * ball_diameter) * shaft_freq * (1 - (bd_pd * cos_angle) ** 2)
    ftf = shaft_freq / 2 * (1 - bd_pd * cos_angle)
    features['bpfo'] = float(bpfo)
    features['bpfi'] = float(bpfi)
    features['bsf'] = float(bsf)
    features['ftf'] = float(ftf)
    freqs, env_spectrum = compute_envelope_spectrum(data, sample_rate)
    freq_resolution = sample_rate / len(data)
    for name, freq in [('bpfo', bpfo), ('bpfi', bpfi), ('bsf', bsf), ('ftf', ftf)]:
        idx = int(freq / freq_resolution)
        if idx < len(env_spectrum):
            window_start = max(0, idx - 3)
            window_end = min(len(env_spectrum), idx + 4)
            features[f'{name}_magnitude'] = float(np.max(env_spectrum[window_start:window_end]))
        else:
            features[f'{name}_magnitude'] = 0.0
    return features

class FeatureEngineer:

    def __init__(self, sample_rate: float=1000.0, motor_frequency: float=60.0, nperseg: int=256):
        self.sample_rate = sample_rate
        self.motor_frequency = motor_frequency
        self.nperseg = nperseg
        self.feature_names: List[str] = []
        self._build_feature_names()

    def _build_feature_names(self):
        self.feature_names.extend(['mean', 'std', 'variance', 'rms', 'peak', 'peak_positive', 'peak_negative', 'peak_to_peak', 'crest_factor', 'shape_factor', 'impulse_factor', 'clearance_factor', 'skewness', 'kurtosis', 'zero_crossing_rate', 'energy'])
        self.feature_names.extend(['dominant_frequency', 'dominant_magnitude', 'spectral_centroid', 'spectral_bandwidth', 'spectral_rolloff', 'spectral_flatness', 'spectral_entropy', 'power_low', 'power_mid', 'power_high', 'power_ratio_low', 'power_ratio_mid', 'power_ratio_high'])
        self.feature_names.extend(['percentile_5', 'percentile_10', 'percentile_25', 'percentile_50', 'percentile_75', 'percentile_90', 'percentile_95', 'iqr', 'mad', 'cv', 'range', 'histogram_entropy'])

    def extract_features(self, data: np.ndarray) -> np.ndarray:
        features = {}
        features.update(extract_time_domain_features(data))
        features.update(extract_frequency_domain_features(data, self.sample_rate, self.nperseg))
        features.update(extract_statistical_features(data))
        feature_vector = np.array([features.get(name, 0.0) for name in self.feature_names], dtype=np.float32)
        return feature_vector

    def extract_features_dict(self, data: np.ndarray) -> Dict[str, float]:
        features = {}
        features.update(extract_time_domain_features(data))
        features.update(extract_frequency_domain_features(data, self.sample_rate, self.nperseg))
        features.update(extract_statistical_features(data))
        return features

    def extract_extended_features(self, data: np.ndarray, include_bearing: bool=True, bearing_params: Optional[Dict]=None) -> np.ndarray:
        features = self.extract_features_dict(data)
        features.update(compute_spectral_features(data, self.sample_rate, self.motor_frequency))
        if include_bearing:
            if bearing_params is None:
                bearing_params = {'shaft_freq': self.motor_frequency, 'n_balls': 9, 'ball_diameter': 7.94, 'pitch_diameter': 38.5, 'contact_angle': 0.0}
            features.update(extract_bearing_features(data, self.sample_rate, **bearing_params))
        return np.array(list(features.values()), dtype=np.float32)

    def extract_batch(self, data_batch: np.ndarray) -> np.ndarray:
        n_samples = data_batch.shape[0]
        features = np.zeros((n_samples, len(self.feature_names)), dtype=np.float32)
        for i in range(n_samples):
            features[i] = self.extract_features(data_batch[i])
        return features

    def get_feature_names(self) -> List[str]:
        return self.feature_names.copy()

    def get_num_features(self) -> int:
        return len(self.feature_names)