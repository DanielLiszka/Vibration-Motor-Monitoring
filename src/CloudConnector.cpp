#include "CloudConnector.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

CloudConnector* CloudConnector::instance = nullptr;

CloudConnector::CloudConnector()
    : secureClient(nullptr)
    , insecureClient(nullptr)
    , mqttClient(nullptr)
    , connectionState(CLOUD_DISCONNECTED)
    , lastReconnectAttempt(0)
    , lastHeartbeat(0)
    , lastModelCheckRequest(0)
    , modelUpdateAvailable(false)
    , messageCallback(nullptr)
    , eventCallback(nullptr)
{
    memset(&currentConfig, 0, sizeof(currentConfig));
    memset(&stats, 0, sizeof(stats));
    memset(pendingModelVersion, 0, sizeof(pendingModelVersion));
    memset(pendingModelUrl, 0, sizeof(pendingModelUrl));
    instance = this;
}

CloudConnector::~CloudConnector() {
    disconnect();
    if (mqttClient) delete mqttClient;
    if (secureClient) delete secureClient;
    if (insecureClient) delete insecureClient;
    instance = nullptr;
}

bool CloudConnector::begin(const CloudConfig& config) {
    DEBUG_PRINTLN("Initializing Cloud Connector...");

    currentConfig = config;
    clearPendingModelUpdate();
    lastModelCheckRequest = 0;

    if (config.useTLS) {
        secureClient = new WiFiClientSecure();
        setupTLS();
        mqttClient = new PubSubClient(*secureClient);
    } else {
        insecureClient = new WiFiClient();
        mqttClient = new PubSubClient(*insecureClient);
    }

    mqttClient->setServer(config.endpoint, config.port);
    mqttClient->setCallback(mqttCallback);
    mqttClient->setBufferSize(CLOUD_MAX_MESSAGE_SIZE);
    mqttClient->setKeepAlive(CLOUD_KEEPALIVE);

    DEBUG_PRINT("Cloud provider: ");
    switch (config.provider) {
        case CLOUD_AWS_IOT:
            DEBUG_PRINTLN("AWS IoT Core");
            break;
        case CLOUD_AZURE_IOT:
            DEBUG_PRINTLN("Azure IoT Hub");
            break;
        case CLOUD_GENERIC_MQTT:
            DEBUG_PRINTLN("Generic MQTT");
            break;
        default:
            DEBUG_PRINTLN("Unknown");
    }

    return connect();
}

void CloudConnector::clearPendingModelUpdate() {
    pendingModelVersion[0] = '\0';
    pendingModelUrl[0] = '\0';
    modelUpdateAvailable = false;
}

String CloudConnector::buildTopicRoot() {
    String telemetry = buildTelemetryTopic();
    while (telemetry.endsWith("/")) {
        telemetry.remove(telemetry.length() - 1);
    }

    int lastSlash = telemetry.lastIndexOf('/');
    if (lastSlash < 0) return telemetry;
    return telemetry.substring(0, lastSlash);
}

void CloudConnector::loop() {
    if (!mqttClient) return;

    if (connectionState == CLOUD_CONNECTED) {
        if (!mqttClient->connected()) {
            setConnectionState(CLOUD_DISCONNECTED);
            stats.reconnects++;
        } else {
            mqttClient->loop();
        }
    }

    if (connectionState == CLOUD_DISCONNECTED) {
        uint32_t now = millis();
        if (now - lastReconnectAttempt >= CLOUD_RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            connect();
        }
    }

    if (connectionState == CLOUD_CONNECTED && millis() - lastHeartbeat > 60000) {
        publishHeartbeat();
        lastHeartbeat = millis();
    }
}

void CloudConnector::disconnect() {
    if (mqttClient && mqttClient->connected()) {
        mqttClient->disconnect();
    }
    setConnectionState(CLOUD_DISCONNECTED);
}

bool CloudConnector::connect() {
    if (!mqttClient) return false;

    setConnectionState(CLOUD_CONNECTING);

    DEBUG_PRINT("Connecting to cloud endpoint: ");
    DEBUG_PRINTLN(currentConfig.endpoint);

    bool connected = false;

    if (strlen(currentConfig.username) > 0) {
        connected = mqttClient->connect(
            currentConfig.deviceId,
            currentConfig.username,
            currentConfig.password
        );
    } else {
        connected = mqttClient->connect(currentConfig.deviceId);
    }

    if (connected) {
        setConnectionState(CLOUD_CONNECTED);
        stats.lastConnectTime = millis();
        DEBUG_PRINTLN("Cloud connected successfully");

        String commandTopic = buildCommandTopic();
        if (commandTopic.length() > 0) {
            subscribe(commandTopic.c_str(), 1);
        }

        return true;
    } else {
        setConnectionState(CLOUD_ERROR);
        stats.errors++;
        DEBUG_PRINT("Cloud connection failed, rc=");
        DEBUG_PRINTLN(mqttClient->state());
        return false;
    }
}

bool CloudConnector::publish(const char* topic, const char* payload, uint8_t qos, bool retain) {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return false;

    bool success = mqttClient->publish(topic, payload, retain);

    if (success) {
        stats.messagesPublished++;
        stats.bytesPublished += strlen(payload);
        stats.lastPublishTime = millis();
    } else {
        stats.errors++;
    }

    return success;
}

bool CloudConnector::publishJSON(const char* topic, const String& json, uint8_t qos) {
    return publish(topic, json.c_str(), qos, false);
}

bool CloudConnector::subscribe(const char* topic, uint8_t qos) {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return false;

    bool success = mqttClient->subscribe(topic, qos);
    DEBUG_PRINT("Subscribed to: ");
    DEBUG_PRINTLN(topic);

    return success;
}

bool CloudConnector::unsubscribe(const char* topic) {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return false;
    return mqttClient->unsubscribe(topic);
}

void CloudConnector::publishFeatures(const FeatureVector& features) {
    String topic = buildTelemetryTopic();
    String json = "{\"type\":\"features\",\"data\":{";
    json += "\"rms\":" + String(features.rms, 4) + ",";
    json += "\"peakToPeak\":" + String(features.peakToPeak, 4) + ",";
    json += "\"kurtosis\":" + String(features.kurtosis, 4) + ",";
    json += "\"skewness\":" + String(features.skewness, 4) + ",";
    json += "\"crestFactor\":" + String(features.crestFactor, 4) + ",";
    json += "\"variance\":" + String(features.variance, 4) + ",";
    json += "\"spectralCentroid\":" + String(features.spectralCentroid, 2) + ",";
    json += "\"spectralSpread\":" + String(features.spectralSpread, 2) + ",";
    json += "\"bandPowerRatio\":" + String(features.bandPowerRatio, 4) + ",";
    json += "\"dominantFrequency\":" + String(features.dominantFrequency, 2);
    json += "},\"timestamp\":" + String(features.timestamp) + "}";

    publishJSON(topic.c_str(), json, 0);
}

void CloudConnector::publishFault(const FaultResult& fault) {
    String topic = buildAlertTopic();
    String json = "{\"type\":\"fault\",\"data\":{";
    json += "\"faultType\":" + String((int)fault.type) + ",";
    json += "\"severity\":" + String((int)fault.severity) + ",";
    json += "\"confidence\":" + String(fault.confidence, 4) + ",";
    json += "\"description\":\"" + fault.description + "\"";
    json += "},\"timestamp\":" + String(millis()) + "}";

    publishJSON(topic.c_str(), json, 1);
}

void CloudConnector::publishTelemetry(const String& telemetryJson) {
    String topic = buildTelemetryTopic();
    publishJSON(topic.c_str(), telemetryJson, 0);
}

void CloudConnector::publishAlert(const String& alertJson) {
    String topic = buildAlertTopic();
    publishJSON(topic.c_str(), alertJson, 1);
}

void CloudConnector::publishHeartbeat() {
    String topic = buildTelemetryTopic();
    String json = "{\"type\":\"heartbeat\",\"data\":{";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "},\"timestamp\":" + String(millis()) + "}";

    publishJSON(topic.c_str(), json, 0);
}

void CloudConnector::handleMessage(const char* topic, const uint8_t* payload, size_t length) {
    stats.messagesReceived++;
    stats.bytesReceived += length;

    if (topic && payload && length > 0) {
        const String root = buildTopicRoot();
        const String modelUpdateTopic = root + "/models/update";

        if (String(topic) == modelUpdateTopic) {
            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (!err) {
                const char* version = doc["version"] | doc["model_version"] | "";
                const char* url = doc["url"] | doc["model_url"] | "";

                if (version && version[0] && url && url[0]) {
                    strncpy(pendingModelVersion, version, sizeof(pendingModelVersion) - 1);
                    strncpy(pendingModelUrl, url, sizeof(pendingModelUrl) - 1);
                    modelUpdateAvailable = true;
                }
            }
        }
    }

    if (messageCallback) {
        messageCallback(topic, payload, length);
    }
}

void CloudConnector::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (instance) {
        instance->handleMessage(topic, payload, length);
    }
}

void CloudConnector::setConnectionState(CloudConnectionState state) {
    if (connectionState != state) {
        connectionState = state;
        if (eventCallback) {
            eventCallback(state);
        }
    }
}

void CloudConnector::setupTLS() {
    if (!secureClient) return;

    if (currentConfig.rootCA) {
        secureClient->setCACert(currentConfig.rootCA);
    }
    if (currentConfig.clientCert) {
        secureClient->setCertificate(currentConfig.clientCert);
    }
    if (currentConfig.privateKey) {
        secureClient->setPrivateKey(currentConfig.privateKey);
    }
}

void CloudConnector::resetStats() {
    memset(&stats, 0, sizeof(stats));
}

String CloudConnector::getStatusJson() const {
    String json = "{\"cloud\":{";
    json += "\"connected\":" + String(connectionState == CLOUD_CONNECTED ? "true" : "false") + ",";
    json += "\"provider\":" + String((int)currentConfig.provider) + ",";
    json += "\"endpoint\":\"" + String(currentConfig.endpoint) + "\",";
    json += "\"stats\":{";
    json += "\"published\":" + String(stats.messagesPublished) + ",";
    json += "\"received\":" + String(stats.messagesReceived) + ",";
    json += "\"errors\":" + String(stats.errors) + ",";
    json += "\"reconnects\":" + String(stats.reconnects);
    json += "}}}";
    return json;
}

String CloudConnector::buildTelemetryTopic() {
    return String(currentConfig.deviceId) + "/telemetry";
}

String CloudConnector::buildAlertTopic() {
    return String(currentConfig.deviceId) + "/alerts";
}

String CloudConnector::buildCommandTopic() {
    return String(currentConfig.deviceId) + "/commands";
}

bool CloudConnector::uploadTrainingData(const String& samplesJson) {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return false;
    if (samplesJson.length() == 0) return false;

    const String topic = buildTopicRoot() + "/training";
    return publishJSON(topic.c_str(), samplesJson, 1);
}

bool CloudConnector::checkModelUpdate(String& availableVersion) {
    availableVersion = "";

    if (modelUpdateAvailable) {
        availableVersion = String(pendingModelVersion);
        return true;
    }

    if (!mqttClient || connectionState != CLOUD_CONNECTED) return false;

    const uint32_t now = millis();
    if (lastModelCheckRequest != 0 && (now - lastModelCheckRequest) < 60000) {
        return false;
    }
    lastModelCheckRequest = now;

    StaticJsonDocument<256> doc;
    doc["device_id"] = currentConfig.deviceId;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["timestamp"] = now;
    doc["type"] = "model_check";

    String payload;
    serializeJson(doc, payload);

    const String topic = buildTopicRoot() + "/models/check";
    publishJSON(topic.c_str(), payload, 1);

    return false;
}

bool CloudConnector::downloadModel(const char* modelUrl, uint8_t* buffer, size_t maxSize, size_t& downloadedSize) {
    downloadedSize = 0;
    if (!modelUrl || !buffer || maxSize == 0) return false;

    HTTPClient http;
    http.begin(modelUrl);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    if (contentLength > 0 && (size_t)contentLength > maxSize) {
        http.end();
        return false;
    }

    uint32_t start = millis();
    while (http.connected() && (contentLength < 0 || downloadedSize < (size_t)contentLength)) {
        size_t available = stream->available();
        if (available > 0) {
            size_t toRead = available;
            if (contentLength > 0) {
                toRead = min(toRead, (size_t)contentLength - downloadedSize);
            }
            toRead = min(toRead, maxSize - downloadedSize);

            if (toRead == 0) break;

            size_t read = stream->readBytes(buffer + downloadedSize, toRead);
            downloadedSize += read;
        } else {
            if (millis() - start > CLOUD_TIMEOUT) break;
            delay(1);
        }
        yield();
    }

    http.end();

    if (contentLength > 0) {
        return downloadedSize == (size_t)contentLength;
    }
    return downloadedSize > 0;
}

bool CloudConnector::requestLabels(const String& sampleIds) {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return false;
    if (sampleIds.length() == 0) return false;

    String payload = "{\"device_id\":\"";
    payload += String(currentConfig.deviceId);
    payload += "\",\"timestamp\":";
    payload += String(millis());
    payload += ",\"sample_ids\":";

    if (sampleIds.startsWith("[")) {
        payload += sampleIds;
    } else {
        payload += "[\"";
        payload += sampleIds;
        payload += "\"]";
    }

    payload += "}";

    const String topic = buildTopicRoot() + "/labels/request";
    return publishJSON(topic.c_str(), payload, 1);
}

void CloudConnector::subscribeModelUpdates() {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return;

    const String topic = buildTopicRoot() + "/models/update";
    subscribe(topic.c_str(), 1);
}

void CloudConnector::subscribeLabelResponses() {
    if (!mqttClient || connectionState != CLOUD_CONNECTED) return;

    const String topic = buildTopicRoot() + "/labels/response";
    subscribe(topic.c_str(), 1);
}

AWSIoTConnector::AWSIoTConnector()
    : shadowEnabled(false)
{
    memset(thingName, 0, sizeof(thingName));
}

bool AWSIoTConnector::connect() {
    if (!mqttClient) return false;

    setConnectionState(CLOUD_CONNECTING);

    DEBUG_PRINTLN("Connecting to AWS IoT Core...");

    bool connected = mqttClient->connect(currentConfig.deviceId);

    if (connected) {
        setConnectionState(CLOUD_CONNECTED);
        stats.lastConnectTime = millis();
        DEBUG_PRINTLN("AWS IoT connected");

        if (shadowEnabled) {
            subscribeShadowTopics();
        }

        String commandTopic = buildCommandTopic();
        subscribe(commandTopic.c_str(), 1);

        return true;
    } else {
        setConnectionState(CLOUD_ERROR);
        stats.errors++;
        return false;
    }
}

String AWSIoTConnector::buildTelemetryTopic() {
    return String("$aws/things/") + thingName + "/telemetry";
}

String AWSIoTConnector::buildAlertTopic() {
    return String("$aws/things/") + thingName + "/alerts";
}

String AWSIoTConnector::buildCommandTopic() {
    return String("$aws/things/") + thingName + "/commands";
}

bool AWSIoTConnector::publishShadowUpdate(const String& stateJson) {
    String topic = String("$aws/things/") + thingName + "/shadow/update";
    return publishJSON(topic.c_str(), stateJson, 1);
}

bool AWSIoTConnector::publishShadowGet() {
    String topic = String("$aws/things/") + thingName + "/shadow/get";
    return publish(topic.c_str(), "", 0, false);
}

void AWSIoTConnector::subscribeShadowTopics() {
    String baseTopic = String("$aws/things/") + thingName + "/shadow/";

    subscribe((baseTopic + "update/accepted").c_str(), 1);
    subscribe((baseTopic + "update/rejected").c_str(), 1);
    subscribe((baseTopic + "update/delta").c_str(), 1);
    subscribe((baseTopic + "get/accepted").c_str(), 1);
    subscribe((baseTopic + "get/rejected").c_str(), 1);
}

AzureIoTConnector::AzureIoTConnector()
    : sasTokenExpiry(0)
{
    memset(iotHubName, 0, sizeof(iotHubName));
    memset(sasToken, 0, sizeof(sasToken));
}

bool AzureIoTConnector::connect() {
    if (!mqttClient) return false;

    setConnectionState(CLOUD_CONNECTING);

    DEBUG_PRINTLN("Connecting to Azure IoT Hub...");

    String username = String(iotHubName) + ".azure-devices.net/" +
                     currentConfig.deviceId + "/?api-version=2021-04-12";

    bool connected = mqttClient->connect(
        currentConfig.deviceId,
        username.c_str(),
        sasToken
    );

    if (connected) {
        setConnectionState(CLOUD_CONNECTED);
        stats.lastConnectTime = millis();
        DEBUG_PRINTLN("Azure IoT Hub connected");

        subscribeCloudToDevice();
        subscribeTwinUpdates();

        return true;
    } else {
        setConnectionState(CLOUD_ERROR);
        stats.errors++;
        return false;
    }
}

String AzureIoTConnector::buildTelemetryTopic() {
    return String("devices/") + currentConfig.deviceId + "/messages/events/";
}

String AzureIoTConnector::buildAlertTopic() {
    return String("devices/") + currentConfig.deviceId + "/messages/events/$.ct=application%2Fjson&$.ce=utf-8&priority=high";
}

String AzureIoTConnector::buildCommandTopic() {
    return String("devices/") + currentConfig.deviceId + "/messages/devicebound/#";
}

bool AzureIoTConnector::sendDeviceToCloud(const String& payload) {
    String topic = buildTelemetryTopic();
    return publishJSON(topic.c_str(), payload, 1);
}

void AzureIoTConnector::subscribeCloudToDevice() {
    String topic = String("devices/") + currentConfig.deviceId + "/messages/devicebound/#";
    subscribe(topic.c_str(), 1);
}

void AzureIoTConnector::subscribeTwinUpdates() {
    subscribe("$iothub/twin/PATCH/properties/desired/#", 1);
    subscribe("$iothub/twin/res/#", 1);
}

String AzureIoTConnector::generateSASToken(uint32_t expirySeconds) {
    return String(sasToken);
}

GenericMQTTConnector::GenericMQTTConnector() {
    memset(topicPrefix, 0, sizeof(topicPrefix));
    memset(willTopic, 0, sizeof(willTopic));
    memset(willMessage, 0, sizeof(willMessage));
    strcpy(topicPrefix, "vibesentry");
}

bool GenericMQTTConnector::connect() {
    if (!mqttClient) return false;

    setConnectionState(CLOUD_CONNECTING);

    DEBUG_PRINTLN("Connecting to MQTT broker...");

    bool connected;
    if (strlen(willTopic) > 0) {
        connected = mqttClient->connect(
            currentConfig.deviceId,
            currentConfig.username,
            currentConfig.password,
            willTopic,
            1,
            true,
            willMessage
        );
    } else if (strlen(currentConfig.username) > 0) {
        connected = mqttClient->connect(
            currentConfig.deviceId,
            currentConfig.username,
            currentConfig.password
        );
    } else {
        connected = mqttClient->connect(currentConfig.deviceId);
    }

    if (connected) {
        setConnectionState(CLOUD_CONNECTED);
        stats.lastConnectTime = millis();
        DEBUG_PRINTLN("MQTT connected");

        String commandTopic = buildCommandTopic();
        subscribe(commandTopic.c_str(), 1);

        return true;
    } else {
        setConnectionState(CLOUD_ERROR);
        stats.errors++;
        return false;
    }
}

String GenericMQTTConnector::buildTelemetryTopic() {
    return String(topicPrefix) + "/" + currentConfig.deviceId + "/telemetry";
}

String GenericMQTTConnector::buildAlertTopic() {
    return String(topicPrefix) + "/" + currentConfig.deviceId + "/alerts";
}

String GenericMQTTConnector::buildCommandTopic() {
    return String(topicPrefix) + "/" + currentConfig.deviceId + "/commands";
}

void GenericMQTTConnector::setWillMessage(const char* topic, const char* message) {
    strncpy(willTopic, topic, sizeof(willTopic) - 1);
    strncpy(willMessage, message, sizeof(willMessage) - 1);
}
