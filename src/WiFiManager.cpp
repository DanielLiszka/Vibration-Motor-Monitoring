#include "WiFiManager.h"

WiFiManager::WiFiManager()
    : wifiStatus(WIFI_DISCONNECTED)
    , mqttStatus(MQTT_STATUS_DISCONNECTED)
    , mqttClient(nullptr)
    , dnsServer(nullptr)
    , mqttPort(MQTT_PORT)
    , lastReconnectAttempt(0)
    , reconnectInterval(5000)
    , provisioningActive(false)
    , reconnectFailures(0)
{
}

WiFiManager::~WiFiManager() {
    if (mqttClient != nullptr) {
        delete mqttClient;
    }
    if (dnsServer != nullptr) {
        delete dnsServer;
    }
}

bool WiFiManager::begin() {
    return begin(WIFI_SSID, WIFI_PASSWORD, WIFI_TIMEOUT_MS);
}

bool WiFiManager::begin(const char* ssid, const char* password, uint32_t timeoutMs) {
    DEBUG_PRINTLN("Initializing WiFi Manager...");

    if (ssid == nullptr || ssid[0] == '\0') {
        DEBUG_PRINTLN("WiFi credentials missing, starting provisioning portal");
        return startProvisioningPortal();
    }

    if (strcmp(ssid, "your_ssid") == 0) {
        DEBUG_PRINTLN("Placeholder WiFi credentials detected, starting provisioning portal");
        wifiSsid = "";
        wifiPassword = "";
        return startProvisioningPortal();
    }

    wifiSsid = String(ssid);
    wifiPassword = String(password ? password : "");

    if (!connectWiFi(wifiSsid.c_str(), wifiPassword.c_str(), timeoutMs)) {
        DEBUG_PRINTLN("WiFi connection failed, provisioning portal active");
        return provisioningActive;
    }

    DEBUG_PRINTLN("WiFi Manager initialized");
    return true;
}

bool WiFiManager::connectWiFi(const char* ssid, const char* password, uint32_t timeoutMs) {
    if (ssid == nullptr || ssid[0] == '\0') {
        wifiStatus = WIFI_ERROR;
        return startProvisioningPortal();
    }

    DEBUG_PRINTF("Connecting to WiFi: %s\n", ssid);

    wifiStatus = WIFI_CONNECTING;
    WiFi.mode(provisioningActive ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(ssid, password);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > timeoutMs) {
            DEBUG_PRINTLN("WiFi connection timeout");
            wifiStatus = WIFI_ERROR;
            reconnectFailures++;
            if (!provisioningActive) {
                startProvisioningPortal();
            }
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
    reconnectFailures = 0;
    if (provisioningActive) {
        stopProvisioningPortal();
    }
    return true;
}

bool WiFiManager::startProvisioningPortal(const char* apName) {
    if (provisioningActive) {
        return true;
    }

    uint64_t chipId = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06llX", chipId & 0xFFFFFF);

    provisioningSsid = apName && apName[0]
        ? String(apName)
        : String("MotorMonitor-Setup-") + String(suffix);

    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(provisioningSsid.c_str())) {
        DEBUG_PRINTLN("Failed to start provisioning AP");
        return false;
    }

    if (dnsServer == nullptr) {
        dnsServer = new DNSServer();
    }

    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(53, "*", WiFi.softAPIP());

    provisioningActive = true;
    DEBUG_PRINTF("Provisioning AP active: %s (%s)\n",
                 provisioningSsid.c_str(),
                 WiFi.softAPIP().toString().c_str());
    return true;
}

void WiFiManager::stopProvisioningPortal() {
    if (dnsServer != nullptr) {
        dnsServer->stop();
    }

    if (provisioningActive) {
        WiFi.softAPdisconnect(true);
        provisioningActive = false;
        provisioningSsid = "";
        if (wifiStatus == WIFI_CONNECTED) {
            WiFi.mode(WIFI_STA);
        }
    }
}

bool WiFiManager::connectMQTT(const char* broker, uint16_t port, const char* clientId,
                              const char* user, const char* password) {
    if (!isWiFiConnected()) {
        DEBUG_PRINTLN("WiFi not connected, cannot connect MQTT");
        return false;
    }

    if (mqttClient == nullptr) {
        mqttClient = new PubSubClient(wifiClient);
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
    if (provisioningActive && dnsServer != nullptr) {
        dnsServer->processNextRequest();
    }

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
        if (provisioningActive) {
            stopProvisioningPortal();
        }
    }

    if (mqttClient != nullptr && isWiFiConnected() && mqttBroker.length() > 0) {
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
    if (isWiFiConnected()) {
        return WiFi.localIP().toString();
    }
    if (provisioningActive) {
        return WiFi.softAPIP().toString();
    }
    return String("0.0.0.0");
}

String WiFiManager::getProvisioningIP() const {
    if (!provisioningActive) {
        return String("");
    }
    return WiFi.softAPIP().toString();
}

bool WiFiManager::reconnectWiFi() {
    DEBUG_PRINTLN("Attempting WiFi reconnection...");
    return connectWiFi(wifiSsid.c_str(), wifiPassword.c_str());
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
