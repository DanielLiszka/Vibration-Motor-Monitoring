#include "AlertManager.h"
#include "MQTTManager.h"
#include "WebServer.h"

extern MQTTManager mqttMgr;
extern MotorWebServer webServer;

#define BUZZER_PIN 25

AlertManager::AlertManager()
    : enabledChannels(CHANNEL_ALL)
{
    memset(lastAlertTime, 0, sizeof(lastAlertTime));
}

AlertManager::~AlertManager() {
}

bool AlertManager::begin() {
    DEBUG_PRINTLN("Initializing Alert Manager...");
    alertHistory.reserve(MAX_ALERT_HISTORY);
    return true;
}

void AlertManager::raiseAlert(const Alert& alert) {
    if (!shouldSendAlert(alert.type)) {
        return;
    }

    alertHistory.push_back(alert);

    if (alertHistory.size() > MAX_ALERT_HISTORY) {
        alertHistory.erase(alertHistory.begin());
    }

    sendToChannels(alert);

    lastAlertTime[alert.type] = millis();

    DEBUG_PRINTF("Alert raised: %s - %s\n",
                 alert.message.c_str(), alert.details.c_str());
}

void AlertManager::raiseAlert(AlertType type, SeverityLevel severity,
                              const String& message, const String& details) {
    Alert alert(type, severity, message, details);
    raiseAlert(alert);
}

void AlertManager::acknowledgealert(size_t index) {
    if (index < alertHistory.size()) {
        alertHistory[index].acknowledged = true;
    }
}

void AlertManager::acknowledgeAll() {
    for (auto& alert : alertHistory) {
        alert.acknowledged = false;
    }
}

void AlertManager::clearAlert(size_t index) {
    if (index < alertHistory.size()) {
        alertHistory.erase(alertHistory.begin() + index);
    }
}

void AlertManager::clearAll() {
    alertHistory.clear();
}

std::vector<Alert> AlertManager::getActiveAlerts() {
    std::vector<Alert> active;
    for (const auto& alert : alertHistory) {
        if (!alert.acknowledged) {
            active.push_back(alert);
        }
    }
    return active;
}

std::vector<Alert> AlertManager::getAllAlerts() {
    return alertHistory;
}

size_t AlertManager::getActiveAlertCount() {
    size_t count = 0;
    for (const auto& alert : alertHistory) {
        if (!alert.acknowledged) {
            count++;
        }
    }
    return count;
}

void AlertManager::printActiveAlerts() {
    std::vector<Alert> active = getActiveAlerts();

    if (active.empty()) {
        Serial.println("No active alerts");
        return;
    }

    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.printf("║  ACTIVE ALERTS (%d)                           ║\n", active.size());
    Serial.println("╚════════════════════════════════════════════════╝\n");

    for (const auto& alert : active) {
        Serial.printf("[%lu] %s\n", alert.timestamp, alert.message.c_str());
        if (alert.details.length() > 0) {
            Serial.printf("     Details: %s\n", alert.details.c_str());
        }
    }

    Serial.println();
}

void AlertManager::sendToChannels(const Alert& alert) {
    if (isChannelEnabled(CHANNEL_SERIAL)) {
        sendToSerial(alert);
    }

    if (isChannelEnabled(CHANNEL_LED)) {
        sendToLED(alert);
    }

    if (isChannelEnabled(CHANNEL_BUZZER)) {
        sendToBuzzer(alert);
    }
}

void AlertManager::sendToSerial(const Alert& alert) {
    const char* severityStr = (alert.severity == SEVERITY_CRITICAL) ? "CRITICAL" :
                             (alert.severity == SEVERITY_WARNING) ? "WARNING" : "INFO";

    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.printf("║  ALERT - %s                              ║\n", severityStr);
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("Message: %s\n", alert.message.c_str());
    if (alert.details.length() > 0) {
        Serial.printf("Details: %s\n", alert.details.c_str());
    }
    Serial.printf("Time: %lu ms\n", alert.timestamp);
    Serial.println("════════════════════════════════════════════════\n");
}

void AlertManager::sendToMQTT(const Alert& alert) {
    if (mqttMgr.isConnected()) {
        String payload = "{\"type\":" + String((int)alert.type) +
                        ",\"severity\":" + String((int)alert.severity) +
                        ",\"message\":\"" + alert.message + "\"" +
                        ",\"details\":\"" + alert.details + "\"" +
                        ",\"timestamp\":" + String(alert.timestamp) + "}";
        mqttMgr.publishAlert(payload.c_str(), alert.severity);
    }
}

void AlertManager::sendToWeb(const Alert& alert) {
    if (webServer.isClientConnected()) {
        String json = "{\"type\":\"alert\",\"data\":{";
        json += "\"alertType\":" + String((int)alert.type) + ",";
        json += "\"severity\":" + String((int)alert.severity) + ",";
        json += "\"message\":\"" + alert.message + "\",";
        json += "\"details\":\"" + alert.details + "\",";
        json += "\"timestamp\":" + String(alert.timestamp);
        json += "}}";
        webServer.broadcastMessage(json);
    }
}

void AlertManager::sendToLED(const Alert& alert) {
    if (alert.severity >= SEVERITY_WARNING) {
        digitalWrite(LED_STATUS_PIN, HIGH);
    }
}

void AlertManager::sendToBuzzer(const Alert& alert) {
    pinMode(BUZZER_PIN, OUTPUT);

    if (alert.severity == SEVERITY_CRITICAL) {
        for (int i = 0; i < 3; i++) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(200);
            digitalWrite(BUZZER_PIN, LOW);
            delay(100);
        }
    } else if (alert.severity == SEVERITY_WARNING) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(300);
        digitalWrite(BUZZER_PIN, LOW);
    }
}

bool AlertManager::shouldSendAlert(AlertType type) {
    if (type >= 10) return true;

    uint32_t currentTime = millis();
    if (currentTime - lastAlertTime[type] < ALERT_REPEAT_INTERVAL_MS) {
        return false;
    }

    return true;
}
