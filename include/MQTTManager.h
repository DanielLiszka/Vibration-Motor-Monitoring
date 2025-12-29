#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"
#include "PerformanceMonitor.h"

#define MQTT_DEFAULT_PORT 1883
#define MQTT_KEEPALIVE_SECONDS 60
#define MQTT_QOS 1
#define MQTT_BUFFER_SIZE 512
#define MQTT_RECONNECT_INTERVAL 5000

#define TOPIC_FEATURES "motor/features"
#define TOPIC_FAULT "motor/fault"
#define TOPIC_STATUS "motor/status"
#define TOPIC_SPECTRUM "motor/spectrum"
#define TOPIC_PERFORMANCE "motor/performance"
#define TOPIC_ALERT "motor/alert"
#define TOPIC_COMMAND "motor/command"
#define TOPIC_RESPONSE "motor/response"

class MQTTManager {
public:
    MQTTManager();
    ~MQTTManager();

    bool begin(const char* broker, uint16_t port = MQTT_DEFAULT_PORT,
               const char* clientId = "motor-vibration-monitor",
               const char* username = nullptr, const char* password = nullptr);

    void loop();

    bool isConnected() const { return mqttClient->connected(); }

    void publishFeatures(const FeatureVector& features);
    void publishFault(const FaultResult& fault);
    void publishStatus(const char* status);
    void publishSpectrum(const float* spectrum, size_t length);
    void publishPerformance(const PerformanceMetrics& metrics);
    void publishAlert(const char* message, SeverityLevel severity);

    bool subscribe(const char* topic);
    bool unsubscribe(const char* topic);

    void setCallback(MQTT_CALLBACK_SIGNATURE);

    uint32_t getMessageCount() const { return messageCount; }
    uint32_t getLastPublishTime() const { return lastPublishTime; }

private:
    WiFiClient* wifiClient;
    PubSubClient* mqttClient;

    const char* brokerAddress;
    uint16_t brokerPort;
    const char* clientIdentifier;
    const char* mqttUsername;
    const char* mqttPassword;

    uint32_t lastReconnectAttempt;
    uint32_t lastPublishTime;
    uint32_t messageCount;
    bool configured;

    bool reconnect();
    void handleCommand(char* topic, uint8_t* payload, unsigned int length);
};

#endif
