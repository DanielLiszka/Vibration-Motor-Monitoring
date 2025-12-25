#include "NotificationSystem.h"

NotificationSystem::NotificationSystem()
    : deliveredCount(0)
    , failedCount(0)
{
}

NotificationSystem::~NotificationSystem() {
    webhooks.clear();
    history.clear();
}

bool NotificationSystem::begin() {
    DEBUG_PRINTLN("Initializing Notification System...");
    deliveredCount = 0;
    failedCount = 0;
    return true;
}

bool NotificationSystem::addWebhook(const String& url, const String& method,
                                    const String& authHeader) {
    if (webhooks.size() >= MAX_WEBHOOKS) {
        DEBUG_PRINTLN("Maximum webhooks reached");
        return false;
    }

    WebhookConfig config;
    config.url = url;
    config.method = method;
    config.authHeader = authHeader;
    config.enabled = true;

    webhooks.push_back(config);

    DEBUG_PRINT("Added webhook: ");
    DEBUG_PRINTLN(url);

    return true;
}

bool NotificationSystem::removeWebhook(size_t index) {
    if (index >= webhooks.size()) {
        return false;
    }

    webhooks.erase(webhooks.begin() + index);
    return true;
}

void NotificationSystem::enableWebhook(size_t index, bool enable) {
    if (index < webhooks.size()) {
        webhooks[index].enabled = enable;
    }
}

bool NotificationSystem::sendNotification(NotificationType type, const String& title,
                                         const String& message, const String& details) {
    String payload = buildJSONPayload(type, title, message, details);

    bool success = sendToAllWebhooks(payload);

    addToHistory(type, title + ": " + message, success);

    return success;
}

bool NotificationSystem::sendWebhook(size_t webhookIndex, const String& payload) {
    if (webhookIndex >= webhooks.size()) {
        return false;
    }

    const WebhookConfig& webhook = webhooks[webhookIndex];

    if (!webhook.enabled) {
        return false;
    }

    return executeWebhook(webhook, payload);
}

bool NotificationSystem::sendToAllWebhooks(const String& payload) {
    if (webhooks.empty()) {
        DEBUG_PRINTLN("No webhooks configured");
        return false;
    }

    bool anySuccess = false;

    for (const auto& webhook : webhooks) {
        if (!webhook.enabled) {
            continue;
        }

        if (executeWebhook(webhook, payload)) {
            anySuccess = true;
        }
    }

    return anySuccess;
}

std::vector<NotificationHistory> NotificationSystem::getHistory(size_t count) {
    std::vector<NotificationHistory> result;

    size_t start = 0;
    if (history.size() > count) {
        start = history.size() - count;
    }

    for (size_t i = start; i < history.size(); i++) {
        result.push_back(history[i]);
    }

    return result;
}

String NotificationSystem::buildJSONPayload(NotificationType type, const String& title,
                                           const String& message, const String& details) {
    String json = "{";
    json += "\"type\":\"" + getNotificationTypeString(type) + "\",";
    json += "\"title\":\"" + title + "\",";
    json += "\"message\":\"" + message + "\",";
    json += "\"details\":\"" + details + "\",";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"device\":\"" + String(DEVICE_ID) + "\"";
    json += "}";

    return json;
}

bool NotificationSystem::executeWebhook(const WebhookConfig& webhook, const String& payload) {
    HTTPClient http;

    DEBUG_PRINT("Sending webhook to: ");
    DEBUG_PRINTLN(webhook.url);

    http.begin(webhook.url);
    http.setTimeout(NOTIFICATION_TIMEOUT);

    if (webhook.authHeader.length() > 0) {
        http.addHeader("Authorization", webhook.authHeader);
    }

    http.addHeader("Content-Type", "application/json");

    int retryCount = 0;
    int httpCode = -1;

    while (retryCount < NOTIFICATION_RETRY_COUNT && httpCode < 200) {
        if (webhook.method == "POST") {
            httpCode = http.POST(payload);
        } else if (webhook.method == "PUT") {
            httpCode = http.PUT(payload);
        } else if (webhook.method == "GET") {
            httpCode = http.GET();
        }

        if (httpCode >= 200 && httpCode < 300) {
            DEBUG_PRINT("Webhook successful: ");
            DEBUG_PRINTLN(httpCode);
            deliveredCount++;
            http.end();
            return true;
        }

        retryCount++;
        delay(500);
    }

    DEBUG_PRINT("Webhook failed: ");
    DEBUG_PRINTLN(httpCode);
    failedCount++;

    http.end();
    return false;
}

void NotificationSystem::addToHistory(NotificationType type, const String& message, bool delivered) {
    NotificationHistory entry;
    entry.type = type;
    entry.message = message;
    entry.timestamp = millis();
    entry.delivered = delivered;

    history.push_back(entry);

    if (history.size() > 100) {
        history.erase(history.begin());
    }
}

String NotificationSystem::getNotificationTypeString(NotificationType type) {
    switch (type) {
        case NOTIFY_FAULT: return "fault";
        case NOTIFY_WARNING: return "warning";
        case NOTIFY_INFO: return "info";
        case NOTIFY_CRITICAL: return "critical";
        case NOTIFY_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}
