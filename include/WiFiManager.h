#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"

enum WiFiStatus {
    WIFI_DISCONNECTED = 0,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_ERROR
};

enum MQTTStatus {
    MQTT_STATUS_DISCONNECTED = 0,
    MQTT_STATUS_CONNECTING,
    MQTT_STATUS_CONNECTED,
    MQTT_STATUS_ERROR
};

class WiFiManager {
public:

    WiFiManager();

    ~WiFiManager();

    bool begin();
    bool begin(const char* ssid, const char* password, uint32_t timeoutMs = WIFI_TIMEOUT_MS);

    bool connectWiFi(const char* ssid, const char* password, uint32_t timeoutMs = WIFI_TIMEOUT_MS);

    bool connectMQTT(const char* broker, uint16_t port, const char* clientId,
                     const char* user = "", const char* password = "");

    bool startProvisioningPortal(const char* apName = nullptr);
    void stopProvisioningPortal();

    void disconnectWiFi();

    void disconnectMQTT();

    void loop();

    bool publishStatus(const String& message);

    bool publishVibration(const FeatureVector& features);

    bool publishFault(const FaultResult& fault);

    bool publishFeatures(const FeatureVector& features);

    bool subscribeToCommands(void (*callback)(char*, uint8_t*, unsigned int));

    WiFiStatus getWiFiStatus() const { return wifiStatus; }

    MQTTStatus getMQTTStatus() const { return mqttStatus; }

    bool isWiFiConnected() const { return wifiStatus == WIFI_CONNECTED; }
    bool isProvisioningActive() const { return provisioningActive; }
    String getProvisioningSSID() const { return provisioningSsid; }
    String getProvisioningIP() const;

    bool isMQTTConnected() const { return mqttStatus == MQTT_STATUS_CONNECTED; }

    int32_t getWiFiRSSI() const;

    String getIPAddress() const;

private:
    WiFiClient wifiClient;
    PubSubClient* mqttClient;
    DNSServer* dnsServer;

    WiFiStatus wifiStatus;
    MQTTStatus mqttStatus;

    String wifiSsid;
    String wifiPassword;
    String provisioningSsid;
    String mqttBroker;
    uint16_t mqttPort;
    String mqttClientId;
    String mqttUser;
    String mqttPassword;

    uint32_t lastReconnectAttempt;
    uint32_t reconnectInterval;
    bool provisioningActive;
    uint8_t reconnectFailures;

    bool reconnectWiFi();

    bool reconnectMQTT();

    bool publish(const char* topic, const String& payload, bool retained = false);

    String featuresToJSON(const FeatureVector& features);

    String faultToJSON(const FaultResult& fault);
};

#endif
