#include "WiFiManager.h"

WiFiManager::WiFiManager()
    : wifiStatus(WIFI_DISCONNECTED)
    , mqttStatus(MQTT_STATUS_DISCONNECTED)
    , mqttClient(nullptr)
    , mqttPort(MQTT_PORT)
    , lastReconnectAttempt(0)
    , reconnectInterval(5000)
{
}

WiFiManager::~WiFiManager() {
    if (mqttClient != nullptr) {
        delete mqttClient;
    }
}

bool WiFiManager::begin() {
    DEBUG_PRINTLN("Initializing WiFi Manager...");

    if (!WIFI_ENABLED) {
        DEBUG_PRINTLN("WiFi disabled in config");
        return false;
    }

    mqttClient = new PubSubClient(wifiClient);

    if (!connectWiFi(WIFI_SSID, WIFI_PASSWORD)) {
        DEBUG_PRINTLN("WiFi connection failed");
        return false;
    }

    if (MQTT_ENABLED) {
        if (!connectMQTT(MQTT_BROKER, MQTT_PORT, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
            DEBUG_PRINTLN("MQTT connection failed (will retry)");
        }
    }

    DEBUG_PRINTLN("WiFi Manager initialized");
    return true;
}

bool WiFiManager::connectWiFi(const char* ssid, const char* password, uint32_t timeoutMs) {
    DEBUG_PRINTF("Connecting to WiFi: %s\n", ssid);

    wifiStatus = WIFI_CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > timeoutMs) {
            DEBUG_PRINTLN("WiFi connection timeout");
            wifiStatus = WIFI_ERROR;
            return false;
        }
        delay(500);
        DEBUG_PRINT(".");
    }

    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("WiFi connected!");
    DEBUG_PRINTF("IP address: %s\n", WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("Signal strength: %d dBm\n", WiFi.RSSI());

    wifiStatus = WIFI_CONNECTED;
    return true;
}

bool WiFiManager::connectMQTT(const char* broker, uint16_t port, const char* clientId,
                              const char* user, const char* password) {
    if (!isWiFiConnected()) {
        DEBUG_PRINTLN("WiFi not connected, cannot connect MQTT");
        return false;
    }

    DEBUG_PRINTF("Connecting to MQTT broker: %s:%d\n", broker, port);

    mqttBroker = String(broker);
    mqttPort = port;
    mqttClientId = String(clientId);
    mqttUser = String(user);
    mqttPassword = String(password);

    mqttClient->setServer(broker, port);

    mqttStatus = MQTT_STATUS_CONNECTING;

    bool connected = false;
    if (strlen(user) > 0 && strlen(password) > 0) {
        connected = mqttClient->connect(clientId, user, password);
    } else {
        connected = mqttClient->connect(clientId);
    }

    if (connected) {
        DEBUG_PRINTLN("MQTT connected!");
        mqttStatus = MQTT_STATUS_CONNECTED;

        publishStatus("Motor monitor online");

        return true;
    } else {
        DEBUG_PRINTF("MQTT connection failed, rc=%d\n", mqttClient->state());
        mqttStatus = MQTT_STATUS_ERROR;
        return false;
    }
}

void WiFiManager::disconnectWiFi() {
    WiFi.disconnect();
    wifiStatus = WIFI_DISCONNECTED;
    DEBUG_PRINTLN("WiFi disconnected");
}

void WiFiManager::disconnectMQTT() {
    if (mqttClient != nullptr && mqttClient->connected()) {
        publishStatus("Motor monitor offline");
        mqttClient->disconnect();
    }
    mqttStatus = MQTT_STATUS_DISCONNECTED;
    DEBUG_PRINTLN("MQTT disconnected");
}

void WiFiManager::loop() {

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiStatus == WIFI_CONNECTED) {
            DEBUG_PRINTLN("WiFi connection lost");
            wifiStatus = WIFI_DISCONNECTED;
        }

        if (millis() - lastReconnectAttempt > reconnectInterval) {
            reconnectWiFi();
            lastReconnectAttempt = millis();
        }
    } else {
        wifiStatus = WIFI_CONNECTED;
    }

    if (MQTT_ENABLED && isWiFiConnected()) {
        if (!mqttClient->connected()) {
            if (mqttStatus == MQTT_STATUS_CONNECTED) {
                DEBUG_PRINTLN("MQTT connection lost");
                mqttStatus = MQTT_STATUS_DISCONNECTED;
            }

            if (millis() - lastReconnectAttempt > reconnectInterval) {
                reconnectMQTT();
                lastReconnectAttempt = millis();
            }
        } else {
            mqttStatus = MQTT_STATUS_CONNECTED;
            mqttClient->loop();
        }
    }
}

bool WiFiManager::publishStatus(const String& message) {
    String payload = "{\"device\":\"" + mqttClientId + "\",\"status\":\"" + message + "\",\"timestamp\":" + String(millis()) + "}";
    return publish(MQTT_TOPIC_STATUS, payload);
}

bool WiFiManager::publishVibration(const FeatureVector& features) {
    String payload = featuresToJSON(features);
    return publish(MQTT_TOPIC_VIBRATION, payload);
}

bool WiFiManager::publishFault(const FaultResult& fault) {
    String payload = faultToJSON(fault);
    return publish(MQTT_TOPIC_FAULT, payload, true);
}

bool WiFiManager::publishFeatures(const FeatureVector& features) {
    String payload = featuresToJSON(features);
    return publish(MQTT_TOPIC_FEATURES, payload);
}

bool WiFiManager::subscribeToCommands(void (*callback)(char*, uint8_t*, unsigned int)) {
    if (!isMQTTConnected()) {
        return false;
    }

    mqttClient->setCallback(callback);
    return mqttClient->subscribe(MQTT_TOPIC_COMMAND);
}

int32_t WiFiManager::getWiFiRSSI() const {
    return WiFi.RSSI();
}

String WiFiManager::getIPAddress() const {
    return WiFi.localIP().toString();
}

bool WiFiManager::reconnectWiFi() {
    DEBUG_PRINTLN("Attempting WiFi reconnection...");
    return connectWiFi(WIFI_SSID, WIFI_PASSWORD);
}

bool WiFiManager::reconnectMQTT() {
    if (!isWiFiConnected()) {
        return false;
    }

    DEBUG_PRINTLN("Attempting MQTT reconnection...");
    return connectMQTT(mqttBroker.c_str(), mqttPort, mqttClientId.c_str(),
                      mqttUser.c_str(), mqttPassword.c_str());
}

bool WiFiManager::publish(const char* topic, const String& payload, bool retained) {
    if (!isMQTTConnected()) {
        return false;
    }

    bool success = mqttClient->publish(topic, payload.c_str(), retained);

    if (success) {
        DEBUG_PRINTF("Published to %s: %s\n", topic, payload.c_str());
    } else {
        DEBUG_PRINTF("Failed to publish to %s\n", topic);
    }

    return success;
}

String WiFiManager::featuresToJSON(const FeatureVector& features) {
    String json = "{";
    json += "\"device\":\"" + mqttClientId + "\",";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"rms\":" + String(features.rms, 4) + ",";
    json += "\"peakToPeak\":" + String(features.peakToPeak, 4) + ",";
    json += "\"kurtosis\":" + String(features.kurtosis, 4) + ",";
    json += "\"skewness\":" + String(features.skewness, 4) + ",";
    json += "\"crestFactor\":" + String(features.crestFactor, 4) + ",";
    json += "\"variance\":" + String(features.variance, 4) + ",";
    json += "\"spectralCentroid\":" + String(features.spectralCentroid, 2) + ",";
    json += "\"spectralSpread\":" + String(features.spectralSpread, 2) + ",";
    json += "\"bandPowerRatio\":" + String(features.bandPowerRatio, 4) + ",";
    json += "\"dominantFreq\":" + String(features.dominantFrequency, 2);
    json += "}";
    return json;
}

String WiFiManager::faultToJSON(const FaultResult& fault) {
    String json = "{";
    json += "\"device\":\"" + mqttClientId + "\",";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"type\":\"" + String(fault.getFaultTypeName()) + "\",";
    json += "\"severity\":\"" + String(fault.getSeverityName()) + "\",";
    json += "\"confidence\":" + String(fault.confidence, 2) + ",";
    json += "\"anomalyScore\":" + String(fault.anomalyScore, 4) + ",";
    json += "\"description\":\"" + fault.description + "\"";
    json += "}";
    return json;
}
