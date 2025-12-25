class MotorMonitor {
    constructor() {
        this.ws = null;
        this.connected = false;
        this.signalChart = null;
        this.spectrumChart = null;
        this.trendChart = null;
        this.signalData = [];
        this.spectrumData = [];
        this.trendData = [];
        this.maxDataPoints = 100;

        this.init();
    }

    init() {
        this.setupTabs();
        this.setupCharts();
        this.setupEventListeners();
        this.connectWebSocket();
    }

    setupTabs() {
        const tabs = document.querySelectorAll('.nav-tab');
        tabs.forEach(tab => {
            tab.addEventListener('click', () => {
                tabs.forEach(t => t.classList.remove('active'));
                tab.classList.add('active');

                const tabId = tab.dataset.tab;
                document.querySelectorAll('.tab-content').forEach(content => {
                    content.classList.remove('active');
                });
                document.getElementById(tabId).classList.add('active');

                if (tabId === 'spectrum') {
                    this.resizeChart(this.spectrumChart);
                } else if (tabId === 'trends') {
                    this.resizeChart(this.trendChart);
                }
            });
        });
    }

    setupCharts() {
        const chartOptions = {
            responsive: true,
            maintainAspectRatio: false,
            animation: { duration: 0 },
            scales: {
                x: {
                    grid: { color: 'rgba(255, 255, 255, 0.1)' },
                    ticks: { color: '#94a3b8' }
                },
                y: {
                    grid: { color: 'rgba(255, 255, 255, 0.1)' },
                    ticks: { color: '#94a3b8' }
                }
            },
            plugins: {
                legend: { display: false }
            }
        };

        const signalCtx = document.getElementById('signal-chart');
        if (signalCtx) {
            this.signalChart = new Chart(signalCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        data: [],
                        borderColor: '#2563eb',
                        borderWidth: 1,
                        fill: false,
                        tension: 0,
                        pointRadius: 0
                    }]
                },
                options: {
                    ...chartOptions,
                    scales: {
                        ...chartOptions.scales,
                        y: {
                            ...chartOptions.scales.y,
                            min: -3,
                            max: 3
                        }
                    }
                }
            });
        }

        const spectrumCtx = document.getElementById('spectrum-chart');
        if (spectrumCtx) {
            this.spectrumChart = new Chart(spectrumCtx, {
                type: 'bar',
                data: {
                    labels: [],
                    datasets: [{
                        data: [],
                        backgroundColor: 'rgba(37, 99, 235, 0.6)',
                        borderColor: '#2563eb',
                        borderWidth: 1
                    }]
                },
                options: {
                    ...chartOptions,
                    scales: {
                        ...chartOptions.scales,
                        x: {
                            ...chartOptions.scales.x,
                            title: { display: true, text: 'Frequency (Hz)', color: '#94a3b8' }
                        },
                        y: {
                            ...chartOptions.scales.y,
                            title: { display: true, text: 'Magnitude', color: '#94a3b8' }
                        }
                    }
                }
            });
        }

        const trendCtx = document.getElementById('trend-chart');
        if (trendCtx) {
            this.trendChart = new Chart(trendCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'RMS',
                        data: [],
                        borderColor: '#2563eb',
                        backgroundColor: 'rgba(37, 99, 235, 0.1)',
                        fill: true,
                        tension: 0.4
                    }]
                },
                options: {
                    ...chartOptions,
                    plugins: {
                        legend: { display: true, labels: { color: '#94a3b8' } }
                    }
                }
            });
        }
    }

    resizeChart(chart) {
        if (chart) {
            setTimeout(() => chart.resize(), 100);
        }
    }

    setupEventListeners() {
        document.getElementById('calibrate-btn')?.addEventListener('click', () => this.calibrate());
        document.getElementById('export-btn')?.addEventListener('click', () => this.exportData());
        document.getElementById('reset-btn')?.addEventListener('click', () => this.resetDevice());
        document.getElementById('ack-all-btn')?.addEventListener('click', () => this.acknowledgeAllAlerts());
        document.getElementById('clear-all-btn')?.addEventListener('click', () => this.clearAllAlerts());

        document.getElementById('config-form')?.addEventListener('submit', (e) => {
            e.preventDefault();
            this.saveConfig();
        });

        document.getElementById('threshold-form')?.addEventListener('submit', (e) => {
            e.preventDefault();
            this.saveThresholds();
        });

        document.getElementById('spectrum-scale')?.addEventListener('change', (e) => {
            this.updateSpectrumScale(e.target.value);
        });

        document.getElementById('trend-metric')?.addEventListener('change', (e) => {
            this.updateTrendMetric(e.target.value);
        });
    }

    connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.hostname}:81/ws`;

        this.ws = new WebSocket(wsUrl);

        this.ws.onopen = () => {
            this.connected = true;
            this.updateConnectionStatus(true);
            console.log('WebSocket connected');
        };

        this.ws.onclose = () => {
            this.connected = false;
            this.updateConnectionStatus(false);
            console.log('WebSocket disconnected');
            setTimeout(() => this.connectWebSocket(), 5000);
        };

        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };

        this.ws.onmessage = (event) => {
            this.handleMessage(JSON.parse(event.data));
        };
    }

    handleMessage(data) {
        switch (data.type) {
            case 'features':
                this.updateFeatures(data.data);
                break;
            case 'fault':
                this.updateFault(data.data);
                break;
            case 'spectrum':
                this.updateSpectrum(data.data);
                break;
            case 'system':
                this.updateSystemStatus(data.data);
                break;
            case 'alert':
                this.addAlert(data.data);
                break;
            case 'signal':
                this.updateSignalChart(data.data);
                break;
        }
    }

    updateConnectionStatus(connected) {
        const statusEl = document.getElementById('connection-status');
        if (statusEl) {
            statusEl.textContent = connected ? 'Connected' : 'Disconnected';
            statusEl.className = `status ${connected ? 'connected' : 'disconnected'}`;
        }
    }

    updateFeatures(features) {
        this.setElementText('rms', features.rms?.toFixed(3) || '0.000');
        this.setElementText('peak-to-peak', features.peakToPeak?.toFixed(3) || '0.000');
        this.setElementText('kurtosis', features.kurtosis?.toFixed(2) || '0.00');
        this.setElementText('crest-factor', features.crestFactor?.toFixed(2) || '0.00');
        this.setElementText('dominant-freq', `${features.dominantFrequency?.toFixed(1) || '0.0'} Hz`);
        this.setElementText('spectral-centroid', `${features.spectralCentroid?.toFixed(1) || '0.0'} Hz`);
        this.setElementText('band-power', features.bandPowerRatio?.toFixed(3) || '0.000');

        this.signalData.push(features.rms || 0);
        if (this.signalData.length > this.maxDataPoints) {
            this.signalData.shift();
        }
        this.updateSignalChartData();

        this.trendData.push({
            timestamp: Date.now(),
            rms: features.rms,
            kurtosis: features.kurtosis,
            crestFactor: features.crestFactor,
            dominantFreq: features.dominantFrequency
        });
        if (this.trendData.length > 500) {
            this.trendData.shift();
        }
        this.updateTrendChart();
    }

    updateFault(fault) {
        const indicator = document.getElementById('fault-indicator');
        const faultText = indicator?.querySelector('.fault-text');
        const faultIcon = indicator?.querySelector('.fault-icon');

        const faultTypes = ['Normal', 'Imbalance', 'Misalignment', 'Bearing Fault', 'Looseness'];
        const faultType = faultTypes[fault.type] || 'Unknown';

        if (indicator) {
            indicator.className = 'fault-indicator';
            if (fault.type === 0) {
                indicator.classList.add('normal');
                if (faultIcon) faultIcon.textContent = '✓';
                if (faultText) faultText.textContent = 'Normal Operation';
            } else if (fault.severity >= 2) {
                indicator.classList.add('critical');
                if (faultIcon) faultIcon.textContent = '⚠';
                if (faultText) faultText.textContent = `Critical: ${faultType}`;
            } else {
                indicator.classList.add('warning');
                if (faultIcon) faultIcon.textContent = '!';
                if (faultText) faultText.textContent = `Warning: ${faultType}`;
            }
        }

        this.setElementText('fault-confidence', Math.round((fault.confidence || 0) * 100));
        const severityNames = ['None', 'Low', 'Medium', 'High', 'Critical'];
        this.setElementText('fault-severity', severityNames[fault.severity] || 'Unknown');
    }

    updateSpectrum(spectrum) {
        if (!this.spectrumChart || !spectrum.data) return;

        const freqResolution = (spectrum.sampleRate || 1000) / (spectrum.fftSize || 256);
        const labels = spectrum.data.map((_, i) => (i * freqResolution).toFixed(0));

        this.spectrumChart.data.labels = labels;
        this.spectrumChart.data.datasets[0].data = spectrum.data;
        this.spectrumChart.update('none');
    }

    updateSystemStatus(system) {
        const cpuUsage = system.cpuUsage || 0;
        const freeHeap = system.freeHeap || 0;
        const totalHeap = 320000;
        const memoryUsage = ((totalHeap - freeHeap) / totalHeap) * 100;

        this.setElementText('cpu-value', `${cpuUsage.toFixed(0)}%`);
        this.setProgress('cpu-bar', cpuUsage);

        this.setElementText('memory-value', `${Math.round(freeHeap / 1024)} KB`);
        this.setProgress('memory-bar', memoryUsage);

        this.setElementText('wifi-rssi', `${system.rssi || '--'} dBm`);
        this.setElementText('uptime', this.formatUptime(system.uptime || 0));
        this.setElementText('free-heap-info', `${Math.round(freeHeap / 1024)} KB`);
    }

    updateSignalChartData() {
        if (!this.signalChart) return;

        this.signalChart.data.labels = this.signalData.map((_, i) => i);
        this.signalChart.data.datasets[0].data = this.signalData;
        this.signalChart.update('none');
    }

    updateSignalChart(signal) {
        if (!this.signalChart || !signal.data) return;

        this.signalChart.data.labels = signal.data.map((_, i) => i);
        this.signalChart.data.datasets[0].data = signal.data;
        this.signalChart.update('none');
    }

    updateTrendChart() {
        if (!this.trendChart || this.trendData.length === 0) return;

        const metric = document.getElementById('trend-metric')?.value || 'rms';
        const labels = this.trendData.map(d => this.formatTime(d.timestamp));
        const data = this.trendData.map(d => d[metric] || 0);

        this.trendChart.data.labels = labels;
        this.trendChart.data.datasets[0].data = data;
        this.trendChart.data.datasets[0].label = metric.charAt(0).toUpperCase() + metric.slice(1);
        this.trendChart.update('none');
    }

    updateSpectrumScale(scale) {
        if (!this.spectrumChart) return;

        this.spectrumChart.options.scales.y.type = scale === 'log' ? 'logarithmic' : 'linear';
        this.spectrumChart.update();
    }

    updateTrendMetric(metric) {
        this.updateTrendChart();
    }

    addAlert(alert) {
        const alertList = document.getElementById('alert-list');
        if (!alertList) return;

        const noAlerts = alertList.querySelector('.no-alerts');
        if (noAlerts) noAlerts.remove();

        const severityClass = alert.severity >= 2 ? 'critical' : alert.severity === 1 ? 'warning' : 'info';
        const icon = alert.severity >= 2 ? '⚠' : alert.severity === 1 ? '!' : 'ℹ';

        const alertEl = document.createElement('div');
        alertEl.className = `alert-item ${severityClass}`;
        alertEl.innerHTML = `
            <span class="alert-icon">${icon}</span>
            <div class="alert-content">
                <div class="alert-message">${alert.message}</div>
                <div class="alert-time">${this.formatTime(alert.timestamp)}</div>
            </div>
            <button class="btn btn-secondary" onclick="monitor.acknowledgeAlert(${alert.id})">Ack</button>
        `;

        alertList.insertBefore(alertEl, alertList.firstChild);
    }

    acknowledgeAlert(id) {
        this.sendCommand('ack_alert', { id });
    }

    acknowledgeAllAlerts() {
        this.sendCommand('ack_all_alerts');
    }

    clearAllAlerts() {
        const alertList = document.getElementById('alert-list');
        if (alertList) {
            alertList.innerHTML = '<p class="no-alerts">No alerts</p>';
        }
        this.sendCommand('clear_all_alerts');
    }

    calibrate() {
        if (confirm('Start baseline calibration?')) {
            this.sendCommand('calibrate');
        }
    }

    exportData() {
        this.sendCommand('export');
        window.open('/api/v1/export?format=csv', '_blank');
    }

    resetDevice() {
        if (confirm('Are you sure you want to reset the device?')) {
            this.sendCommand('reset');
        }
    }

    saveConfig() {
        const config = {
            deviceName: document.getElementById('device-name')?.value,
            sampleRate: parseInt(document.getElementById('sample-rate')?.value),
            windowSize: parseInt(document.getElementById('window-size')?.value)
        };
        this.sendCommand('config', config);
    }

    saveThresholds() {
        const thresholds = {
            rmsWarning: parseFloat(document.getElementById('rms-warning')?.value),
            rmsCritical: parseFloat(document.getElementById('rms-critical')?.value),
            kurtosisWarning: parseFloat(document.getElementById('kurtosis-warning')?.value)
        };
        this.sendCommand('thresholds', thresholds);
    }

    sendCommand(command, data = {}) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({ command, ...data }));
        }
    }

    setElementText(id, value) {
        const el = document.getElementById(id);
        if (el) el.textContent = value;
    }

    setProgress(id, value) {
        const el = document.getElementById(id);
        if (el) el.style.width = `${Math.min(100, Math.max(0, value))}%`;
    }

    formatUptime(ms) {
        const seconds = Math.floor(ms / 1000);
        const h = Math.floor(seconds / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = seconds % 60;
        return `${h}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
    }

    formatTime(timestamp) {
        const date = new Date(timestamp);
        return date.toLocaleTimeString();
    }
}

const monitor = new MotorMonitor();
