#ifndef MODBUS_SERVER_H
#define MODBUS_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"

#define MODBUS_TCP_PORT 502
#define MODBUS_RTU_BAUD 9600
#define MODBUS_RTU_CONFIG SERIAL_8N1
#define MODBUS_UNIT_ID 1
#define MODBUS_MAX_REGISTERS 100
#define MODBUS_MAX_COILS 32

enum ModbusRegisterMap {
    REG_RMS = 0,
    REG_PEAK_TO_PEAK = 2,
    REG_KURTOSIS = 4,
    REG_SKEWNESS = 6,
    REG_CREST_FACTOR = 8,
    REG_VARIANCE = 10,
    REG_SPECTRAL_CENTROID = 12,
    REG_SPECTRAL_SPREAD = 14,
    REG_BAND_POWER_RATIO = 16,
    REG_DOMINANT_FREQ = 18,

    REG_FAULT_TYPE = 20,
    REG_FAULT_SEVERITY = 21,
    REG_FAULT_CONFIDENCE = 22,

    REG_CPU_USAGE = 30,
    REG_FREE_HEAP = 32,
    REG_UPTIME_LOW = 34,
    REG_UPTIME_HIGH = 35,
    REG_WIFI_RSSI = 36,
    REG_SAMPLE_RATE = 37,

    REG_TEMP_X10 = 40,
    REG_HUMIDITY_X10 = 41,

    REG_ALERT_COUNT = 50,
    REG_LAST_ALERT_TYPE = 51,
    REG_LAST_ALERT_SEVERITY = 52,

    REG_DEVICE_STATUS = 60,
    REG_ERROR_CODE = 61,

    REG_SPECTRUM_START = 70
};

enum ModbusCoilMap {
    COIL_ENABLE_MONITORING = 0,
    COIL_ENABLE_ALERTS = 1,
    COIL_ENABLE_LOGGING = 2,
    COIL_CALIBRATE = 3,
    COIL_RESET = 4,
    COIL_FAULT_DETECTED = 10,
    COIL_WIFI_CONNECTED = 11,
    COIL_MQTT_CONNECTED = 12,
    COIL_SD_AVAILABLE = 13,
    COIL_SENSOR_OK = 14
};

enum ModbusFunctionCode {
    FC_READ_COILS = 0x01,
    FC_READ_DISCRETE_INPUTS = 0x02,
    FC_READ_HOLDING_REGISTERS = 0x03,
    FC_READ_INPUT_REGISTERS = 0x04,
    FC_WRITE_SINGLE_COIL = 0x05,
    FC_WRITE_SINGLE_REGISTER = 0x06,
    FC_WRITE_MULTIPLE_COILS = 0x0F,
    FC_WRITE_MULTIPLE_REGISTERS = 0x10
};

struct ModbusFrame {
    uint8_t unitId;
    uint8_t functionCode;
    uint16_t startAddress;
    uint16_t quantity;
    uint8_t* data;
    size_t dataLength;
    uint16_t crc;
};

class ModbusServer {
public:
    ModbusServer();
    ~ModbusServer();

    bool begin(bool enableTcp = true, bool enableRtu = false);
    void loop();
    void stop();

    void updateFeatures(const FeatureVector& features);
    void updateFault(const FaultResult& fault);
    void updateSystemStatus(uint8_t cpuUsage, uint32_t freeHeap, int8_t rssi);
    void updateSpectrum(const float* spectrum, size_t length);

    void setCoil(uint8_t coil, bool value);
    bool getCoil(uint8_t coil) const;
    void setRegister(uint16_t address, uint16_t value);
    uint16_t getRegister(uint16_t address) const;

    void setFloatRegister(uint16_t address, float value);
    float getFloatRegister(uint16_t address) const;

    void setCalibrationCallback(void (*callback)()) { calibrationCallback = callback; }
    void setResetCallback(void (*callback)()) { resetCallback = callback; }

    bool isClientConnected() const { return tcpClientConnected; }
    uint32_t getRequestCount() const { return requestCount; }
    uint32_t getErrorCount() const { return errorCount; }

private:
    WiFiServer* tcpServer;
    WiFiClient tcpClient;
    HardwareSerial* rtuSerial;

    uint16_t holdingRegisters[MODBUS_MAX_REGISTERS];
    uint16_t inputRegisters[MODBUS_MAX_REGISTERS];
    uint8_t coils[MODBUS_MAX_COILS / 8 + 1];
    uint8_t discreteInputs[MODBUS_MAX_COILS / 8 + 1];

    bool tcpEnabled;
    bool rtuEnabled;
    bool tcpClientConnected;

    uint32_t requestCount;
    uint32_t errorCount;
    uint32_t lastActivityTime;

    void (*calibrationCallback)();
    void (*resetCallback)();

    void handleTcpClient();
    void handleRtuRequest();

    bool parseFrame(uint8_t* buffer, size_t length, ModbusFrame& frame);
    size_t buildResponse(const ModbusFrame& request, uint8_t* response);
    size_t buildErrorResponse(uint8_t functionCode, uint8_t exceptionCode, uint8_t* response);

    size_t handleReadCoils(const ModbusFrame& request, uint8_t* response);
    size_t handleReadDiscreteInputs(const ModbusFrame& request, uint8_t* response);
    size_t handleReadHoldingRegisters(const ModbusFrame& request, uint8_t* response);
    size_t handleReadInputRegisters(const ModbusFrame& request, uint8_t* response);
    size_t handleWriteSingleCoil(const ModbusFrame& request, uint8_t* response);
    size_t handleWriteSingleRegister(const ModbusFrame& request, uint8_t* response);
    size_t handleWriteMultipleCoils(const ModbusFrame& request, uint8_t* response);
    size_t handleWriteMultipleRegisters(const ModbusFrame& request, uint8_t* response);

    uint16_t calculateCRC(uint8_t* buffer, size_t length);
    void setBit(uint8_t* array, uint16_t bit, bool value);
    bool getBit(const uint8_t* array, uint16_t bit) const;

    void processCoilWrite(uint16_t coil, bool value);
    void processRegisterWrite(uint16_t address, uint16_t value);
};

#endif
