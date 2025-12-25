#include "SystemHardening.h"
#include "StorageManager.h"

#define CRASH_DUMP_FILE "/crash_dump.txt"

SystemHardening* SystemHardening::instance = nullptr;

SystemHardening::SystemHardening()
    : watchdogEnabled(false)
    , safeMode(false)
    , autoRecoveryEnabled(true)
    , errorIndex(0)
    , errorCount(0)
    , lastMemoryCheck(0)
    , lastStackCheck(0)
    , recoveryAttempts(0)
    , maxRecoveryAttempts(3)
    , lastRecoveryTime(0)
    , errorCallback(nullptr)
    , recoveryCallback(nullptr)
{
    memset(errorLog, 0, sizeof(errorLog));
    memset(&lastMemoryStats, 0, sizeof(lastMemoryStats));
    instance = this;
}

SystemHardening::~SystemHardening() {
    if (watchdogEnabled) {
        disableWatchdog();
    }
    instance = nullptr;
}

bool SystemHardening::begin() {
    DEBUG_PRINTLN("Initializing System Hardening...");

    enableWatchdog(WATCHDOG_TIMEOUT_S);

    lastMemoryStats = getMemoryStats();

    DEBUG_PRINTLN("System Hardening initialized");
    return true;
}

void SystemHardening::loop() {
    feedWatchdog();

    uint32_t now = millis();

    if (now - lastMemoryCheck >= MEMORY_CHECK_INTERVAL) {
        checkMemory();
        lastMemoryCheck = now;
    }

    if (now - lastStackCheck >= STACK_CHECK_INTERVAL) {
        checkStack();
        lastStackCheck = now;
    }

    if (now - lastRecoveryTime > 300000) {
        resetRecoveryCounter();
    }
}

void SystemHardening::enableWatchdog(uint32_t timeoutSeconds) {
    if (watchdogEnabled) return;

    esp_task_wdt_init(timeoutSeconds, true);
    esp_task_wdt_add(NULL);
    watchdogEnabled = true;

    DEBUG_PRINT("Watchdog enabled with ");
    DEBUG_PRINT(timeoutSeconds);
    DEBUG_PRINTLN("s timeout");
}

void SystemHardening::disableWatchdog() {
    if (!watchdogEnabled) return;

    esp_task_wdt_delete(NULL);
    watchdogEnabled = false;

    DEBUG_PRINTLN("Watchdog disabled");
}

void SystemHardening::feedWatchdog() {
    if (watchdogEnabled) {
        esp_task_wdt_reset();
    }
}

MemoryStats SystemHardening::getMemoryStats() {
    MemoryStats stats;

    stats.freeHeap = ESP.getFreeHeap();
    stats.minFreeHeap = ESP.getMinFreeHeap();
    stats.largestFreeBlock = ESP.getMaxAllocHeap();

    if (stats.freeHeap > 0 && stats.largestFreeBlock > 0) {
        stats.heapFragmentation = 100 - ((stats.largestFreeBlock * 100) / stats.freeHeap);
    } else {
        stats.heapFragmentation = 0;
    }

    stats.freeStack = uxTaskGetStackHighWaterMark(NULL);

    stats.psramFree = 0;
#ifdef BOARD_HAS_PSRAM
    stats.psramFree = ESP.getFreePsram();
#endif

    stats.lowMemoryWarning = stats.freeHeap < MIN_FREE_HEAP;

    return stats;
}

void SystemHardening::checkMemory() {
    lastMemoryStats = getMemoryStats();

    if (lastMemoryStats.lowMemoryWarning) {
        recordError(ERR_LOW_MEMORY, "Free heap below threshold");

        collectGarbage();

        if (autoRecoveryEnabled && shouldAttemptRecovery()) {
            RecoveryAction action = determineRecoveryAction(ERR_LOW_MEMORY);
            executeRecovery(action);
        }
    }

    if (lastMemoryStats.heapFragmentation > 70) {
        DEBUG_PRINTLN("Warning: High heap fragmentation");
    }
}

void SystemHardening::checkStack() {
    uint32_t stackFree = uxTaskGetStackHighWaterMark(NULL);

    if (stackFree < 1024) {
        recordError(ERR_STACK_OVERFLOW, "Stack space critically low");

        if (autoRecoveryEnabled) {
            executeRecovery(RECOVERY_SOFT_RESET);
        }
    }
}

bool SystemHardening::isMemoryCritical() const {
    return lastMemoryStats.freeHeap < (MIN_FREE_HEAP / 2);
}

void* SystemHardening::safeAlloc(size_t size) {
    if (size == 0 || isMemoryCritical()) {
        return nullptr;
    }

    void* ptr = malloc(size);

    if (!ptr) {
        recordError(ERR_LOW_MEMORY, "Allocation failed");
        collectGarbage();
        ptr = malloc(size);
    }

    return ptr;
}

void SystemHardening::safeFree(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

void SystemHardening::collectGarbage() {
    DEBUG_PRINTLN("Running garbage collection...");

#ifdef BOARD_HAS_PSRAM
#endif
}

void SystemHardening::recordError(SystemError error, const char* details) {
    logError(error, details);

    if (errorCallback) {
        errorCallback(error, details);
    }

    DEBUG_PRINT("System Error: ");
    DEBUG_PRINT((int)error);
    DEBUG_PRINT(" - ");
    DEBUG_PRINTLN(details);
}

void SystemHardening::logError(SystemError error, const char* details) {
    for (size_t i = 0; i < ERROR_LOG_SIZE; i++) {
        if (errorLog[i].error == error) {
            errorLog[i].count++;
            errorLog[i].timestamp = millis();
            if (details) {
                strncpy(errorLog[i].details, details, sizeof(errorLog[i].details) - 1);
            }
            return;
        }
    }

    errorLog[errorIndex].error = error;
    errorLog[errorIndex].timestamp = millis();
    errorLog[errorIndex].count = 1;
    errorLog[errorIndex].lastAction = RECOVERY_NONE;
    if (details) {
        strncpy(errorLog[errorIndex].details, details, sizeof(errorLog[errorIndex].details) - 1);
    } else {
        errorLog[errorIndex].details[0] = '\0';
    }

    errorIndex = (errorIndex + 1) % ERROR_LOG_SIZE;
    errorCount++;
}

void SystemHardening::clearErrors() {
    memset(errorLog, 0, sizeof(errorLog));
    errorIndex = 0;
    errorCount = 0;
}

ErrorRecord* SystemHardening::getLastError() {
    if (errorCount == 0) return nullptr;

    size_t lastIdx = (errorIndex == 0) ? ERROR_LOG_SIZE - 1 : errorIndex - 1;
    return &errorLog[lastIdx];
}

ErrorRecord* SystemHardening::getErrors(size_t& count) {
    count = (errorCount < ERROR_LOG_SIZE) ? errorCount : ERROR_LOG_SIZE;
    return errorLog;
}

RecoveryAction SystemHardening::determineRecoveryAction(SystemError error) {
    switch (error) {
        case ERR_LOW_MEMORY:
            if (recoveryAttempts < 2) return RECOVERY_RESTART_SERVICE;
            return RECOVERY_SOFT_RESET;

        case ERR_STACK_OVERFLOW:
            return RECOVERY_SOFT_RESET;

        case ERR_SENSOR_FAILURE:
            if (recoveryAttempts < maxRecoveryAttempts) return RECOVERY_RESTART_SERVICE;
            return RECOVERY_SAFE_MODE;

        case ERR_WIFI_FAILURE:
        case ERR_MQTT_FAILURE:
            if (recoveryAttempts < 3) return RECOVERY_RESTART_SERVICE;
            return RECOVERY_SOFT_RESET;

        case ERR_STORAGE_FAILURE:
            return RECOVERY_SOFT_RESET;

        case ERR_WATCHDOG_TIMEOUT:
            return RECOVERY_HARD_RESET;

        case ERR_ASSERTION:
            return RECOVERY_SAFE_MODE;

        default:
            return RECOVERY_NONE;
    }
}

void SystemHardening::executeRecovery(RecoveryAction action) {
    if (!shouldAttemptRecovery()) {
        DEBUG_PRINTLN("Max recovery attempts reached");
        enterSafeMode();
        return;
    }

    recoveryAttempts++;
    lastRecoveryTime = millis();

    if (recoveryCallback) {
        recoveryCallback(action);
    }

    DEBUG_PRINT("Executing recovery action: ");
    DEBUG_PRINTLN((int)action);

    switch (action) {
        case RECOVERY_RESTART_SERVICE:
            break;

        case RECOVERY_SOFT_RESET:
            saveCrashDump();
            delay(100);
            ESP.restart();
            break;

        case RECOVERY_HARD_RESET:
            saveCrashDump();
            delay(100);
            esp_restart();
            break;

        case RECOVERY_SAFE_MODE:
            enterSafeMode();
            break;

        case RECOVERY_NONE:
        default:
            break;
    }
}

void SystemHardening::enterSafeMode() {
    DEBUG_PRINTLN("Entering safe mode...");
    safeMode = true;
}

bool SystemHardening::shouldAttemptRecovery() {
    return recoveryAttempts < maxRecoveryAttempts;
}

void SystemHardening::resetRecoveryCounter() {
    recoveryAttempts = 0;
}

void SystemHardening::assertCondition(bool condition, const char* message) {
    if (!condition) {
        recordError(ERR_ASSERTION, message);

        DEBUG_PRINT("Assertion failed: ");
        DEBUG_PRINTLN(message);

        if (autoRecoveryEnabled) {
            executeRecovery(RECOVERY_SAFE_MODE);
        }
    }
}

void SystemHardening::panic(const char* message) {
    DEBUG_PRINT("PANIC: ");
    DEBUG_PRINTLN(message);

    recordError(ERR_UNKNOWN, message);
    saveCrashDump();

    delay(1000);
    ESP.restart();
}

TaskStats SystemHardening::getTaskStats(const char* taskName) {
    TaskStats stats;
    stats.name = taskName;
    stats.stackHighWaterMark = 0;
    stats.runTime = 0;
    stats.priority = 0;
    stats.running = false;

    return stats;
}

void SystemHardening::printSystemStatus() {
    MemoryStats mem = getMemoryStats();

    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║           SYSTEM STATUS REPORT                 ║");
    Serial.println("╚════════════════════════════════════════════════╝\n");

    Serial.printf("Free Heap:          %u bytes\n", mem.freeHeap);
    Serial.printf("Min Free Heap:      %u bytes\n", mem.minFreeHeap);
    Serial.printf("Largest Block:      %u bytes\n", mem.largestFreeBlock);
    Serial.printf("Fragmentation:      %u%%\n", mem.heapFragmentation);
    Serial.printf("Stack Free:         %u words\n", mem.freeStack);
    Serial.printf("Uptime:             %lu ms\n", millis());
    Serial.printf("Watchdog:           %s\n", watchdogEnabled ? "Enabled" : "Disabled");
    Serial.printf("Safe Mode:          %s\n", safeMode ? "Yes" : "No");
    Serial.printf("Error Count:        %lu\n", errorCount);
    Serial.printf("Recovery Attempts:  %u/%u\n", recoveryAttempts, maxRecoveryAttempts);

    Serial.println("\n════════════════════════════════════════════════\n");
}

String SystemHardening::generateDiagnosticsReport() {
    MemoryStats mem = getMemoryStats();

    String report = "{\"diagnostics\":{";
    report += "\"timestamp\":" + String(millis()) + ",";
    report += "\"memory\":{";
    report += "\"freeHeap\":" + String(mem.freeHeap) + ",";
    report += "\"minFreeHeap\":" + String(mem.minFreeHeap) + ",";
    report += "\"largestBlock\":" + String(mem.largestFreeBlock) + ",";
    report += "\"fragmentation\":" + String(mem.heapFragmentation);
    report += "},";
    report += "\"status\":{";
    report += "\"watchdogEnabled\":" + String(watchdogEnabled ? "true" : "false") + ",";
    report += "\"safeMode\":" + String(safeMode ? "true" : "false") + ",";
    report += "\"errorCount\":" + String(errorCount) + ",";
    report += "\"recoveryAttempts\":" + String(recoveryAttempts);
    report += "},";
    report += "\"errors\":[";

    bool first = true;
    for (size_t i = 0; i < ERROR_LOG_SIZE; i++) {
        if (errorLog[i].count > 0) {
            if (!first) report += ",";
            first = false;
            report += "{\"type\":" + String((int)errorLog[i].error);
            report += ",\"count\":" + String(errorLog[i].count);
            report += ",\"timestamp\":" + String(errorLog[i].timestamp);
            report += ",\"details\":\"" + String(errorLog[i].details) + "\"}";
        }
    }

    report += "]}}";
    return report;
}

bool SystemHardening::saveCrashDump() {
    StorageManager storage;
    if (!storage.begin()) return false;

    String dump = "Crash Dump - " + String(millis()) + "\n";
    dump += "Free Heap: " + String(ESP.getFreeHeap()) + "\n";
    dump += "Min Free Heap: " + String(ESP.getMinFreeHeap()) + "\n";
    dump += "Safe Mode: " + String(safeMode ? "Yes" : "No") + "\n";
    dump += "Error Count: " + String(errorCount) + "\n\n";

    dump += "Recent Errors:\n";
    for (size_t i = 0; i < ERROR_LOG_SIZE; i++) {
        if (errorLog[i].count > 0) {
            dump += "  Error " + String((int)errorLog[i].error);
            dump += " (x" + String(errorLog[i].count) + ")";
            dump += ": " + String(errorLog[i].details) + "\n";
        }
    }

    return storage.saveLog(CRASH_DUMP_FILE, dump);
}

bool SystemHardening::loadLastCrashDump(String& output) {
    StorageManager storage;
    if (!storage.begin()) return false;

    output = storage.readLog(CRASH_DUMP_FILE);
    return output.length() > 0;
}

void IRAM_ATTR SystemHardening::watchdogISR() {
    if (instance) {
        instance->recordError(ERR_WATCHDOG_TIMEOUT, "WDT ISR triggered");
    }
}

CriticalSection::CriticalSection() {
    portENTER_CRITICAL(&mux);
}

CriticalSection::~CriticalSection() {
    portEXIT_CRITICAL(&mux);
}

SafeBuffer::SafeBuffer(size_t size) : buffer(nullptr), bufferSize(0) {
    if (size > 0 && size < 65536) {
        buffer = (uint8_t*)malloc(size);
        if (buffer) {
            bufferSize = size;
            memset(buffer, 0, size);
        }
    }
}

SafeBuffer::~SafeBuffer() {
    if (buffer) {
        memset(buffer, 0, bufferSize);
        free(buffer);
    }
}

void SafeBuffer::fill(uint8_t value) {
    if (buffer) {
        memset(buffer, value, bufferSize);
    }
}

void SafeBuffer::zero() {
    fill(0);
}

bool SafeBuffer::copy(const uint8_t* src, size_t len) {
    if (!buffer || !src || len > bufferSize) return false;
    memcpy(buffer, src, len);
    return true;
}
