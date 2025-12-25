#ifndef SYSTEM_HARDENING_H
#define SYSTEM_HARDENING_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "Config.h"

#define WATCHDOG_TIMEOUT_S 30
#define MIN_FREE_HEAP 32768
#define STACK_CHECK_INTERVAL 10000
#define MEMORY_CHECK_INTERVAL 5000
#define ERROR_LOG_SIZE 20

enum SystemError {
    ERR_NONE = 0,
    ERR_WATCHDOG_TIMEOUT,
    ERR_LOW_MEMORY,
    ERR_STACK_OVERFLOW,
    ERR_SENSOR_FAILURE,
    ERR_WIFI_FAILURE,
    ERR_MQTT_FAILURE,
    ERR_STORAGE_FAILURE,
    ERR_CONFIGURATION,
    ERR_ASSERTION,
    ERR_UNKNOWN
};

enum RecoveryAction {
    RECOVERY_NONE = 0,
    RECOVERY_RESTART_SERVICE,
    RECOVERY_SOFT_RESET,
    RECOVERY_HARD_RESET,
    RECOVERY_SAFE_MODE
};

struct ErrorRecord {
    SystemError error;
    uint32_t timestamp;
    uint32_t count;
    char details[64];
    RecoveryAction lastAction;
};

struct MemoryStats {
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t largestFreeBlock;
    uint32_t heapFragmentation;
    uint32_t freeStack;
    uint32_t psramFree;
    bool lowMemoryWarning;
};

struct TaskStats {
    const char* name;
    uint32_t stackHighWaterMark;
    uint32_t runTime;
    uint8_t priority;
    bool running;
};

typedef void (*ErrorCallback)(SystemError error, const char* details);
typedef void (*RecoveryCallback)(RecoveryAction action);

class SystemHardening {
public:
    SystemHardening();
    ~SystemHardening();

    bool begin();
    void loop();

    void enableWatchdog(uint32_t timeoutSeconds = WATCHDOG_TIMEOUT_S);
    void disableWatchdog();
    void feedWatchdog();
    bool isWatchdogEnabled() const { return watchdogEnabled; }

    MemoryStats getMemoryStats();
    void checkMemory();
    bool isMemoryCritical() const;
    void* safeAlloc(size_t size);
    void safeFree(void* ptr);
    void collectGarbage();

    void recordError(SystemError error, const char* details = "");
    void clearErrors();
    ErrorRecord* getLastError();
    ErrorRecord* getErrors(size_t& count);
    uint32_t getErrorCount() const { return errorCount; }

    void setErrorCallback(ErrorCallback callback) { errorCallback = callback; }
    void setRecoveryCallback(RecoveryCallback callback) { recoveryCallback = callback; }

    RecoveryAction determineRecoveryAction(SystemError error);
    void executeRecovery(RecoveryAction action);
    void enterSafeMode();
    bool isInSafeMode() const { return safeMode; }

    void assertCondition(bool condition, const char* message);
    void panic(const char* message);

    TaskStats getTaskStats(const char* taskName);
    void printSystemStatus();

    void enableAutoRecovery(bool enable) { autoRecoveryEnabled = enable; }
    void setRecoveryAttempts(uint8_t max) { maxRecoveryAttempts = max; }

    String generateDiagnosticsReport();
    bool saveCrashDump();
    bool loadLastCrashDump(String& output);

private:
    bool watchdogEnabled;
    bool safeMode;
    bool autoRecoveryEnabled;

    ErrorRecord errorLog[ERROR_LOG_SIZE];
    size_t errorIndex;
    uint32_t errorCount;

    uint32_t lastMemoryCheck;
    uint32_t lastStackCheck;
    MemoryStats lastMemoryStats;

    uint8_t recoveryAttempts;
    uint8_t maxRecoveryAttempts;
    uint32_t lastRecoveryTime;

    ErrorCallback errorCallback;
    RecoveryCallback recoveryCallback;

    void initWatchdog();
    void checkStack();
    void logError(SystemError error, const char* details);
    bool shouldAttemptRecovery();
    void resetRecoveryCounter();

    static void IRAM_ATTR watchdogISR();
    static SystemHardening* instance;
};

class CriticalSection {
public:
    CriticalSection();
    ~CriticalSection();

private:
    portMUX_TYPE mux;
};

class SafeBuffer {
public:
    SafeBuffer(size_t size);
    ~SafeBuffer();

    bool isValid() const { return buffer != nullptr; }
    uint8_t* data() { return buffer; }
    size_t size() const { return bufferSize; }

    void fill(uint8_t value);
    void zero();
    bool copy(const uint8_t* src, size_t len);

private:
    uint8_t* buffer;
    size_t bufferSize;
};

#endif
