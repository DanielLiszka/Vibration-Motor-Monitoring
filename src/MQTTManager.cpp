#include "MQTTManager.h"
#include "DataExporter.h"

MQTTManager::MQTTManager()
    : wifiClient(nullptr)
    , mqttClient(nullptr)
    , brokerAddress(nullptr)
    , brokerPort(MQTT_DEFAULT_PORT)
    , clientIdentifier(nullptr)
    , mqttUsername(nullptr)
    , mqttPassword(nullptr)
    , lastReconnectAttempt(0)
    , lastPublishTime(0)
    , messageCount(0)
    , configured(false)
{
    wifiClient = new WiFiClient();
    mqttClient = new PubSubClient(*wifiClient);
    mqttClient->setBufferSize(MQTT_BUFFER_SIZE);
}

MQTTManager::~MQTTManager() {
    if (mqttClient) {
        mqttClient->disconnect();
        delete mqttClient;
    }
    if (wifiClient) {
        delete wifiClient;
    }
}

bool MQTTManager::begin(const char* broker, uint16_t port,
                       const char* clientId,
                       const char* username, const char* password) {
    DEBUG_PRINTLN("Initializing MQTT Manager...");

    brokerAddress = broker;
    brokerPort = port;
    clientIdentifier = clientId;
    mqttUsername = username;
    mqttPassword = password;

    mqttClient->setServer(brokerAddress, brokerPort);
    mqttClient->setKeepAlive(MQTT_KEEPALIVE_SECONDS);

    mqttClient->setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
        this->handleCommand(topic, payload, length);
    });

    configured = true;

    if (reconnect()) {
        DEBUG_PRINTLN("MQTT connected successfully");
        subscribe(TOPIC_COMMAND);
        return true;
    }

    DEBUG_PRINTLN("MQTT initial connection failed");
    return false;
}

void MQTTManager::loop() {
    if (!configured) return;

    if (!mqttClient->connected()) {
        uint32_t now = millis();
        if (now - lastReconnectAttempt >= MQTT_RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            if (reconnect()) {
                lastReconnectAttempt = 0;
                subscribe(TOPIC_COMMAND);
            }
        }
    } else {
        mqttClient->loop();
    }
}

bool MQTTManager::reconnect() {
    DEBUG_PRINT("Attempting MQTT connection to ");
    DEBUG_PRINT(brokerAddress);
    DEBUG_PRINT(":");
    DEBUG_PRINT(brokerPort);
    DEBUG_PRINT("...");

    bool connected;
    if (mqttUsername != nullptr && mqttPassword != nullptr) {
        connected = mqttClient->connect(clientIdentifier, mqttUsername, mqttPassword);
    } else {
        connected = mqttClient->connect(clientIdentifier);
    }

    if (connected) {
        DEBUG_PRINTLN(" connected!");
        publishStatus("online");
    } else {
        DEBUG_PRINT(" failed, rc=");
        DEBUG_PRINTLN(mqttClient->state());
    }

    return connected;
}

void MQTTManager::publishFeatures(const FeatureVector& features) {
    if (!isConnected()) return;

    DataExporter exporter;
    String json = exporter.exportFeatures(features, FORMAT_JSON);

    if (mqttClient->publish(TOPIC_FEATURES, json.c_str(), false)) {
        messageCount++;
        lastPublishTime = millis();
    }
}

void MQTTManager::publishFault(const FaultResult& fault) {
    if (!isConnected()) return;

    DataExporter exporter;
    String json = exporter.exportFault(fault, FORMAT_JSON);

    if (mqttClient->publish(TOPIC_FAULT, json.c_str(), true)) {
        messageCount++;
        lastPublishTime = millis();
    }
}

void MQTTManager::publishStatus(const char* status) {
    if (!isConnected()) return;

    String json = "{\"status\":\"" + String(status) + "\",";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"uptime\":" + String(millis() / 1000) + "}";

    if (mqttClient->publish(TOPIC_STATUS, json.c_str(), true)) {
        messageCount++;
        lastPublishTime = millis();
    }
}

void MQTTManager::publishSpectrum(const float* spectrum, size_t length) {
    if (!isConnected() || spectrum == nullptr) return;

    DataExporter exporter;
    String json = exporter.exportSpectrum(spectrum, length, FORMAT_JSON);

    if (mqttClient->publish(TOPIC_SPECTRUM, json.c_str(), false)) {
        messageCount++;
        lastPublishTime = millis();
    }
}

void MQTTManager::publishPerformance(const PerformanceMetrics& metrics) {
    if (!isConnected()) return;

    String json = "{";
    json += "\"loopTimeUs\":" + String(metrics.loopTime) + ",";
    json += "\"avgLoopTimeUs\":" + String(metrics.avgLoopTime) + ",";
    json += "\"maxLoopTimeUs\":" + String(metrics.maxLoopTime) + ",";
    json += "\"freeHeapBytes\":" + String(metrics.freeHeap) + ",";
    json += "\"minFreeHeapBytes\":" + String(metrics.minFreeHeap) + ",";
    json += "\"heapFragmentationPercent\":" + String(metrics.heapFragmentation) + ",";
    json += "\"cpuUsagePercent\":" + String(metrics.cpuUsage, 1) + ",";
    json += "\"samplesPerSecond\":" + String(metrics.samplesPerSecond, 2) + ",";
    json += "\"fftPerSecond\":" + String(metrics.fftPerSecond, 2) + ",";
    json += "\"detectionsPerSecond\":" + String(metrics.detectionsPerSecond, 2) + ",";
    json += "\"totalSamples\":" + String(metrics.totalSamples) + ",";
    json += "\"totalFFTs\":" + String(metrics.totalFFTs) + ",";
    json += "\"totalDetections\":" + String(metrics.totalDetections) + ",";
    json += "\"missedSamples\":" + String(metrics.missedSamples) + ",";
    json += "\"uptimeSeconds\":" + String(millis() / 1000);
    json += "}";

    if (mqttClient->publish(TOPIC_PERFORMANCE, json.c_str(), false)) {
        messageCount++;
        lastPublishTime = millis();
    }
}

void MQTTManager::publishAlert(const char* message, SeverityLevel severity) {
    if (!isConnected()) return;

    String severityStr;
    switch (severity) {
        case SEVERITY_NORMAL: severityStr = "NORMAL"; break;
        case SEVERITY_WARNING: severityStr = "WARNING"; break;
        case SEVERITY_CRITICAL: severityStr = "CRITICAL"; break;
        default: severityStr = "UNKNOWN"; break;
    }

    String json = "{";
    json += "\"message\":\"" + String(message) + "\",";
    json += "\"severity\":\"" + severityStr + "\",";
    json += "\"timestamp\":" + String(millis());
    json += "}";

    if (mqttClient->publish(TOPIC_ALERT, json.c_str(), true)) {
        messageCount++;
        lastPublishTime = millis();
    }
}

bool MQTTManager::subscribe(const char* topic) {
    if (!isConnected()) return false;

    DEBUG_PRINT("Subscribing to topic: ");
    DEBUG_PRINTLN(topic);

    return mqttClient->subscribe(topic, MQTT_QOS);
}

bool MQTTManager::unsubscribe(const char* topic) {
    if (!isConnected()) return false;

    DEBUG_PRINT("Unsubscribing from topic: ");
    DEBUG_PRINTLN(topic);

    return mqttClient->unsubscribe(topic);
}

void MQTTManager::setCallback(MQTT_CALLBACK_SIGNATURE) {
    mqttClient->setCallback(callback);
}

void MQTTManager::handleCommand(char* topic, uint8_t* payload, unsigned int length) {
    DEBUG_PRINT("Message arrived [");
    DEBUG_PRINT(topic);
    DEBUG_PRINT("] ");

    char message[MQTT_BUFFER_SIZE];
    if (length >= MQTT_BUFFER_SIZE) {
        length = MQTT_BUFFER_SIZE - 1;
    }
    memcpy(message, payload, length);
    message[length] = '\0';

    DEBUG_PRINTLN(message);

    String response = "{\"command\":\"" + String(message) + "\",";

    if (strcmp(topic, TOPIC_COMMAND) == 0) {
        if (strcmp(message, "calibrate") == 0) {
            response += "\"status\":\"calibration_started\"}";
        } else if (strcmp(message, "reset") == 0) {
            response += "\"status\":\"reset_initiated\"}";
        } else if (strcmp(message, "status") == 0) {
            response += "\"status\":\"ok\"}";
        } else {
            response += "\"status\":\"unknown_command\"}";
        }

        mqttClient->publish(TOPIC_RESPONSE, response.c_str(), false);
    }
}
