#include "ModbusServer.h"

#define MODBUS_TCP_HEADER_SIZE 7
#define MODBUS_RTU_MIN_SIZE 4
#define MODBUS_TIMEOUT_MS 1000

ModbusServer::ModbusServer()
    : tcpServer(nullptr)
    , rtuSerial(nullptr)
    , tcpEnabled(false)
    , rtuEnabled(false)
    , tcpClientConnected(false)
    , requestCount(0)
    , errorCount(0)
    , lastActivityTime(0)
    , calibrationCallback(nullptr)
    , resetCallback(nullptr)
{
    memset(holdingRegisters, 0, sizeof(holdingRegisters));
    memset(inputRegisters, 0, sizeof(inputRegisters));
    memset(coils, 0, sizeof(coils));
    memset(discreteInputs, 0, sizeof(discreteInputs));
}

ModbusServer::~ModbusServer() {
    stop();
}

bool ModbusServer::begin(bool enableTcp, bool enableRtu) {
    DEBUG_PRINTLN("Initializing Modbus Server...");

    tcpEnabled = enableTcp;
    rtuEnabled = enableRtu;

    if (tcpEnabled) {
        tcpServer = new WiFiServer(MODBUS_TCP_PORT);
        tcpServer->begin();
        DEBUG_PRINT("Modbus TCP server started on port ");
        DEBUG_PRINTLN(MODBUS_TCP_PORT);
    }

    if (rtuEnabled) {
        rtuSerial = &Serial2;
        rtuSerial->begin(MODBUS_RTU_BAUD, MODBUS_RTU_CONFIG);
        DEBUG_PRINT("Modbus RTU started at ");
        DEBUG_PRINT(MODBUS_RTU_BAUD);
        DEBUG_PRINTLN(" baud");
    }

    DEBUG_PRINTLN("Modbus Server initialized");
    return true;
}

void ModbusServer::loop() {
    if (tcpEnabled) {
        handleTcpClient();
    }

    if (rtuEnabled) {
        handleRtuRequest();
    }
}

void ModbusServer::stop() {
    if (tcpServer) {
        tcpServer->stop();
        delete tcpServer;
        tcpServer = nullptr;
    }

    if (tcpClient.connected()) {
        tcpClient.stop();
    }

    tcpEnabled = false;
    rtuEnabled = false;
}

void ModbusServer::handleTcpClient() {
    if (!tcpServer) return;

    if (tcpServer->hasClient()) {
        if (tcpClient.connected()) {
            tcpClient.stop();
        }
        tcpClient = tcpServer->available();
        tcpClientConnected = true;
        lastActivityTime = millis();
        DEBUG_PRINTLN("Modbus TCP client connected");
    }

    if (tcpClient.connected()) {
        if (millis() - lastActivityTime > 30000) {
            tcpClient.stop();
            tcpClientConnected = false;
            return;
        }

        if (tcpClient.available() >= MODBUS_TCP_HEADER_SIZE) {
            uint8_t buffer[256];
            size_t len = 0;

            while (tcpClient.available() && len < sizeof(buffer)) {
                buffer[len++] = tcpClient.read();
            }

            lastActivityTime = millis();

            uint16_t transactionId = (buffer[0] << 8) | buffer[1];
            uint16_t protocolId = (buffer[2] << 8) | buffer[3];
            uint16_t pduLength = (buffer[4] << 8) | buffer[5];
            uint8_t unitId = buffer[6];

            if (protocolId != 0 || unitId != MODBUS_UNIT_ID) {
                return;
            }

            ModbusFrame frame;
            frame.unitId = unitId;
            frame.functionCode = buffer[7];
            frame.startAddress = (buffer[8] << 8) | buffer[9];
            frame.quantity = (buffer[10] << 8) | buffer[11];
            frame.data = (len > 12) ? &buffer[12] : nullptr;
            frame.dataLength = (len > 12) ? len - 12 : 0;

            uint8_t response[256];
            size_t responseLen = buildResponse(frame, response + 7);

            if (responseLen > 0) {
                response[0] = buffer[0];
                response[1] = buffer[1];
                response[2] = 0;
                response[3] = 0;
                response[4] = (responseLen + 1) >> 8;
                response[5] = (responseLen + 1) & 0xFF;
                response[6] = unitId;

                tcpClient.write(response, responseLen + 7);
                requestCount++;
            }
        }
    } else {
        tcpClientConnected = false;
    }
}

void ModbusServer::handleRtuRequest() {
    if (!rtuSerial || !rtuSerial->available()) return;

    uint8_t buffer[256];
    size_t len = 0;

    uint32_t startTime = millis();
    while (millis() - startTime < 50) {
        while (rtuSerial->available() && len < sizeof(buffer)) {
            buffer[len++] = rtuSerial->read();
            startTime = millis();
        }
        delay(1);
    }

    if (len < MODBUS_RTU_MIN_SIZE) return;

    uint16_t receivedCrc = (buffer[len - 1] << 8) | buffer[len - 2];
    uint16_t calculatedCrc = calculateCRC(buffer, len - 2);

    if (receivedCrc != calculatedCrc) {
        errorCount++;
        return;
    }

    if (buffer[0] != MODBUS_UNIT_ID) return;

    ModbusFrame frame;
    frame.unitId = buffer[0];
    frame.functionCode = buffer[1];
    frame.startAddress = (buffer[2] << 8) | buffer[3];
    frame.quantity = (buffer[4] << 8) | buffer[5];
    frame.data = (len > 6) ? &buffer[6] : nullptr;
    frame.dataLength = (len > 8) ? len - 8 : 0;

    uint8_t response[256];
    response[0] = MODBUS_UNIT_ID;
    size_t responseLen = buildResponse(frame, response + 1) + 1;

    uint16_t crc = calculateCRC(response, responseLen);
    response[responseLen++] = crc & 0xFF;
    response[responseLen++] = crc >> 8;

    delay(2);
    rtuSerial->write(response, responseLen);
    requestCount++;
}

size_t ModbusServer::buildResponse(const ModbusFrame& request, uint8_t* response) {
    switch (request.functionCode) {
        case FC_READ_COILS:
            return handleReadCoils(request, response);
        case FC_READ_DISCRETE_INPUTS:
            return handleReadDiscreteInputs(request, response);
        case FC_READ_HOLDING_REGISTERS:
            return handleReadHoldingRegisters(request, response);
        case FC_READ_INPUT_REGISTERS:
            return handleReadInputRegisters(request, response);
        case FC_WRITE_SINGLE_COIL:
            return handleWriteSingleCoil(request, response);
        case FC_WRITE_SINGLE_REGISTER:
            return handleWriteSingleRegister(request, response);
        case FC_WRITE_MULTIPLE_COILS:
            return handleWriteMultipleCoils(request, response);
        case FC_WRITE_MULTIPLE_REGISTERS:
            return handleWriteMultipleRegisters(request, response);
        default:
            return buildErrorResponse(request.functionCode, 0x01, response);
    }
}

size_t ModbusServer::buildErrorResponse(uint8_t functionCode, uint8_t exceptionCode, uint8_t* response) {
    response[0] = functionCode | 0x80;
    response[1] = exceptionCode;
    errorCount++;
    return 2;
}

size_t ModbusServer::handleReadCoils(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress + request.quantity > MODBUS_MAX_COILS) {
        return buildErrorResponse(FC_READ_COILS, 0x02, response);
    }

    uint8_t byteCount = (request.quantity + 7) / 8;
    response[0] = FC_READ_COILS;
    response[1] = byteCount;

    for (uint8_t i = 0; i < byteCount; i++) {
        response[2 + i] = 0;
        for (uint8_t bit = 0; bit < 8 && (i * 8 + bit) < request.quantity; bit++) {
            if (getBit(coils, request.startAddress + i * 8 + bit)) {
                response[2 + i] |= (1 << bit);
            }
        }
    }

    return 2 + byteCount;
}

size_t ModbusServer::handleReadDiscreteInputs(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress + request.quantity > MODBUS_MAX_COILS) {
        return buildErrorResponse(FC_READ_DISCRETE_INPUTS, 0x02, response);
    }

    uint8_t byteCount = (request.quantity + 7) / 8;
    response[0] = FC_READ_DISCRETE_INPUTS;
    response[1] = byteCount;

    for (uint8_t i = 0; i < byteCount; i++) {
        response[2 + i] = 0;
        for (uint8_t bit = 0; bit < 8 && (i * 8 + bit) < request.quantity; bit++) {
            if (getBit(discreteInputs, request.startAddress + i * 8 + bit)) {
                response[2 + i] |= (1 << bit);
            }
        }
    }

    return 2 + byteCount;
}

size_t ModbusServer::handleReadHoldingRegisters(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress + request.quantity > MODBUS_MAX_REGISTERS) {
        return buildErrorResponse(FC_READ_HOLDING_REGISTERS, 0x02, response);
    }

    uint8_t byteCount = request.quantity * 2;
    response[0] = FC_READ_HOLDING_REGISTERS;
    response[1] = byteCount;

    for (uint16_t i = 0; i < request.quantity; i++) {
        uint16_t value = holdingRegisters[request.startAddress + i];
        response[2 + i * 2] = value >> 8;
        response[3 + i * 2] = value & 0xFF;
    }

    return 2 + byteCount;
}

size_t ModbusServer::handleReadInputRegisters(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress + request.quantity > MODBUS_MAX_REGISTERS) {
        return buildErrorResponse(FC_READ_INPUT_REGISTERS, 0x02, response);
    }

    uint8_t byteCount = request.quantity * 2;
    response[0] = FC_READ_INPUT_REGISTERS;
    response[1] = byteCount;

    for (uint16_t i = 0; i < request.quantity; i++) {
        uint16_t value = inputRegisters[request.startAddress + i];
        response[2 + i * 2] = value >> 8;
        response[3 + i * 2] = value & 0xFF;
    }

    return 2 + byteCount;
}

size_t ModbusServer::handleWriteSingleCoil(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress >= MODBUS_MAX_COILS) {
        return buildErrorResponse(FC_WRITE_SINGLE_COIL, 0x02, response);
    }

    bool value = (request.quantity == 0xFF00);
    processCoilWrite(request.startAddress, value);

    response[0] = FC_WRITE_SINGLE_COIL;
    response[1] = request.startAddress >> 8;
    response[2] = request.startAddress & 0xFF;
    response[3] = value ? 0xFF : 0x00;
    response[4] = 0x00;

    return 5;
}

size_t ModbusServer::handleWriteSingleRegister(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress >= MODBUS_MAX_REGISTERS) {
        return buildErrorResponse(FC_WRITE_SINGLE_REGISTER, 0x02, response);
    }

    processRegisterWrite(request.startAddress, request.quantity);

    response[0] = FC_WRITE_SINGLE_REGISTER;
    response[1] = request.startAddress >> 8;
    response[2] = request.startAddress & 0xFF;
    response[3] = request.quantity >> 8;
    response[4] = request.quantity & 0xFF;

    return 5;
}

size_t ModbusServer::handleWriteMultipleCoils(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress + request.quantity > MODBUS_MAX_COILS) {
        return buildErrorResponse(FC_WRITE_MULTIPLE_COILS, 0x02, response);
    }

    if (request.data) {
        for (uint16_t i = 0; i < request.quantity; i++) {
            bool value = (request.data[i / 8] & (1 << (i % 8))) != 0;
            processCoilWrite(request.startAddress + i, value);
        }
    }

    response[0] = FC_WRITE_MULTIPLE_COILS;
    response[1] = request.startAddress >> 8;
    response[2] = request.startAddress & 0xFF;
    response[3] = request.quantity >> 8;
    response[4] = request.quantity & 0xFF;

    return 5;
}

size_t ModbusServer::handleWriteMultipleRegisters(const ModbusFrame& request, uint8_t* response) {
    if (request.startAddress + request.quantity > MODBUS_MAX_REGISTERS) {
        return buildErrorResponse(FC_WRITE_MULTIPLE_REGISTERS, 0x02, response);
    }

    if (request.data && request.dataLength >= request.quantity * 2) {
        for (uint16_t i = 0; i < request.quantity; i++) {
            uint16_t value = (request.data[1 + i * 2] << 8) | request.data[2 + i * 2];
            processRegisterWrite(request.startAddress + i, value);
        }
    }

    response[0] = FC_WRITE_MULTIPLE_REGISTERS;
    response[1] = request.startAddress >> 8;
    response[2] = request.startAddress & 0xFF;
    response[3] = request.quantity >> 8;
    response[4] = request.quantity & 0xFF;

    return 5;
}

void ModbusServer::updateFeatures(const FeatureVector& features) {
    setFloatRegister(REG_RMS, features.rms);
    setFloatRegister(REG_PEAK_TO_PEAK, features.peakToPeak);
    setFloatRegister(REG_KURTOSIS, features.kurtosis);
    setFloatRegister(REG_SKEWNESS, features.skewness);
    setFloatRegister(REG_CREST_FACTOR, features.crestFactor);
    setFloatRegister(REG_VARIANCE, features.variance);
    setFloatRegister(REG_SPECTRAL_CENTROID, features.spectralCentroid);
    setFloatRegister(REG_SPECTRAL_SPREAD, features.spectralSpread);
    setFloatRegister(REG_BAND_POWER_RATIO, features.bandPowerRatio);
    setFloatRegister(REG_DOMINANT_FREQ, features.dominantFrequency);
}

void ModbusServer::updateFault(const FaultResult& fault) {
    inputRegisters[REG_FAULT_TYPE] = (uint16_t)fault.type;
    inputRegisters[REG_FAULT_SEVERITY] = (uint16_t)fault.severity;
    inputRegisters[REG_FAULT_CONFIDENCE] = (uint16_t)(fault.confidence * 100);

    setBit(discreteInputs, COIL_FAULT_DETECTED, fault.type != FAULT_NONE);
}

void ModbusServer::updateSystemStatus(uint8_t cpuUsage, uint32_t freeHeap, int8_t rssi) {
    inputRegisters[REG_CPU_USAGE] = cpuUsage;
    inputRegisters[REG_FREE_HEAP] = freeHeap / 1024;
    inputRegisters[REG_WIFI_RSSI] = (int16_t)rssi;

    uint32_t uptime = millis();
    inputRegisters[REG_UPTIME_LOW] = uptime & 0xFFFF;
    inputRegisters[REG_UPTIME_HIGH] = (uptime >> 16) & 0xFFFF;
}

void ModbusServer::updateSpectrum(const float* spectrum, size_t length) {
    size_t maxBins = (MODBUS_MAX_REGISTERS - REG_SPECTRUM_START) / 2;
    size_t binsToStore = (length < maxBins) ? length : maxBins;

    for (size_t i = 0; i < binsToStore; i++) {
        setFloatRegister(REG_SPECTRUM_START + i * 2, spectrum[i]);
    }
}

void ModbusServer::setCoil(uint8_t coil, bool value) {
    if (coil < MODBUS_MAX_COILS) {
        setBit(coils, coil, value);
    }
}

bool ModbusServer::getCoil(uint8_t coil) const {
    if (coil >= MODBUS_MAX_COILS) return false;
    return getBit(coils, coil);
}

void ModbusServer::setRegister(uint16_t address, uint16_t value) {
    if (address < MODBUS_MAX_REGISTERS) {
        holdingRegisters[address] = value;
    }
}

uint16_t ModbusServer::getRegister(uint16_t address) const {
    if (address >= MODBUS_MAX_REGISTERS) return 0;
    return holdingRegisters[address];
}

void ModbusServer::setFloatRegister(uint16_t address, float value) {
    if (address + 1 >= MODBUS_MAX_REGISTERS) return;

    union {
        float f;
        uint32_t u;
    } converter;

    converter.f = value;
    inputRegisters[address] = (converter.u >> 16) & 0xFFFF;
    inputRegisters[address + 1] = converter.u & 0xFFFF;
}

float ModbusServer::getFloatRegister(uint16_t address) const {
    if (address + 1 >= MODBUS_MAX_REGISTERS) return 0.0f;

    union {
        float f;
        uint32_t u;
    } converter;

    converter.u = ((uint32_t)holdingRegisters[address] << 16) | holdingRegisters[address + 1];
    return converter.f;
}

uint16_t ModbusServer::calculateCRC(uint8_t* buffer, size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }

    return crc;
}

void ModbusServer::setBit(uint8_t* array, uint16_t bit, bool value) {
    uint16_t byteIdx = bit / 8;
    uint8_t bitIdx = bit % 8;
    if (value) {
        array[byteIdx] |= (1 << bitIdx);
    } else {
        array[byteIdx] &= ~(1 << bitIdx);
    }
}

bool ModbusServer::getBit(const uint8_t* array, uint16_t bit) const {
    uint16_t byteIdx = bit / 8;
    uint8_t bitIdx = bit % 8;
    return (array[byteIdx] & (1 << bitIdx)) != 0;
}

void ModbusServer::processCoilWrite(uint16_t coil, bool value) {
    setBit(coils, coil, value);

    switch (coil) {
        case COIL_CALIBRATE:
            if (value && calibrationCallback) {
                calibrationCallback();
            }
            setBit(coils, COIL_CALIBRATE, false);
            break;

        case COIL_RESET:
            if (value && resetCallback) {
                resetCallback();
            }
            setBit(coils, COIL_RESET, false);
            break;
    }
}

void ModbusServer::processRegisterWrite(uint16_t address, uint16_t value) {
    holdingRegisters[address] = value;
}
