import numpy as np
import csv
import argparse
import os
SAMPLE_RATE = 1000
WINDOW_SIZE = 256

def generate_normal_signal(duration_ms, amplitude=0.2):
    num_samples = int(duration_ms * SAMPLE_RATE / 1000)
    t = np.linspace(0, duration_ms / 1000, num_samples)
    signal = amplitude * np.random.randn(num_samples)
    return signal

def generate_imbalance_signal(duration_ms, rpm=1800, amplitude=0.8):
    num_samples = int(duration_ms * SAMPLE_RATE / 1000)
    t = np.linspace(0, duration_ms / 1000, num_samples)
    freq = rpm / 60
    signal = amplitude * np.sin(2 * np.pi * freq * t)
    signal += 0.1 * np.random.randn(num_samples)
    return signal

def generate_misalignment_signal(duration_ms, rpm=1800, amplitude=0.6):
    num_samples = int(duration_ms * SAMPLE_RATE / 1000)
    t = np.linspace(0, duration_ms / 1000, num_samples)
    freq = rpm / 60
    signal = amplitude * np.sin(2 * np.pi * freq * t)
    signal += 0.5 * amplitude * np.sin(2 * np.pi * 2 * freq * t)
    signal += 0.3 * amplitude * np.sin(2 * np.pi * 3 * freq * t)
    signal += 0.1 * np.random.randn(num_samples)
    return signal

def generate_bearing_fault_signal(duration_ms, bpfo=120, amplitude=1.0):
    num_samples = int(duration_ms * SAMPLE_RATE / 1000)
    t = np.linspace(0, duration_ms / 1000, num_samples)
    carrier_freq = 2000
    signal = np.zeros(num_samples)
    impulse_period = int(SAMPLE_RATE / bpfo)
    for i in range(0, num_samples, impulse_period):
        decay = np.exp(-np.arange(50) / 10)
        burst = amplitude * decay * np.sin(2 * np.pi * carrier_freq * np.arange(50) / SAMPLE_RATE)
        end_idx = min(i + len(burst), num_samples)
        signal[i:end_idx] += burst[:end_idx - i]
    signal += 0.15 * np.random.randn(num_samples)
    return signal

def generate_looseness_signal(duration_ms, rpm=1800, amplitude=0.7):
    num_samples = int(duration_ms * SAMPLE_RATE / 1000)
    t = np.linspace(0, duration_ms / 1000, num_samples)
    freq = rpm / 60
    signal = np.zeros(num_samples)
    for harmonic in range(1, 10):
        signal += amplitude / harmonic * np.sin(2 * np.pi * harmonic * freq * t + np.random.rand() * 2 * np.pi)
    signal += 0.2 * np.random.randn(num_samples)
    return signal

def compute_features(signal):
    features = {}
    features['rms'] = np.sqrt(np.mean(signal ** 2))
    features['peak_to_peak'] = np.max(signal) - np.min(signal)
    mean = np.mean(signal)
    std = np.std(signal)
    features['variance'] = std ** 2
    if std > 0.0001:
        normalized = (signal - mean) / std
        features['skewness'] = np.mean(normalized ** 3)
        features['kurtosis'] = np.mean(normalized ** 4) - 3
    else:
        features['skewness'] = 0
        features['kurtosis'] = 0
    peak = max(abs(np.max(signal)), abs(np.min(signal)))
    features['crest_factor'] = peak / features['rms'] if features['rms'] > 0.0001 else 0
    spectrum = np.abs(np.fft.rfft(signal))
    freqs = np.fft.rfftfreq(len(signal), 1 / SAMPLE_RATE)
    total_power = np.sum(spectrum ** 2)
    if total_power > 0.0001:
        features['spectral_centroid'] = np.sum(freqs * spectrum ** 2) / total_power
        features['spectral_spread'] = np.sqrt(np.sum((freqs - features['spectral_centroid']) ** 2 * spectrum ** 2) / total_power)
    else:
        features['spectral_centroid'] = 0
        features['spectral_spread'] = 0
    mid_idx = len(spectrum) // 2
    low_band = np.sum(spectrum[:mid_idx] ** 2)
    high_band = np.sum(spectrum[mid_idx:] ** 2)
    features['band_power_ratio'] = high_band / low_band if low_band > 0.0001 else 0
    features['dominant_frequency'] = freqs[np.argmax(spectrum[1:]) + 1]
    return features

def main():
    parser = argparse.ArgumentParser(description='Generate test vibration data')
    parser.add_argument('--output', type=str, default='test_data.csv', help='Output CSV file')
    parser.add_argument('--samples-per-class', type=int, default=100, help='Samples per fault class')
    parser.add_argument('--duration', type=int, default=256, help='Signal duration in ms')
    args = parser.parse_args()
    generators = [('Normal', generate_normal_signal), ('Imbalance', generate_imbalance_signal), ('Misalignment', generate_misalignment_signal), ('BearingFault', generate_bearing_fault_signal), ('Looseness', generate_looseness_signal)]
    rows = []
    feature_names = ['rms', 'peak_to_peak', 'kurtosis', 'skewness', 'crest_factor', 'variance', 'spectral_centroid', 'spectral_spread', 'band_power_ratio', 'dominant_frequency']
    for class_idx, (class_name, generator) in enumerate(generators):
        print(f'Generating {args.samples_per_class} samples for class: {class_name}')
        for i in range(args.samples_per_class):
            signal = generator(args.duration)
            features = compute_features(signal)
            row = [class_idx, class_name]
            for name in feature_names:
                row.append(features[name])
            rows.append(row)
    with open(args.output, 'w', newline='') as f:
        writer = csv.writer(f)
        header = ['class_id', 'class_name'] + feature_names
        writer.writerow(header)
        writer.writerows(rows)
    print(f'\nGenerated {len(rows)} samples to {args.output}')
if __name__ == '__main__':
    main()