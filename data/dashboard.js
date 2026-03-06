class MotorMonitor {
    constructor() {
        this.ws = null;
        this.signalData = [];
        this.spectrumData = [];
        this.historyRecords = [];
        this.alerts = [];
        this.maxSignalPoints = 120;
        this.signalCanvas = document.getElementById('signal-chart');
        this.spectrumCanvas = document.getElementById('spectrum-chart');
        this.trendCanvas = document.getElementById('trend-chart');
        this.init();
    }

    init() {
        this.setupTabs();
        this.setupEventListeners();
        this.connectWebSocket();
        this.refreshAll();
        setInterval(() => this.refreshStatus(), 5000);
        setInterval(() => this.refreshSystemInfo(), 10000);
        setInterval(() => this.refreshAlerts(), 7000);
        setInterval(() => this.refreshHistory(), 8000);
        setInterval(() => this.refreshConfig(), 15000);
        window.addEventListener('resize', () => this.redrawCharts());
    }

    setupTabs() {
        document.querySelectorAll('.nav-tab').forEach((tab) => {
            tab.addEventListener('click', () => {
                document.querySelectorAll('.nav-tab').forEach((item) => item.classList.remove('active'));
                document.querySelectorAll('.tab-content').forEach((item) => item.classList.remove('active'));
                tab.classList.add('active');
                document.getElementById(tab.dataset.tab)?.classList.add('active');
                this.redrawCharts();
            });
        });
    }

    setupEventListeners() {
        document.getElementById('settings-form')?.addEventListener('submit', (event) => {
            event.preventDefault();
            this.saveSettings();
        });
        document.getElementById('calibrate-btn')?.addEventListener('click', () => this.calibrate());
        document.getElementById('export-json-btn')?.addEventListener('click', () => this.exportData('json'));
        document.getElementById('export-btn')?.addEventListener('click', () => this.exportData('csv'));
        document.getElementById('reset-btn')?.addEventListener('click', () => this.resetDevice());
        document.getElementById('ack-all-btn')?.addEventListener('click', () => this.acknowledgeAllAlerts());
        document.getElementById('clear-all-btn')?.addEventListener('click', () => this.clearAllAlerts());
    }

    async refreshAll() {
        await Promise.all([
            this.refreshConfig(),
            this.refreshStatus(),
            this.refreshSystemInfo(),
            this.refreshAlerts(),
            this.refreshHistory()
        ]);
    }

    connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        this.ws = new WebSocket(`${protocol}//${window.location.host}/ws`);
        this.ws.onopen = () => {
            this.updateConnectionStatus(true);
            this.setStatusMessage('Live dashboard connected.', 'success');
        };
        this.ws.onclose = () => {
            this.updateConnectionStatus(false);
            this.setStatusMessage('Live dashboard disconnected. Retrying...', 'warning');
            setTimeout(() => this.connectWebSocket(), 3000);
        };
        this.ws.onerror = () => this.setStatusMessage('WebSocket error. Falling back to polling.', 'warning');
        this.ws.onmessage = (event) => {
            try {
                this.handleSocketPayload(JSON.parse(event.data));
            } catch (error) {
                console.error('Failed to parse socket payload', error);
            }
        };
    }

    handleSocketPayload(payload) {
        if (payload.type === 'alert' && payload.data) {
            this.addAlert(payload.data, true);
            return;
        }

        if (payload.features) {
            this.updateFeatureCards(payload.features);
            this.pushSignalPoint(payload.features.rms || 0);
        }
        if (payload.fault) {
            this.updateFaultCard(payload.fault);
        }
        if (Array.isArray(payload.spectrum)) {
            this.spectrumData = payload.spectrum;
            this.drawSpectrumChart();
        }
        if (typeof payload.uptime !== 'undefined') {
            this.setElementText('uptime', this.formatUptime(payload.uptime * 1000));
        }
    }

    async refreshConfig() {
        const data = await this.fetchJson('/api/v1/config');
        const config = data?.config;
        if (!config) return;

        const thresholds = config.thresholds || {};
        const dashboard = config.dashboard || {};
        const wifi = config.wifi || {};
        const mqtt = config.mqtt || {};
        const ota = config.ota || {};
        const provisioning = config.provisioning || {};

        this.setInputValue('device-name', config.deviceName || '');
        this.setInputValue('device-id-input', config.deviceId || '');
        this.setInputValue('broadcast-interval', dashboard.broadcastIntervalMs || 250);
        this.setInputValue('warning-threshold', thresholds.warning || 2.0);
        this.setInputValue('critical-threshold', thresholds.critical || 3.0);
        this.setCheckboxValue('wifi-enabled', !!wifi.enabled);
        this.setInputValue('wifi-ssid', wifi.ssid || '');
        this.setInputValue('wifi-password', '');
        this.setCheckboxValue('mqtt-enabled', !!mqtt.enabled);
        this.setInputValue('mqtt-broker', mqtt.broker || '');
        this.setInputValue('mqtt-port', mqtt.port || 1883);
        this.setInputValue('mqtt-user', mqtt.user || '');
        this.setInputValue('mqtt-password', '');
        this.setCheckboxValue('ota-enabled', !!ota.enabled);
        this.setInputValue('ota-password', '');
        this.setElementText('device-id', config.deviceId || '--');

        if (provisioning.active) {
            this.setStatusMessage(
                `Provisioning mode active on ${provisioning.ssid || 'device AP'} (${provisioning.ip || '192.168.4.1'}). Save WiFi settings, then reset the device.`,
                'warning'
            );
        }
    }

    async refreshStatus() {
        const data = await this.fetchJson('/api/v1/status');
        if (!data) return;
        this.setElementText('system-mode', data.faultDetected ? 'FAULT' : 'RUNNING');
        this.setElementText('calibrated-status', data.calibrated ? 'Yes' : 'No');
        this.setElementText('api-version', data.apiVersion || '--');
        this.setElementText('uptime', this.formatUptime(data.uptime || 0));
        if (data.provisioningActive) {
            this.setStatusMessage(
                `Provisioning mode active on ${data.provisioningSsid || 'device AP'} (${data.networkIp || '192.168.4.1'}). Save WiFi settings, then reset the device.`,
                'warning'
            );
        } else if (data.wifiConnected) {
            this.setStatusMessage('Device connected and monitoring live.', 'success');
        }
    }

    async refreshSystemInfo() {
        const system = await this.fetchJson('/api/v1/system');
        const health = await this.fetchJson('/api/v1/health');
        const metrics = await this.fetchJson('/api/v1/metrics');

        if (system?.system) {
            this.setElementText('firmware-version', system.system.firmwareVersion || '--');
            this.setElementText('chip-model', system.system.chipModel || '--');
            this.setElementText('flash-size', this.formatBytes(system.system.flashSize || 0));
            this.setElementText('free-heap-info', this.formatBytes(system.system.freeHeap || 0));
            if (system.system.deviceId) {
                this.setElementText('device-id', system.system.deviceId);
            }
        }
        if (health?.health) {
            this.setElementText('wifi-rssi', `${health.health.rssi ?? '--'} dBm`);
        }
        if (metrics?.metrics) {
            const cpu = Number(metrics.metrics.cpuUsage || 0);
            const freeHeap = system?.system?.freeHeap || 0;
            const heapSize = system?.system?.heapSize || 1;
            const usedPercent = heapSize > 0 ? ((heapSize - freeHeap) / heapSize) * 100 : 0;
            this.setElementText('cpu-value', `${cpu.toFixed(0)}%`);
            this.setProgress('cpu-bar', cpu);
            this.setElementText('memory-value', `${Math.round(freeHeap / 1024)} KB`);
            this.setProgress('memory-bar', usedPercent);
        }
    }

    async refreshHistory() {
        const data = await this.fetchJson('/api/v1/history?limit=120');
        if (!data || !Array.isArray(data.records)) return;
        this.historyRecords = data.records;
        this.drawTrendChart();
        this.updateHistorySummary();
    }

    async refreshAlerts() {
        const data = await this.fetchJson('/api/v1/alerts');
        if (!data || !Array.isArray(data.alerts)) return;
        this.alerts = data.alerts;
        this.renderAlerts();
    }

    async saveSettings() {
        const payload = {
            deviceName: this.getInputValue('device-name'),
            deviceId: this.getInputValue('device-id-input'),
            thresholds: {
                warning: this.getNumberValue('warning-threshold'),
                critical: this.getNumberValue('critical-threshold')
            },
            dashboard: {
                broadcastIntervalMs: this.getNumberValue('broadcast-interval')
            },
            wifi: {
                enabled: this.getCheckboxValue('wifi-enabled'),
                ssid: this.getInputValue('wifi-ssid'),
                password: this.getInputValue('wifi-password')
            },
            mqtt: {
                enabled: this.getCheckboxValue('mqtt-enabled'),
                broker: this.getInputValue('mqtt-broker'),
                port: this.getNumberValue('mqtt-port'),
                user: this.getInputValue('mqtt-user'),
                password: this.getInputValue('mqtt-password')
            },
            ota: {
                enabled: this.getCheckboxValue('ota-enabled'),
                password: this.getInputValue('ota-password')
            }
        };

        const response = await this.fetchJson('/api/v1/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        if (!response?.success) {
            this.setStatusMessage('Failed to save settings.', 'error');
            return;
        }

        this.setStatusMessage(
            response.restartRequired
                ? 'Settings saved. Restart the device to apply network identity changes.'
                : 'Settings saved.',
            response.restartRequired ? 'warning' : 'success'
        );
        this.setInputValue('wifi-password', '');
        this.setInputValue('mqtt-password', '');
        this.setInputValue('ota-password', '');
    }

    async calibrate() {
        if (!window.confirm('Start baseline calibration?')) return;
        const response = await this.fetchJson('/api/v1/calibrate', { method: 'POST' });
        if (response?.success) this.setStatusMessage('Calibration scheduled.', 'success');
    }

    exportData(format) {
        window.open(`/api/v1/export?format=${format}`, '_blank');
    }

    async resetDevice() {
        if (!window.confirm('Reset the device now?')) return;
        const response = await this.fetchJson('/api/v1/reset', { method: 'POST' });
        if (response?.success) this.setStatusMessage('Reset scheduled.', 'warning');
    }

    async acknowledgeAllAlerts() {
        const response = await this.fetchJson('/api/v1/alerts/ack', { method: 'POST' });
        if (response?.success) {
            this.setStatusMessage('Alerts acknowledged.', 'success');
            this.refreshAlerts();
        }
    }

    async clearAllAlerts() {
        const response = await this.fetchJson('/api/v1/alerts', { method: 'DELETE' });
        if (response?.success) {
            this.setStatusMessage('Alerts cleared.', 'success');
            this.refreshAlerts();
        }
    }

    updateFeatureCards(features) {
        this.setElementText('rms', this.formatNumber(features.rms, 3));
        this.setElementText('peak-to-peak', this.formatNumber(features.peakToPeak, 3));
        this.setElementText('kurtosis', this.formatNumber(features.kurtosis, 2));
        this.setElementText('crest-factor', this.formatNumber(features.crestFactor, 2));
        this.setElementText('dominant-freq', `${this.formatNumber(features.dominantFreq ?? features.dominantFrequency, 1)} Hz`);
        this.setElementText('spectral-centroid', `${this.formatNumber(features.spectralCentroid, 1)} Hz`);
        this.setElementText('band-power', this.formatNumber(features.bandPowerRatio, 3));
    }

    updateFaultCard(fault) {
        const indicator = document.getElementById('fault-indicator');
        const icon = indicator?.querySelector('.fault-icon');
        const text = indicator?.querySelector('.fault-text');
        const severity = typeof fault.severity === 'number' ? ['NORMAL', 'WARNING', 'CRITICAL'][fault.severity] : (fault.severity || 'NORMAL');
        const type = typeof fault.type === 'number' ? ['NONE', 'IMBALANCE', 'MISALIGNMENT', 'BEARING', 'LOOSENESS', 'UNKNOWN'][fault.type] : (fault.type || 'NONE');

        if (indicator) {
            indicator.className = 'fault-indicator';
            if (type === 'NONE' || severity === 'NORMAL') {
                indicator.classList.add('normal');
                if (icon) icon.textContent = '✓';
                if (text) text.textContent = 'Normal Operation';
            } else if (severity === 'CRITICAL') {
                indicator.classList.add('critical');
                if (icon) icon.textContent = '⚠';
                if (text) text.textContent = `Critical: ${type}`;
            } else {
                indicator.classList.add('warning');
                if (icon) icon.textContent = '!';
                if (text) text.textContent = `Warning: ${type}`;
            }
        }

        this.setElementText('fault-confidence', Math.round((fault.confidence || 0) * 100));
        this.setElementText('fault-severity', severity);
        this.setElementText('fault-description', fault.description || 'No active fault');
    }

    pushSignalPoint(value) {
        this.signalData.push(Number(value) || 0);
        if (this.signalData.length > this.maxSignalPoints) this.signalData.shift();
        this.drawSignalChart();
    }

    updateHistorySummary() {
        const values = this.historyRecords.map((record) => Number(record.features?.rms || 0));
        this.setElementText('history-depth', values.length);
        this.setElementText('buffer-usage', `${values.length} samples`);
        const slope = this.calculateSlope(values);
        this.setElementText('trend-status', slope > 0.01 ? 'Watch' : 'Stable');
        this.setElementText('trend-direction', slope > 0.002 ? 'Rising' : slope < -0.002 ? 'Falling' : 'Flat');
        this.setElementText('trend-rate', `${slope.toFixed(4)}/window`);
    }

    addAlert(alert, prepend = false) {
        const normalized = {
            severity: typeof alert.severity === 'number' ? alert.severity : this.mapSeverity(alert.severity),
            message: alert.message || alert.description || 'Alert',
            timestamp: alert.timestamp || Date.now()
        };
        if (prepend) {
            this.alerts.unshift(normalized);
            this.alerts = this.alerts.slice(0, 50);
            this.renderAlerts();
        }
    }

    renderAlerts() {
        const container = document.getElementById('alert-list');
        if (!container) return;
        if (!this.alerts.length) {
            container.innerHTML = '<p class="no-alerts">No alerts</p>';
            return;
        }

        container.innerHTML = this.alerts.map((alert) => {
            const level = alert.severity >= 2 ? 'critical' : alert.severity === 1 ? 'warning' : 'info';
            const icon = alert.severity >= 2 ? '⚠' : alert.severity === 1 ? '!' : 'i';
            return `<div class="alert-item ${level}"><span class="alert-icon">${icon}</span><div class="alert-content"><div class="alert-message">${this.escapeHtml(alert.message)}</div><div class="alert-time">${this.formatTimestamp(alert.timestamp)}</div></div></div>`;
        }).join('');
    }

    redrawCharts() {
        this.drawSignalChart();
        this.drawSpectrumChart();
        this.drawTrendChart();
    }

    drawSignalChart() {
        this.drawLineChart(this.signalCanvas, this.signalData, '#2563eb', 'rgba(37, 99, 235, 0.15)');
    }

    drawSpectrumChart() {
        this.drawBarChart(this.spectrumCanvas, this.spectrumData);
    }

    drawTrendChart() {
        const values = this.historyRecords.map((record) => Number(record.features?.rms || 0));
        this.drawLineChart(this.trendCanvas, values, '#10b981', 'rgba(16, 185, 129, 0.15)');
    }

    drawLineChart(canvas, values, stroke, fill) {
        if (!canvas) return;
        const context = canvas.getContext('2d');
        this.resizeCanvas(canvas);
        const width = canvas.width;
        const height = canvas.height;
        const padding = 24;
        context.clearRect(0, 0, width, height);
        context.fillStyle = '#0f172a';
        context.fillRect(0, 0, width, height);
        this.drawGrid(context, width, height, padding);
        if (!values.length) return this.drawEmptyState(context, width, height);

        const min = Math.min(...values);
        const max = Math.max(...values);
        const range = max - min || 1;
        context.beginPath();
        values.forEach((value, index) => {
            const x = padding + (index / Math.max(values.length - 1, 1)) * (width - padding * 2);
            const y = height - padding - ((value - min) / range) * (height - padding * 2);
            index === 0 ? context.moveTo(x, y) : context.lineTo(x, y);
        });
        context.strokeStyle = stroke;
        context.lineWidth = 2;
        context.stroke();
        context.lineTo(width - padding, height - padding);
        context.lineTo(padding, height - padding);
        context.closePath();
        context.fillStyle = fill;
        context.fill();
    }

    drawBarChart(canvas, values) {
        if (!canvas) return;
        const context = canvas.getContext('2d');
        this.resizeCanvas(canvas);
        const width = canvas.width;
        const height = canvas.height;
        const padding = 24;
        context.clearRect(0, 0, width, height);
        context.fillStyle = '#0f172a';
        context.fillRect(0, 0, width, height);
        this.drawGrid(context, width, height, padding);
        if (!values.length) return this.drawEmptyState(context, width, height);

        const max = Math.max(...values, 1);
        const barWidth = (width - padding * 2) / values.length;
        values.forEach((value, index) => {
            const scaled = (Number(value) || 0) / max;
            const x = padding + index * barWidth;
            const barHeight = scaled * (height - padding * 2);
            const y = height - padding - barHeight;
            context.fillStyle = 'rgba(37, 99, 235, 0.75)';
            context.fillRect(x, y, Math.max(barWidth - 1, 1), barHeight);
        });
    }

    drawGrid(context, width, height, padding) {
        context.strokeStyle = 'rgba(148, 163, 184, 0.12)';
        for (let index = 0; index <= 4; index += 1) {
            const y = padding + (index / 4) * (height - padding * 2);
            context.beginPath();
            context.moveTo(padding, y);
            context.lineTo(width - padding, y);
            context.stroke();
        }
    }

    drawEmptyState(context, width, height) {
        context.fillStyle = '#94a3b8';
        context.font = '14px sans-serif';
        context.textAlign = 'center';
        context.fillText('Waiting for data', width / 2, height / 2);
    }

    resizeCanvas(canvas) {
        const ratio = window.devicePixelRatio || 1;
        const width = canvas.clientWidth || canvas.parentElement.clientWidth || 640;
        const height = canvas.clientHeight || 240;
        canvas.width = Math.floor(width * ratio);
        canvas.height = Math.floor(height * ratio);
        canvas.getContext('2d').setTransform(ratio, 0, 0, ratio, 0, 0);
    }

    async fetchJson(url, options = {}) {
        try {
            const response = await fetch(url, options);
            if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
            return await response.json();
        } catch (error) {
            console.error(`Request failed for ${url}`, error);
            return null;
        }
    }

    updateConnectionStatus(connected) {
        const element = document.getElementById('connection-status');
        if (!element) return;
        element.textContent = connected ? 'Connected' : 'Disconnected';
        element.className = `status ${connected ? 'connected' : 'disconnected'}`;
    }

    setStatusMessage(message, level = 'info') {
        const element = document.getElementById('status-message');
        if (!element) return;
        element.textContent = message;
        element.dataset.level = level;
    }

    setElementText(id, value) {
        const element = document.getElementById(id);
        if (element) element.textContent = value;
    }

    setInputValue(id, value) {
        const element = document.getElementById(id);
        if (element) element.value = value;
    }

    setCheckboxValue(id, value) {
        const element = document.getElementById(id);
        if (element) element.checked = value;
    }

    getInputValue(id) {
        return document.getElementById(id)?.value?.trim() || '';
    }

    getNumberValue(id) {
        const value = Number(this.getInputValue(id));
        return Number.isFinite(value) ? value : 0;
    }

    getCheckboxValue(id) {
        return !!document.getElementById(id)?.checked;
    }

    setProgress(id, value) {
        const element = document.getElementById(id);
        if (element) element.style.width = `${Math.max(0, Math.min(100, value))}%`;
    }

    formatNumber(value, precision = 2) {
        return Number(value || 0).toFixed(precision);
    }

    formatUptime(milliseconds) {
        const totalSeconds = Math.floor((Number(milliseconds) || 0) / 1000);
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const seconds = totalSeconds % 60;
        return `${hours}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
    }

    formatTimestamp(timestamp) {
        return new Date(Number(timestamp) || Date.now()).toLocaleString();
    }

    formatBytes(bytes) {
        const value = Number(bytes) || 0;
        if (value >= 1024 * 1024) return `${(value / (1024 * 1024)).toFixed(1)} MB`;
        if (value >= 1024) return `${Math.round(value / 1024)} KB`;
        return `${value} B`;
    }

    mapSeverity(severity) {
        return { NORMAL: 0, WARNING: 1, CRITICAL: 2 }[String(severity || '').toUpperCase()] ?? 0;
    }

    calculateSlope(values) {
        if (values.length < 2) return 0;
        const xMean = (values.length - 1) / 2;
        const yMean = values.reduce((sum, value) => sum + value, 0) / values.length;
        let numerator = 0;
        let denominator = 0;
        values.forEach((value, index) => {
            numerator += (index - xMean) * (value - yMean);
            denominator += (index - xMean) * (index - xMean);
        });
        return denominator === 0 ? 0 : numerator / denominator;
    }

    escapeHtml(value) {
        return String(value)
            .replaceAll('&', '&amp;')
            .replaceAll('<', '&lt;')
            .replaceAll('>', '&gt;')
            .replaceAll('"', '&quot;')
            .replaceAll("'", '&#39;');
    }
}

window.monitor = new MotorMonitor();
