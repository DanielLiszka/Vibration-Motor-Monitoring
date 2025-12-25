#include "WebServer.h"
#include "DataExporter.h"

MotorWebServer::MotorWebServer()
    : server(nullptr)
    , ws(nullptr)
    , latestSpectrum(nullptr)
    , spectrumLength(0)
    , clientCount(0)
    , lastBroadcast(0)
    , broadcastInterval(100)
    , calibrationRequested(false)
    , resetRequested(false)
{
    server = new AsyncWebServer(WEB_SERVER_PORT);
    ws = new AsyncWebSocket("/ws");
    latestSpectrum = new float[FFT_OUTPUT_SIZE];
}

MotorWebServer::~MotorWebServer() {
    if (ws) {
        ws->closeAll();
        delete ws;
    }
    if (server) {
        server->end();
        delete server;
    }
    if (latestSpectrum) {
        delete[] latestSpectrum;
    }
}

bool MotorWebServer::begin() {
    DEBUG_PRINTLN("Initializing Web Server...");

    ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });

    server->addHandler(ws);
    setupRoutes();
    server->begin();

    DEBUG_PRINT("Web server started on port ");
    DEBUG_PRINTLN(WEB_SERVER_PORT);

    return true;
}

void MotorWebServer::loop() {
    ws->cleanupClients();

    if (clientCount > 0 && millis() - lastBroadcast >= broadcastInterval) {
        broadcastData();
        lastBroadcast = millis();
    }
}

void MotorWebServer::updateFeatures(const FeatureVector& features) {
    latestFeatures = features;
}

void MotorWebServer::updateFault(const FaultResult& fault) {
    latestFault = fault;
}

void MotorWebServer::updatePerformance(const PerformanceMetrics& metrics) {
    latestMetrics = metrics;
}

void MotorWebServer::updateSpectrum(const float* spectrum, size_t length) {
    if (length <= FFT_OUTPUT_SIZE && spectrum != nullptr) {
        memcpy(latestSpectrum, spectrum, length * sizeof(float));
        spectrumLength = length;
    }
}

void MotorWebServer::broadcastData() {
    if (clientCount == 0) return;

    String json = generateJSON();
    ws->textAll(json);
}

void MotorWebServer::broadcastMessage(const String& message) {
    if (clientCount > 0) {
        ws->textAll(message);
    }
}

void MotorWebServer::broadcastAlert(const String& alertJson) {
    if (clientCount > 0) {
        ws->textAll(alertJson);
    }
}

void MotorWebServer::setupRoutes() {
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleRoot(request);
    });

    server->on("/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleConfig(request);
    });

    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleAPI(request);
    });

    server->on("/api/spectrum", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String json = generateSpectrumJSON();
        request->send(200, "application/json", json);
    });

    server->on("/api/calibrate", HTTP_POST, [this](AsyncWebServerRequest* request) {
        this->handleCalibrate(request);
    });

    server->on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        this->handleReset(request);
    });

    server->on("/api/export", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleExport(request);
    });

    server->on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (request->hasParam("broadcast_interval", true)) {
            String value = request->getParam("broadcast_interval", true)->value();
            broadcastInterval = value.toInt();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
}

void MotorWebServer::handleRoot(AsyncWebServerRequest* request) {
    String html = generateDashboardHTML();
    request->send(200, "text/html", html);
}

void MotorWebServer::handleConfig(AsyncWebServerRequest* request) {
    String html = generateConfigHTML();
    request->send(200, "text/html", html);
}

void MotorWebServer::handleAPI(AsyncWebServerRequest* request) {
    String json = generateJSON();
    request->send(200, "application/json", json);
}

void MotorWebServer::handleCalibrate(AsyncWebServerRequest* request) {
    calibrationRequested = true;
    request->send(200, "application/json", "{\"status\":\"calibration_started\"}");
    broadcastMessage("{\"type\":\"notification\",\"message\":\"Calibration started from web interface\"}");
}

void MotorWebServer::handleReset(AsyncWebServerRequest* request) {
    resetRequested = true;
    request->send(200, "application/json", "{\"status\":\"reset_initiated\"}");
    broadcastMessage("{\"type\":\"notification\",\"message\":\"System reset requested from web interface\"}");
}

void MotorWebServer::handleExport(AsyncWebServerRequest* request) {
    String format = "json";
    if (request->hasParam("format")) {
        format = request->getParam("format")->value();
    }

    DataExporter exporter;
    String data;

    if (format == "csv") {
        data = exporter.exportFeatures(latestFeatures, FORMAT_CSV);
        request->send(200, "text/csv", data);
    } else if (format == "xml") {
        data = exporter.exportFeatures(latestFeatures, FORMAT_XML);
        request->send(200, "text/xml", data);
    } else {
        data = exporter.exportFeatures(latestFeatures, FORMAT_JSON);
        request->send(200, "application/json", data);
    }
}

void MotorWebServer::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            DEBUG_PRINT("WebSocket client connected: ");
            DEBUG_PRINTLN(client->id());
            clientCount++;
            client->text(generateJSON());
            break;

        case WS_EVT_DISCONNECT:
            DEBUG_PRINT("WebSocket client disconnected: ");
            DEBUG_PRINTLN(client->id());
            if (clientCount > 0) clientCount--;
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len) {
                if (info->opcode == WS_TEXT) {
                    String msg;
                    msg.reserve(len);
                    for (size_t i = 0; i < len; i++) {
                        msg += (char)data[i];
                    }
                    DEBUG_PRINT("WebSocket message: ");
                    DEBUG_PRINTLN(msg);

                    if (msg == "get_status") {
                        client->text(generateJSON());
                    } else if (msg == "get_spectrum") {
                        client->text(generateSpectrumJSON());
                    }
                }
            }
            break;
        }

        case WS_EVT_ERROR:
            DEBUG_PRINTLN("WebSocket error");
            break;

        case WS_EVT_PONG:
            break;
    }
}

String MotorWebServer::generateDashboardHTML() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Motor Vibration Monitor</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 20px;
            min-height: 100vh;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            background: white;
            border-radius: 15px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }
        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
        }
        .status-bar {
            display: flex;
            justify-content: space-around;
            padding: 20px;
            background: #f8f9fa;
            border-bottom: 2px solid #e9ecef;
        }
        .status-item {
            text-align: center;
        }
        .status-label {
            font-size: 0.9em;
            color: #6c757d;
            margin-bottom: 5px;
        }
        .status-value {
            font-size: 1.8em;
            font-weight: bold;
            color: #2c3e50;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
            gap: 20px;
            padding: 20px;
        }
        .card {
            background: white;
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            border: 1px solid #e9ecef;
        }
        .card h2 {
            color: #667eea;
            margin-bottom: 15px;
            font-size: 1.3em;
            border-bottom: 2px solid #667eea;
            padding-bottom: 10px;
        }
        .metric {
            display: flex;
            justify-content: space-between;
            padding: 10px 0;
            border-bottom: 1px solid #f1f3f5;
        }
        .metric:last-child {
            border-bottom: none;
        }
        .metric-label {
            color: #6c757d;
            font-weight: 500;
        }
        .metric-value {
            color: #2c3e50;
            font-weight: bold;
        }
        #spectrum-canvas {
            width: 100%;
            height: 300px;
            border: 1px solid #dee2e6;
            border-radius: 5px;
            background: #f8f9fa;
        }
        .alert {
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 15px;
            font-weight: 500;
        }
        .alert-success {
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .alert-warning {
            background: #fff3cd;
            color: #856404;
            border: 1px solid #ffeeba;
        }
        .alert-danger {
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        .btn {
            padding: 10px 20px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 1em;
            font-weight: 500;
            transition: all 0.3s;
            margin: 5px;
        }
        .btn-primary {
            background: #667eea;
            color: white;
        }
        .btn-primary:hover {
            background: #5568d3;
        }
        .btn-danger {
            background: #dc3545;
            color: white;
        }
        .btn-danger:hover {
            background: #c82333;
        }
        .controls {
            display: flex;
            justify-content: center;
            flex-wrap: wrap;
            padding: 20px;
            background: #f8f9fa;
            border-top: 2px solid #e9ecef;
        }
        .connection-status {
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 10px 20px;
            border-radius: 25px;
            font-weight: bold;
            box-shadow: 0 2px 10px rgba(0,0,0,0.2);
        }
        .connected {
            background: #28a745;
            color: white;
        }
        .disconnected {
            background: #dc3545;
            color: white;
        }
    </style>
</head>
<body>
    <div class="connection-status disconnected" id="connection-status">Disconnected</div>

    <div class="container">
        <div class="header">
            <h1>Motor Vibration Fault Detection</h1>
            <p>Real-time Monitoring Dashboard</p>
        </div>

        <div class="status-bar">
            <div class="status-item">
                <div class="status-label">System Status</div>
                <div class="status-value" id="system-status">INIT</div>
            </div>
            <div class="status-item">
                <div class="status-label">RMS (g)</div>
                <div class="status-value" id="rms-value">0.00</div>
            </div>
            <div class="status-item">
                <div class="status-label">Dominant Freq (Hz)</div>
                <div class="status-value" id="freq-value">0.0</div>
            </div>
            <div class="status-item">
                <div class="status-label">Clients</div>
                <div class="status-value" id="client-count">0</div>
            </div>
        </div>

        <div id="alert-container" style="padding: 20px;"></div>

        <div class="grid">
            <div class="card">
                <h2>Frequency Spectrum</h2>
                <canvas id="spectrum-canvas"></canvas>
            </div>

            <div class="card">
                <h2>Time-Domain Features</h2>
                <div class="metric">
                    <span class="metric-label">RMS</span>
                    <span class="metric-value" id="feature-rms">0.00</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Peak-to-Peak</span>
                    <span class="metric-value" id="feature-p2p">0.00</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Kurtosis</span>
                    <span class="metric-value" id="feature-kurtosis">0.00</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Skewness</span>
                    <span class="metric-value" id="feature-skewness">0.00</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Crest Factor</span>
                    <span class="metric-value" id="feature-crest">0.00</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Variance</span>
                    <span class="metric-value" id="feature-variance">0.00</span>
                </div>
            </div>

            <div class="card">
                <h2>Frequency-Domain Features</h2>
                <div class="metric">
                    <span class="metric-label">Spectral Centroid (Hz)</span>
                    <span class="metric-value" id="feature-centroid">0.0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Spectral Spread (Hz)</span>
                    <span class="metric-value" id="feature-spread">0.0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Band Power Ratio</span>
                    <span class="metric-value" id="feature-bpr">0.00</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Dominant Frequency (Hz)</span>
                    <span class="metric-value" id="feature-freq">0.0</span>
                </div>
            </div>

            <div class="card">
                <h2>Fault Detection</h2>
                <div class="metric">
                    <span class="metric-label">Fault Type</span>
                    <span class="metric-value" id="fault-type">NONE</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Severity</span>
                    <span class="metric-value" id="fault-severity">NORMAL</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Confidence</span>
                    <span class="metric-value" id="fault-confidence">0%</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Anomaly Score</span>
                    <span class="metric-value" id="fault-anomaly">0.00</span>
                </div>
                <div style="margin-top: 15px; padding: 10px; background: #f8f9fa; border-radius: 5px;">
                    <div class="metric-label" style="margin-bottom: 5px;">Description</div>
                    <div id="fault-description" style="color: #2c3e50;">No faults detected</div>
                </div>
            </div>

            <div class="card">
                <h2>Performance Metrics</h2>
                <div class="metric">
                    <span class="metric-label">Loop Time (ms)</span>
                    <span class="metric-value" id="perf-loop">0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Free Heap (KB)</span>
                    <span class="metric-value" id="perf-heap">0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">CPU Usage (%)</span>
                    <span class="metric-value" id="perf-cpu">0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Uptime (s)</span>
                    <span class="metric-value" id="perf-uptime">0</span>
                </div>
            </div>
        </div>

        <div class="controls">
            <button class="btn btn-primary" onclick="calibrate()">Start Calibration</button>
            <button class="btn btn-danger" onclick="reset()">Reset System</button>
            <button class="btn btn-primary" onclick="exportData('json')">Export JSON</button>
            <button class="btn btn-primary" onclick="exportData('csv')">Export CSV</button>
        </div>
    </div>

    <script>
        let ws;
        let spectrumChart;
        let canvas = document.getElementById('spectrum-canvas');
        let ctx = canvas.getContext('2d');

        function initWebSocket() {
            ws = new WebSocket('ws://' + window.location.hostname + '/ws');

            ws.onopen = function() {
                document.getElementById('connection-status').className = 'connection-status connected';
                document.getElementById('connection-status').textContent = 'Connected';
                console.log('WebSocket connected');
            };

            ws.onclose = function() {
                document.getElementById('connection-status').className = 'connection-status disconnected';
                document.getElementById('connection-status').textContent = 'Disconnected';
                console.log('WebSocket disconnected');
                setTimeout(initWebSocket, 3000);
            };

            ws.onmessage = function(event) {
                try {
                    let data = JSON.parse(event.data);
                    updateDashboard(data);
                } catch (e) {
                    console.error('Error parsing data:', e);
                }
            };
        }

        function updateDashboard(data) {
            if (data.features) {
                document.getElementById('rms-value').textContent = data.features.rms.toFixed(2);
                document.getElementById('freq-value').textContent = data.features.dominantFreq.toFixed(1);
                document.getElementById('feature-rms').textContent = data.features.rms.toFixed(4);
                document.getElementById('feature-p2p').textContent = data.features.peakToPeak.toFixed(4);
                document.getElementById('feature-kurtosis').textContent = data.features.kurtosis.toFixed(4);
                document.getElementById('feature-skewness').textContent = data.features.skewness.toFixed(4);
                document.getElementById('feature-crest').textContent = data.features.crestFactor.toFixed(4);
                document.getElementById('feature-variance').textContent = data.features.variance.toFixed(4);
                document.getElementById('feature-centroid').textContent = data.features.spectralCentroid.toFixed(2);
                document.getElementById('feature-spread').textContent = data.features.spectralSpread.toFixed(2);
                document.getElementById('feature-bpr').textContent = data.features.bandPowerRatio.toFixed(4);
                document.getElementById('feature-freq').textContent = data.features.dominantFreq.toFixed(2);
            }

            if (data.fault) {
                document.getElementById('fault-type').textContent = data.fault.type;
                document.getElementById('fault-severity').textContent = data.fault.severity;
                document.getElementById('fault-confidence').textContent = (data.fault.confidence * 100).toFixed(0) + '%';
                document.getElementById('fault-anomaly').textContent = data.fault.anomalyScore.toFixed(4);
                document.getElementById('fault-description').textContent = data.fault.description;

                let alertClass = data.fault.severity === 'CRITICAL' ? 'alert-danger' :
                               data.fault.severity === 'WARNING' ? 'alert-warning' : 'alert-success';

                if (data.fault.type !== 'NONE') {
                    showAlert(data.fault.description, alertClass);
                }
            }

            if (data.spectrum) {
                drawSpectrum(data.spectrum);
            }

            if (data.clients !== undefined) {
                document.getElementById('client-count').textContent = data.clients;
            }
        }

        function drawSpectrum(spectrum) {
            canvas.width = canvas.offsetWidth;
            canvas.height = canvas.offsetHeight;

            ctx.fillStyle = '#f8f9fa';
            ctx.fillRect(0, 0, canvas.width, canvas.height);

            let maxVal = Math.max(...spectrum);
            if (maxVal === 0) maxVal = 1;

            let barWidth = canvas.width / spectrum.length;

            for (let i = 0; i < spectrum.length; i++) {
                let barHeight = (spectrum[i] / maxVal) * canvas.height * 0.9;
                let x = i * barWidth;
                let y = canvas.height - barHeight;

                let hue = (i / spectrum.length) * 240;
                ctx.fillStyle = `hsl(${hue}, 70%, 50%)`;
                ctx.fillRect(x, y, barWidth - 1, barHeight);
            }

            ctx.strokeStyle = '#667eea';
            ctx.lineWidth = 2;
            ctx.beginPath();
            for (let i = 0; i < spectrum.length; i++) {
                let x = i * barWidth + barWidth / 2;
                let y = canvas.height - (spectrum[i] / maxVal) * canvas.height * 0.9;
                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.stroke();
        }

        function showAlert(message, className) {
            let container = document.getElementById('alert-container');
            let alert = document.createElement('div');
            alert.className = 'alert ' + className;
            alert.textContent = message;
            container.innerHTML = '';
            container.appendChild(alert);
        }

        function calibrate() {
            fetch('/api/calibrate', { method: 'POST' })
                .then(response => response.json())
                .then(data => {
                    showAlert('Calibration started...', 'alert-warning');
                });
        }

        function reset() {
            if (confirm('Are you sure you want to reset the system?')) {
                fetch('/api/reset', { method: 'POST' })
                    .then(response => response.json())
                    .then(data => {
                        showAlert('System reset initiated', 'alert-warning');
                    });
            }
        }

        function exportData(format) {
            window.open('/api/export?format=' + format, '_blank');
        }

        initWebSocket();
    </script>
</body>
</html>
)rawliteral";

    return html;
}

String MotorWebServer::generateConfigHTML() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Configuration - Motor Monitor</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 20px;
            min-height: 100vh;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background: white;
            border-radius: 15px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }
        .form-group {
            padding: 20px;
            border-bottom: 1px solid #e9ecef;
        }
        .form-group label {
            display: block;
            margin-bottom: 8px;
            color: #2c3e50;
            font-weight: 500;
        }
        .form-group input {
            width: 100%;
            padding: 10px;
            border: 1px solid #ced4da;
            border-radius: 5px;
            font-size: 1em;
        }
        .btn {
            padding: 12px 30px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 1em;
            font-weight: 500;
            margin: 20px;
        }
        .btn-primary {
            background: #667eea;
            color: white;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Configuration</h1>
        </div>
        <form id="config-form">
            <div class="form-group">
                <label>Broadcast Interval (ms)</label>
                <input type="number" name="broadcast_interval" value="100" min="50" max="5000">
            </div>
            <button type="submit" class="btn btn-primary">Save Configuration</button>
        </form>
    </div>
    <script>
        document.getElementById('config-form').addEventListener('submit', function(e) {
            e.preventDefault();
            let formData = new FormData(this);
            fetch('/api/config', {
                method: 'POST',
                body: formData
            }).then(response => response.json())
              .then(data => {
                  alert('Configuration saved!');
              });
        });
    </script>
</body>
</html>
)rawliteral";

    return html;
}

String MotorWebServer::generateJSON() {
    String json = "{";

    json += "\"features\":{";
    json += "\"rms\":" + String(latestFeatures.rms, 4) + ",";
    json += "\"peakToPeak\":" + String(latestFeatures.peakToPeak, 4) + ",";
    json += "\"kurtosis\":" + String(latestFeatures.kurtosis, 4) + ",";
    json += "\"skewness\":" + String(latestFeatures.skewness, 4) + ",";
    json += "\"crestFactor\":" + String(latestFeatures.crestFactor, 4) + ",";
    json += "\"variance\":" + String(latestFeatures.variance, 4) + ",";
    json += "\"spectralCentroid\":" + String(latestFeatures.spectralCentroid, 2) + ",";
    json += "\"spectralSpread\":" + String(latestFeatures.spectralSpread, 2) + ",";
    json += "\"bandPowerRatio\":" + String(latestFeatures.bandPowerRatio, 4) + ",";
    json += "\"dominantFreq\":" + String(latestFeatures.dominantFrequency, 2);
    json += "},";

    json += "\"fault\":{";
    json += "\"type\":\"" + String(latestFault.getFaultTypeName()) + "\",";
    json += "\"severity\":\"" + String(latestFault.getSeverityName()) + "\",";
    json += "\"confidence\":" + String(latestFault.confidence, 2) + ",";
    json += "\"anomalyScore\":" + String(latestFault.anomalyScore, 4) + ",";
    json += "\"description\":\"" + latestFault.description + "\"";
    json += "},";

    json += "\"spectrum\":[";
    for (size_t i = 0; i < spectrumLength; i++) {
        json += String(latestSpectrum[i], 2);
        if (i < spectrumLength - 1) json += ",";
    }
    json += "],";

    json += "\"clients\":" + String(clientCount) + ",";
    json += "\"uptime\":" + String(millis() / 1000);

    json += "}";

    return json;
}

String MotorWebServer::generateSpectrumJSON() {
    String json = "{\"spectrum\":[";
    for (size_t i = 0; i < spectrumLength; i++) {
        json += String(latestSpectrum[i], 2);
        if (i < spectrumLength - 1) json += ",";
    }
    json += "],\"length\":" + String(spectrumLength) + "}";
    return json;
}
