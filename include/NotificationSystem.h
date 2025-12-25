#ifndef NOTIFICATION_SYSTEM_H
#define NOTIFICATION_SYSTEM_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include "Config.h"
#include "FaultDetector.h"

#define MAX_WEBHOOKS 5
#define NOTIFICATION_RETRY_COUNT 3
#define NOTIFICATION_TIMEOUT 5000

enum NotificationType {
    NOTIFY_FAULT,
    NOTIFY_WARNING,
    NOTIFY_INFO,
    NOTIFY_CRITICAL,
    NOTIFY_MAINTENANCE
};

struct WebhookConfig {
    String url;
    String method;
    String authHeader;
    bool enabled;
};

struct NotificationHistory {
    NotificationType type;
    String message;
    uint32_t timestamp;
    bool delivered;
};

class NotificationSystem {
public:
    NotificationSystem();
    ~NotificationSystem();

    bool begin();

    bool addWebhook(const String& url, const String& method = "POST",
                    const String& authHeader = "");
    bool removeWebhook(size_t index);
    void enableWebhook(size_t index, bool enable);

    bool sendNotification(NotificationType type, const String& title,
                         const String& message, const String& details = "");

    bool sendWebhook(size_t webhookIndex, const String& payload);
    bool sendToAllWebhooks(const String& payload);

    std::vector<NotificationHistory> getHistory(size_t count = 10);
    size_t getDeliveredCount() const { return deliveredCount; }
    size_t getFailedCount() const { return failedCount; }

    String buildJSONPayload(NotificationType type, const String& title,
                           const String& message, const String& details);

private:
    std::vector<WebhookConfig> webhooks;
    std::vector<NotificationHistory> history;

    size_t deliveredCount;
    size_t failedCount;

    bool executeWebhook(const WebhookConfig& webhook, const String& payload);
    void addToHistory(NotificationType type, const String& message, bool delivered);
    String getNotificationTypeString(NotificationType type);
};

#endif
