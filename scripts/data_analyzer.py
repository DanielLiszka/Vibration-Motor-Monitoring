import json
import sys
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Any
try:
    import pandas as pd
    import matplotlib.pyplot as plt
    import numpy as np
    from scipy import stats
except ImportError:
    print('Error: Required packages not installed')
    print('Install with: pip install pandas matplotlib numpy scipy')
    sys.exit(1)

class VibrationAnalyzer:

    def __init__(self, data_file: str=None):
        self.data_file = data_file
        self.df = None

    def load_json_logs(self, file_path: str):
        data = []
        with open(file_path, 'r') as f:
            for line in f:
                try:
                    entry = json.loads(line.strip())
                    flat_entry = {'timestamp': entry['timestamp'], 'temperature': entry.get('temperature', 0), 'fault_type': entry['fault']['type'], 'fault_severity': entry['fault']['severity'], 'anomaly_score': entry['fault']['anomalyScore']}
                    for key, value in entry['features'].items():
                        flat_entry[key] = value
                    data.append(flat_entry)
                except (json.JSONDecodeError, KeyError) as e:
                    print(f'Warning: Skipping malformed entry: {e}')
        self.df = pd.DataFrame(data)
        self.df['timestamp'] = pd.to_datetime(self.df['timestamp'], unit='ms')
        print(f'[OK] Loaded {len(self.df)} records')

    def load_csv_logs(self, file_path: str):
        self.df = pd.read_csv(file_path)
        self.df['timestamp'] = pd.to_datetime(self.df['timestamp'], unit='ms')
        print(f'[OK] Loaded {len(self.df)} records')

    def generate_summary_stats(self):
        print('\n' + '=' * 60)
        print('SUMMARY STATISTICS')
        print('=' * 60)
        print(f'\nTime Range:')
        print(f"  Start: {self.df['timestamp'].min()}")
        print(f"  End:   {self.df['timestamp'].max()}")
        print(f"  Duration: {self.df['timestamp'].max() - self.df['timestamp'].min()}")
        print(f'\nFault Statistics:')
        fault_counts = self.df['fault_type'].value_counts()
        for fault_type, count in fault_counts.items():
            pct = count / len(self.df) * 100
            print(f'  {fault_type}: {count} ({pct:.1f}%)')
        print(f'\nSeverity Distribution:')
        severity_counts = self.df['fault_severity'].value_counts()
        for severity, count in severity_counts.items():
            pct = count / len(self.df) * 100
            print(f'  {severity}: {count} ({pct:.1f}%)')
        print(f'\nFeature Statistics:')
        features = ['rms', 'kurtosis', 'crestFactor', 'dominantFreq']
        stats_df = self.df[features].describe()
        print(stats_df.to_string())
        print(f'\nAnomaly Score Statistics:')
        print(f"  Mean: {self.df['anomaly_score'].mean():.4f}")
        print(f"  Std: {self.df['anomaly_score'].std():.4f}")
        print(f"  Min: {self.df['anomaly_score'].min():.4f}")
        print(f"  Max: {self.df['anomaly_score'].max():.4f}")
        print(f"  95th percentile: {self.df['anomaly_score'].quantile(0.95):.4f}")

    def plot_time_series(self, output_dir: str='plots'):
        Path(output_dir).mkdir(exist_ok=True)
        print(f'\nGenerating time series plots...')
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
        ax1.plot(self.df['timestamp'], self.df['rms'], label='RMS', color='blue', alpha=0.7)
        ax1.set_ylabel('RMS', fontsize=12)
        ax1.set_title('Vibration RMS Over Time', fontsize=14, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        ax1.legend()
        fault_mask = self.df['fault_type'] != 'NONE'
        if fault_mask.any():
            ax1.scatter(self.df.loc[fault_mask, 'timestamp'], self.df.loc[fault_mask, 'rms'], color='red', s=100, marker='x', label='Fault', zorder=5)
        ax2.plot(self.df['timestamp'], self.df['anomaly_score'], label='Anomaly Score', color='orange', alpha=0.7)
        ax2.axhline(y=2.0, color='yellow', linestyle='--', label='Warning Threshold')
        ax2.axhline(y=3.0, color='red', linestyle='--', label='Critical Threshold')
        ax2.set_xlabel('Time', fontsize=12)
        ax2.set_ylabel('Anomaly Score', fontsize=12)
        ax2.set_title('Anomaly Score Over Time', fontsize=14, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        ax2.legend()
        plt.tight_layout()
        plt.savefig(f'{output_dir}/time_series.png', dpi=300, bbox_inches='tight')
        print(f'  [OK] Saved: {output_dir}/time_series.png')
        plt.close()
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        features_to_plot = [('rms', 'RMS', axes[0, 0]), ('kurtosis', 'Kurtosis', axes[0, 1]), ('crestFactor', 'Crest Factor', axes[1, 0]), ('dominantFreq', 'Dominant Frequency (Hz)', axes[1, 1])]
        for feature, title, ax in features_to_plot:
            ax.plot(self.df['timestamp'], self.df[feature], alpha=0.7)
            ax.set_ylabel(title, fontsize=11)
            ax.set_title(title, fontsize=12, fontweight='bold')
            ax.grid(True, alpha=0.3)
            if fault_mask.any():
                ax.scatter(self.df.loc[fault_mask, 'timestamp'], self.df.loc[fault_mask, feature], color='red', s=50, marker='x', zorder=5)
        plt.tight_layout()
        plt.savefig(f'{output_dir}/features_time_series.png', dpi=300, bbox_inches='tight')
        print(f'  [OK] Saved: {output_dir}/features_time_series.png')
        plt.close()

    def plot_distributions(self, output_dir: str='plots'):
        Path(output_dir).mkdir(exist_ok=True)
        print(f'\nGenerating distribution plots...')
        features = ['rms', 'kurtosis', 'skewness', 'crestFactor', 'variance', 'spectralCentroid', 'dominantFreq']
        fig, axes = plt.subplots(3, 3, figsize=(15, 12))
        axes = axes.flatten()
        for idx, feature in enumerate(features):
            if feature in self.df.columns:
                ax = axes[idx]
                ax.hist(self.df[feature], bins=50, alpha=0.7, color='skyblue', edgecolor='black')
                ax.set_xlabel(feature, fontsize=10)
                ax.set_ylabel('Frequency', fontsize=10)
                ax.set_title(f'Distribution: {feature}', fontsize=11, fontweight='bold')
                ax.grid(True, alpha=0.3)
                mean_val = self.df[feature].mean()
                std_val = self.df[feature].std()
                ax.axvline(mean_val, color='red', linestyle='--', label=f'Mean: {mean_val:.2f}', linewidth=2)
                ax.axvline(mean_val + std_val, color='orange', linestyle=':', label=f'±1σ', linewidth=1.5)
                ax.axvline(mean_val - std_val, color='orange', linestyle=':', linewidth=1.5)
                ax.legend(fontsize=8)
        for idx in range(len(features), len(axes)):
            axes[idx].set_visible(False)
        plt.tight_layout()
        plt.savefig(f'{output_dir}/distributions.png', dpi=300, bbox_inches='tight')
        print(f'  [OK] Saved: {output_dir}/distributions.png')
        plt.close()

    def plot_correlation_matrix(self, output_dir: str='plots'):
        Path(output_dir).mkdir(exist_ok=True)
        print(f'\nGenerating correlation matrix...')
        features = ['rms', 'peakToPeak', 'kurtosis', 'skewness', 'crestFactor', 'variance', 'spectralCentroid', 'spectralSpread', 'dominantFreq', 'anomaly_score']
        corr_matrix = self.df[features].corr()
        fig, ax = plt.subplots(figsize=(12, 10))
        im = ax.imshow(corr_matrix, cmap='coolwarm', aspect='auto', vmin=-1, vmax=1)
        ax.set_xticks(range(len(features)))
        ax.set_yticks(range(len(features)))
        ax.set_xticklabels(features, rotation=45, ha='right')
        ax.set_yticklabels(features)
        cbar = plt.colorbar(im, ax=ax)
        cbar.set_label('Correlation', fontsize=12)
        for i in range(len(features)):
            for j in range(len(features)):
                text = ax.text(j, i, f'{corr_matrix.iloc[i, j]:.2f}', ha='center', va='center', color='black', fontsize=8)
        ax.set_title('Feature Correlation Matrix', fontsize=14, fontweight='bold', pad=20)
        plt.tight_layout()
        plt.savefig(f'{output_dir}/correlation_matrix.png', dpi=300, bbox_inches='tight')
        print(f'  [OK] Saved: {output_dir}/correlation_matrix.png')
        plt.close()

    def generate_report(self, output_file: str='analysis_report.txt'):
        print(f'\nGenerating analysis report...')
        with open(output_file, 'w') as f:
            f.write('=' * 70 + '\n')
            f.write('MOTOR VIBRATION ANALYSIS REPORT\n')
            f.write('=' * 70 + '\n\n')
            f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f'Data File: {self.data_file}\n')
            f.write(f'Total Records: {len(self.df)}\n\n')
            f.write('TIME RANGE\n')
            f.write('-' * 70 + '\n')
            f.write(f"Start: {self.df['timestamp'].min()}\n")
            f.write(f"End:   {self.df['timestamp'].max()}\n")
            f.write(f"Duration: {self.df['timestamp'].max() - self.df['timestamp'].min()}\n\n")
            f.write('FAULT ANALYSIS\n')
            f.write('-' * 70 + '\n')
            fault_counts = self.df['fault_type'].value_counts()
            for fault_type, count in fault_counts.items():
                pct = count / len(self.df) * 100
                f.write(f'{fault_type:20s}: {count:6d} ({pct:5.1f}%)\n')
            f.write('\n')
            f.write('FEATURE STATISTICS\n')
            f.write('-' * 70 + '\n')
            features = ['rms', 'kurtosis', 'crestFactor', 'dominantFreq']
            for feature in features:
                f.write(f'\n{feature}:\n')
                f.write(f'  Mean: {self.df[feature].mean():.4f}\n')
                f.write(f'  Std:  {self.df[feature].std():.4f}\n')
                f.write(f'  Min:  {self.df[feature].min():.4f}\n')
                f.write(f'  Max:  {self.df[feature].max():.4f}\n')
            f.write('\n' + '=' * 70 + '\n')
        print(f'  [OK] Saved: {output_file}')

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Analyze motor vibration data')
    parser.add_argument('input_file', help='Input data file (JSON or CSV)')
    parser.add_argument('--output-dir', default='plots', help='Output directory for plots')
    parser.add_argument('--format', choices=['json', 'csv'], default='json', help='Input file format')
    args = parser.parse_args()
    analyzer = VibrationAnalyzer(args.input_file)
    if args.format == 'json':
        analyzer.load_json_logs(args.input_file)
    else:
        analyzer.load_csv_logs(args.input_file)
    analyzer.generate_summary_stats()
    analyzer.plot_time_series(args.output_dir)
    analyzer.plot_distributions(args.output_dir)
    analyzer.plot_correlation_matrix(args.output_dir)
    analyzer.generate_report()
    print('\n[OK] Analysis complete!')
if __name__ == '__main__':
    main()
