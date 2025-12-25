#ifndef CLOUD_CONNECTOR_H
#define CLOUD_CONNECTOR_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"

#define CLOUD_MAX_MESSAGE_SIZE 2048
#define CLOUD_RECONNECT_INTERVAL 5000
#define CLOUD_KEEPALIVE 60
#define CLOUD_TIMEOUT 10000

enum CloudProvider {
    CLOUD_NONE = 0,
    CLOUD_AWS_IOT,
    CLOUD_AZURE_IOT,
    CLOUD_GENERIC_MQTT
};

enum CloudConnectionState {
    CLOUD_DISCONNECTED = 0,
    CLOUD_CONNECTING,
    CLOUD_CONNECTED,
    CLOUD_ERROR
};

struct CloudConfig {
    CloudProvider provider;
    char endpoint[128];
    uint16_t port;
    char deviceId[64];
    char username[64];
    char password[128];
    bool useTLS;
    const char* rootCA;
    const char* clientCert;
    const char* privateKey;
};

struct CloudMessage {
    String topic;
    String payload;
    uint8_t qos;
    bool retain;
    uint32_t timestamp;
};

struct CloudStats {
    uint32_t messagesPublished;
    uint32_t messagesReceived;
    uint32_t bytesPublished;
    uint32_t bytesReceived;
    uint32_t reconnects;
    uint32_t errors;
    uint32_t lastConnectTime;
    uint32_t lastPublishTime;
};

typedef void (*CloudMessageCallback)(const char* topic, const uint8_t* payload, size_t length);
typedef void (*CloudEventCallback)(CloudConnectionState state);

class CloudConnector {
public:
    CloudConnector();
    ~CloudConnector();

    bool begin(const CloudConfig& config);
    void loop();
    void disconnect();

    bool isConnected() const { return connectionState == CLOUD_CONNECTED; }
    CloudConnectionState getState() const { return connectionState; }
    CloudProvider getProvider() const { return currentConfig.provider; }

    bool publish(const char* topic, const char* payload, uint8_t qos = 0, bool retain = false);
    bool publishJSON(const char* topic, const String& json, uint8_t qos = 0);
    bool subscribe(const char* topic, uint8_t qos = 0);
    bool unsubscribe(const char* topic);

    void publishFeatures(const FeatureVector& features);
    void publishFault(const FaultResult& fault);
    void publishTelemetry(const String& telemetryJson);
    void publishAlert(const String& alertJson);
    void publishHeartbeat();

    // Continuous learning methods
    bool uploadTrainingData(const String& samplesJson);
    bool checkModelUpdate(String& availableVersion);
    bool downloadModel(const char* modelUrl, uint8_t* buffer, size_t maxSize, size_t& downloadedSize);
    bool requestLabels(const String& sampleIds);
    void subscribeModelUpdates();
    void subscribeLabelResponses();

    void setMessageCallback(CloudMessageCallback callback) { messageCallback = callback; }
    void setEventCallback(CloudEventCallback callback) { eventCallback = callback; }

    CloudStats getStats() const { return stats; }
    void resetStats();

    String getStatusJson() const;

protected:
    WiFiClientSecure* secureClient;
    WiFiClient* insecureClient;
    PubSubClient* mqttClient;

    CloudConfig currentConfig;
    CloudConnectionState connectionState;
    CloudStats stats;

    uint32_t lastReconnectAttempt;
    uint32_t lastHeartbeat;

    CloudMessageCallback messageCallback;
    CloudEventCallback eventCallback;

    virtual bool connect();
    virtual void handleMessage(const char* topic, const uint8_t* payload, size_t length);
    virtual String buildTelemetryTopic();
    virtual String buildAlertTopic();
    virtual String buildCommandTopic();

    void setConnectionState(CloudConnectionState state);
    void setupTLS();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static CloudConnector* instance;
};

class AWSIoTConnector : public CloudConnector {
public:
    AWSIoTConnector();

    void setThingName(const char* name) { strncpy(thingName, name, sizeof(thingName) - 1); }
    void setShadowEnabled(bool enabled) { shadowEnabled = enabled; }

    bool publishShadowUpdate(const String& stateJson);
    bool publishShadowGet();
    void subscribeShadowTopics();

protected:
    bool connect() override;
    String buildTelemetryTopic() override;
    String buildAlertTopic() override;
    String buildCommandTopic() override;

private:
    char thingName[64];
    bool shadowEnabled;
};

class AzureIoTConnector : public CloudConnector {
public:
    AzureIoTConnector();

    void setIoTHubName(const char* name) { strncpy(iotHubName, name, sizeof(iotHubName) - 1); }
    void setSASToken(const char* token) { strncpy(sasToken, token, sizeof(sasToken) - 1); }

    bool sendDeviceToCloud(const String& payload);
    void subscribeCloudToDevice();
    void subscribeTwinUpdates();

protected:
    bool connect() override;
    String buildTelemetryTopic() override;
    String buildAlertTopic() override;
    String buildCommandTopic() override;

private:
    char iotHubName[64];
    char sasToken[256];
    uint32_t sasTokenExpiry;

    String generateSASToken(uint32_t expirySeconds);
};

class GenericMQTTConnector : public CloudConnector {
public:
    GenericMQTTConnector();

    void setTopicPrefix(const char* prefix) { strncpy(topicPrefix, prefix, sizeof(topicPrefix) - 1); }
    void setWillMessage(const char* topic, const char* message);

protected:
    bool connect() override;
    String buildTelemetryTopic() override;
    String buildAlertTopic() override;
    String buildCommandTopic() override;

private:
    char topicPrefix[64];
    char willTopic[128];
    char willMessage[64];
};

#endif
