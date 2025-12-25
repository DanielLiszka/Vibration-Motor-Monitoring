#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "FaultDetector.h"

#define MAX_ALERT_HISTORY 50
#define ALERT_REPEAT_INTERVAL_MS 300000

enum AlertType {
    ALERT_FAULT = 0,
    ALERT_SYSTEM,
    ALERT_TREND,
    ALERT_FAULT_WARNING,
    ALERT_FAULT_CRITICAL,
    ALERT_SENSOR_FAILURE,
    ALERT_MEMORY_LOW,
    ALERT_WIFI_LOST,
    ALERT_CALIBRATION_INVALID,
    ALERT_SYSTEM_ERROR
};

enum AlertChannel {
    CHANNEL_SERIAL = 0x01,
    CHANNEL_MQTT = 0x02,
    CHANNEL_WEB = 0x04,
    CHANNEL_LED = 0x08,
    CHANNEL_BUZZER = 0x10,
    CHANNEL_ALL = 0xFF
};

struct Alert {
    AlertType type;
    SeverityLevel severity;
    String message;
    String details;
    uint32_t timestamp;
    bool acknowledged;
    uint8_t channels;

    Alert() : type(ALERT_FAULT_WARNING), severity(SEVERITY_NORMAL),
              message(""), details(""), timestamp(0),
              acknowledged(false), channels(CHANNEL_ALL) {}

    Alert(AlertType t, SeverityLevel s, const String& msg, const String& det = "",
          uint8_t ch = CHANNEL_ALL)
        : type(t), severity(s), message(msg), details(det),
          timestamp(millis()), acknowledged(false), channels(ch) {}
};

class AlertManager {
public:
    AlertManager();
    ~AlertManager();

    bool begin();

    void raiseAlert(const Alert& alert);
    void raiseAlert(AlertType type, SeverityLevel severity,
                   const String& message, const String& details = "");

    void acknowledgealert(size_t index);
    void acknowledgeAll();

    void clearAlert(size_t index);
    void clearAll();

    std::vector<Alert> getActiveAlerts();
    std::vector<Alert> getAllAlerts();

    size_t getAlertCount() const { return alertHistory.size(); }
    size_t getActiveAlertCount();

    void printActiveAlerts();

    void enableChannel(AlertChannel channel) { enabledChannels |= channel; }
    void disableChannel(AlertChannel channel) { enabledChannels &= ~channel; }
    bool isChannelEnabled(AlertChannel channel) { return enabledChannels & channel; }

private:
    std::vector<Alert> alertHistory;
    uint8_t enabledChannels;
    uint32_t lastAlertTime[10];

    void sendToChannels(const Alert& alert);
    void sendToSerial(const Alert& alert);
    void sendToMQTT(const Alert& alert);
    void sendToWeb(const Alert& alert);
    void sendToLED(const Alert& alert);
    void sendToBuzzer(const Alert& alert);

    bool shouldSendAlert(AlertType type);
};

#endif
