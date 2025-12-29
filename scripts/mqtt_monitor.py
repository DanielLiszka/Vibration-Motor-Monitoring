import json
import sys
from datetime import datetime
from typing import Dict, Any
try:
    import paho.mqtt.client as mqtt
    from rich.console import Console
    from rich.table import Table
    from rich.live import Live
    from rich.layout import Layout
    from rich.panel import Panel
    from rich import box
except ImportError:
    print('Error: Required packages not installed')
    print('Install with: pip install paho-mqtt rich')
    sys.exit(1)

class MotorMonitor:

    def __init__(self, broker: str='broker.hivemq.com', port: int=1883):
        self.broker = broker
        self.port = port
        self.console = Console()
        self.latest_status = 'Connecting...'
        self.latest_vibration = {}
        self.latest_fault = {'type': 'NONE', 'severity': 'NORMAL'}
        self.message_count = 0
        self.fault_count = 0
        self.start_time = datetime.now()
        self.client = mqtt.Client(client_id='MotorMonitor_Python')
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.latest_status = 'Connected'
            client.subscribe('motor/#')
            self.console.print('[green][OK] Connected to MQTT broker[/green]')
        else:
            self.console.print(f'[red][FAIL] Connection failed with code {rc}[/red]')

    def on_message(self, client, userdata, msg):
        self.message_count += 1
        try:
            payload = json.loads(msg.payload.decode())
            if msg.topic == 'motor/status':
                self.latest_status = payload.get('status', 'Unknown')
            elif msg.topic == 'motor/vibration':
                self.latest_vibration = payload
            elif msg.topic == 'motor/fault':
                self.latest_fault = payload
                if payload.get('type') != 'NONE':
                    self.fault_count += 1
                    self.log_fault(payload)
            elif msg.topic == 'motor/features':
                self.latest_vibration = payload
        except json.JSONDecodeError:
            self.console.print(f'[yellow]⚠ Invalid JSON from {msg.topic}[/yellow]')

    def log_fault(self, fault: Dict[str, Any]):
        severity = fault.get('severity', 'UNKNOWN')
        color = 'red' if severity == 'CRITICAL' else 'yellow'
        self.console.print(f"\n[{color}]{'=' * 60}[/{color}]")
        self.console.print(f'[{color}]  FAULT DETECTED![/{color}]')
        self.console.print(f"[{color}]{'=' * 60}[/{color}]")
        self.console.print(f"Type: [{color}]{fault.get('type', 'UNKNOWN')}[/{color}]")
        self.console.print(f'Severity: [{color}]{severity}[/{color}]')
        self.console.print(f"Confidence: {fault.get('confidence', 0) * 100:.1f}%")
        self.console.print(f"Anomaly Score: {fault.get('anomalyScore', 0):.4f}")
        self.console.print(f"Description: {fault.get('description', 'N/A')}")
        self.console.print(f"[{color}]{'=' * 60}[/{color}]\n")

    def generate_dashboard(self) -> Layout:
        layout = Layout()
        layout.split_column(Layout(name='header', size=3), Layout(name='body'), Layout(name='footer', size=5))
        uptime = datetime.now() - self.start_time
        header_text = f'VibeSentry | Uptime: {uptime} | Messages: {self.message_count}'
        layout['header'].update(Panel(header_text, style='bold white on blue'))
        layout['body'].split_row(Layout(name='left'), Layout(name='right'))
        if self.latest_vibration:
            features_table = Table(title='Vibration Features', box=box.ROUNDED)
            features_table.add_column('Feature', style='cyan')
            features_table.add_column('Value', justify='right', style='yellow')
            feature_map = {'rms': 'RMS', 'peakToPeak': 'Peak-to-Peak', 'kurtosis': 'Kurtosis', 'skewness': 'Skewness', 'crestFactor': 'Crest Factor', 'variance': 'Variance', 'spectralCentroid': 'Spectral Centroid (Hz)', 'spectralSpread': 'Spectral Spread', 'bandPowerRatio': 'Band Power Ratio', 'dominantFreq': 'Dominant Freq (Hz)'}
            for key, label in feature_map.items():
                if key in self.latest_vibration:
                    value = self.latest_vibration[key]
                    features_table.add_row(label, f'{value:.4f}')
            layout['left'].update(features_table)
        else:
            layout['left'].update(Panel('Waiting for vibration data...', style='dim'))
        fault_type = self.latest_fault.get('type', 'NONE')
        severity = self.latest_fault.get('severity', 'NORMAL')
        if severity == 'CRITICAL':
            fault_color = 'red'
        elif severity == 'WARNING':
            fault_color = 'yellow'
        else:
            fault_color = 'green'
        fault_table = Table(title='Fault Status', box=box.ROUNDED)
        fault_table.add_column('Item', style='cyan')
        fault_table.add_column('Value', justify='right')
        fault_table.add_row('Type', f'[{fault_color}]{fault_type}[/{fault_color}]')
        fault_table.add_row('Severity', f'[{fault_color}]{severity}[/{fault_color}]')
        fault_table.add_row('Confidence', f"{self.latest_fault.get('confidence', 0) * 100:.1f}%")
        fault_table.add_row('Anomaly Score', f"{self.latest_fault.get('anomalyScore', 0):.4f}")
        fault_table.add_row('Total Faults', f'[red]{self.fault_count}[/red]')
        layout['right'].update(fault_table)
        footer_table = Table(box=box.SIMPLE)
        footer_table.add_column('Status', style='green')
        footer_table.add_column('Device', style='cyan')
        footer_table.add_column('Broker', style='yellow')
        device = self.latest_vibration.get('device', 'Unknown')
        footer_table.add_row(self.latest_status, device, f'{self.broker}:{self.port}')
        layout['footer'].update(footer_table)
        return layout

    def run(self):
        self.console.print('[bold blue]VibeSentry MQTT Monitor[/bold blue]')
        self.console.print(f'Connecting to {self.broker}:{self.port}...\n')
        try:
            self.client.connect(self.broker, self.port, 60)
        except Exception as e:
            self.console.print(f'[red][FAIL] Failed to connect: {e}[/red]')
            return
        self.client.loop_start()
        try:
            with Live(self.generate_dashboard(), refresh_per_second=2, console=self.console) as live:
                while True:
                    live.update(self.generate_dashboard())
        except KeyboardInterrupt:
            self.console.print('\n[yellow]Shutting down...[/yellow]')
        finally:
            self.client.loop_stop()
            self.client.disconnect()
            self.console.print('[green][OK] Disconnected[/green]')

def main():
    import argparse
    parser = argparse.ArgumentParser(description='VibeSentry MQTT Monitor')
    parser.add_argument('--broker', default='broker.hivemq.com', help='MQTT broker address (default: broker.hivemq.com)')
    parser.add_argument('--port', type=int, default=1883, help='MQTT broker port (default: 1883)')
    args = parser.parse_args()
    monitor = MotorMonitor(broker=args.broker, port=args.port)
    monitor.run()
if __name__ == '__main__':
    main()
