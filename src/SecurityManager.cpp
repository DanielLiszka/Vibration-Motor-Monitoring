#include "SecurityManager.h"
#include "StorageManager.h"
#include <esp_random.h>

#define CREDENTIALS_FILE "/credentials.enc"

SecurityManager::SecurityManager()
    : keyInitialized(false)
    , credentialCount(0)
    , certificateCount(0)
    , activeTokenCount(0)
    , currentLevel(SECURITY_BASIC)
{
    memset(encryptionKey, 0, sizeof(encryptionKey));
    memset(iv, 0, sizeof(iv));
    memset(credentials, 0, sizeof(credentials));
    memset(certificates, 0, sizeof(certificates));
    memset(activeTokens, 0, sizeof(activeTokens));
}

SecurityManager::~SecurityManager() {
    secureErase(encryptionKey, sizeof(encryptionKey));
    secureErase(iv, sizeof(iv));

    for (size_t i = 0; i < certificateCount; i++) {
        if (certificates[i].data) {
            secureErase(certificates[i].data, certificates[i].length);
            free(certificates[i].data);
        }
    }
}

bool SecurityManager::begin() {
    DEBUG_PRINTLN("Initializing Security Manager...");

    mbedtls_aes_init(&aesContext);

    if (!keyInitialized) {
        generateEncryptionKey();
    }

    loadCredentialsFromFlash();

    DEBUG_PRINTLN("Security Manager initialized");
    return true;
}

bool SecurityManager::setEncryptionKey(const uint8_t* key, size_t length) {
    if (length != SECURITY_KEY_SIZE) {
        DEBUG_PRINTLN("Invalid key size");
        return false;
    }

    memcpy(encryptionKey, key, SECURITY_KEY_SIZE);
    mbedtls_aes_setkey_enc(&aesContext, encryptionKey, 256);
    keyInitialized = true;

    return true;
}

bool SecurityManager::generateEncryptionKey() {
    generateRandomBytes(encryptionKey, SECURITY_KEY_SIZE);
    mbedtls_aes_setkey_enc(&aesContext, encryptionKey, 256);
    keyInitialized = true;

    DEBUG_PRINTLN("Encryption key generated");
    return true;
}

bool SecurityManager::encrypt(const uint8_t* input, size_t inputLen, uint8_t* output, size_t* outputLen) {
    if (!keyInitialized) return false;

    generateIV();

    size_t paddedLen = ((inputLen + 15) / 16) * 16;
    uint8_t* padded = (uint8_t*)malloc(paddedLen);
    if (!padded) return false;

    memcpy(padded, input, inputLen);
    memset(padded + inputLen, paddedLen - inputLen, paddedLen - inputLen);

    memcpy(output, iv, SECURITY_IV_SIZE);

    uint8_t ivCopy[SECURITY_IV_SIZE];
    memcpy(ivCopy, iv, SECURITY_IV_SIZE);

    mbedtls_aes_crypt_cbc(&aesContext, MBEDTLS_AES_ENCRYPT, paddedLen, ivCopy, padded, output + SECURITY_IV_SIZE);

    *outputLen = SECURITY_IV_SIZE + paddedLen;

    secureErase(padded, paddedLen);
    free(padded);

    return true;
}

bool SecurityManager::decrypt(const uint8_t* input, size_t inputLen, uint8_t* output, size_t* outputLen) {
    if (!keyInitialized) return false;
    if (inputLen < SECURITY_IV_SIZE + 16) return false;

    uint8_t ivCopy[SECURITY_IV_SIZE];
    memcpy(ivCopy, input, SECURITY_IV_SIZE);

    mbedtls_aes_setkey_dec(&aesContext, encryptionKey, 256);

    size_t dataLen = inputLen - SECURITY_IV_SIZE;
    mbedtls_aes_crypt_cbc(&aesContext, MBEDTLS_AES_DECRYPT, dataLen, ivCopy, input + SECURITY_IV_SIZE, output);

    mbedtls_aes_setkey_enc(&aesContext, encryptionKey, 256);

    uint8_t paddingLen = output[dataLen - 1];
    if (paddingLen > 16 || paddingLen == 0) {
        *outputLen = dataLen;
    } else {
        *outputLen = dataLen - paddingLen;
    }

    return true;
}

bool SecurityManager::hash(const uint8_t* input, size_t inputLen, uint8_t* output) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, input, inputLen);
    mbedtls_sha256_finish(&ctx, output);
    mbedtls_sha256_free(&ctx);
    return true;
}

bool SecurityManager::hashString(const String& input, String& output) {
    uint8_t hashOutput[SECURITY_HASH_SIZE];
    hash((const uint8_t*)input.c_str(), input.length(), hashOutput);
    output = base64Encode(hashOutput, SECURITY_HASH_SIZE);
    return true;
}

bool SecurityManager::verifyHash(const uint8_t* input, size_t inputLen, const uint8_t* expectedHash) {
    uint8_t computedHash[SECURITY_HASH_SIZE];
    hash(input, inputLen, computedHash);
    return memcmp(computedHash, expectedHash, SECURITY_HASH_SIZE) == 0;
}

bool SecurityManager::storeCredential(const char* name, const char* value, bool encryptValue) {
    int idx = findCredential(name);
    if (idx < 0) {
        if (credentialCount >= 8) {
            DEBUG_PRINTLN("Credential storage full");
            return false;
        }
        idx = credentialCount++;
    }

    strncpy(credentials[idx].name, name, sizeof(credentials[idx].name) - 1);

    if (encryptValue && keyInitialized) {
        uint8_t encrypted[256];
        size_t encryptedLen;
        if (encrypt((const uint8_t*)value, strlen(value), encrypted, &encryptedLen)) {
            String encoded = base64Encode(encrypted, encryptedLen);
            strncpy(credentials[idx].value, encoded.c_str(), sizeof(credentials[idx].value) - 1);
            credentials[idx].encrypted = true;
        } else {
            strncpy(credentials[idx].value, value, sizeof(credentials[idx].value) - 1);
            credentials[idx].encrypted = false;
        }
    } else {
        strncpy(credentials[idx].value, value, sizeof(credentials[idx].value) - 1);
        credentials[idx].encrypted = false;
    }

    credentials[idx].expiry = 0;
    saveCredentialsToFlash();

    return true;
}

bool SecurityManager::retrieveCredential(const char* name, char* value, size_t maxLen) {
    int idx = findCredential(name);
    if (idx < 0) return false;

    if (credentials[idx].encrypted && keyInitialized) {
        uint8_t decoded[256];
        size_t decodedLen;
        if (!base64Decode(credentials[idx].value, decoded, &decodedLen)) {
            return false;
        }

        uint8_t decrypted[256];
        size_t decryptedLen;
        if (!decrypt(decoded, decodedLen, decrypted, &decryptedLen)) {
            return false;
        }

        size_t copyLen = (decryptedLen < maxLen - 1) ? decryptedLen : maxLen - 1;
        memcpy(value, decrypted, copyLen);
        value[copyLen] = '\0';

        secureErase(decrypted, sizeof(decrypted));
    } else {
        strncpy(value, credentials[idx].value, maxLen - 1);
        value[maxLen - 1] = '\0';
    }

    return true;
}

bool SecurityManager::deleteCredential(const char* name) {
    int idx = findCredential(name);
    if (idx < 0) return false;

    secureErase(&credentials[idx], sizeof(Credential));

    for (size_t i = idx; i < credentialCount - 1; i++) {
        credentials[i] = credentials[i + 1];
    }
    credentialCount--;

    saveCredentialsToFlash();
    return true;
}

bool SecurityManager::credentialExists(const char* name) {
    return findCredential(name) >= 0;
}

bool SecurityManager::storeCertificate(const char* name, const char* certData, size_t length, bool isCA) {
    int idx = findCertificate(name);
    if (idx < 0) {
        if (certificateCount >= SECURITY_MAX_CERTS) {
            DEBUG_PRINTLN("Certificate storage full");
            return false;
        }
        idx = certificateCount++;
    } else {
        if (certificates[idx].data) {
            free(certificates[idx].data);
        }
    }

    strncpy(certificates[idx].name, name, sizeof(certificates[idx].name) - 1);
    certificates[idx].data = (char*)malloc(length + 1);
    if (!certificates[idx].data) return false;

    memcpy(certificates[idx].data, certData, length);
    certificates[idx].data[length] = '\0';
    certificates[idx].length = length;
    certificates[idx].isCA = isCA;
    certificates[idx].expiry = 0;

    return true;
}

bool SecurityManager::getCertificate(const char* name, char** certData, size_t* length) {
    int idx = findCertificate(name);
    if (idx < 0) return false;

    *certData = certificates[idx].data;
    *length = certificates[idx].length;
    return true;
}

bool SecurityManager::deleteCertificate(const char* name) {
    int idx = findCertificate(name);
    if (idx < 0) return false;

    if (certificates[idx].data) {
        secureErase(certificates[idx].data, certificates[idx].length);
        free(certificates[idx].data);
    }

    for (size_t i = idx; i < certificateCount - 1; i++) {
        certificates[i] = certificates[i + 1];
    }
    certificateCount--;

    return true;
}

const char* SecurityManager::getRootCA() const {
    for (size_t i = 0; i < certificateCount; i++) {
        if (certificates[i].isCA && strcmp(certificates[i].name, "root_ca") == 0) {
            return certificates[i].data;
        }
    }
    return nullptr;
}

const char* SecurityManager::getClientCert() const {
    for (size_t i = 0; i < certificateCount; i++) {
        if (!certificates[i].isCA && strcmp(certificates[i].name, "client_cert") == 0) {
            return certificates[i].data;
        }
    }
    return nullptr;
}

const char* SecurityManager::getPrivateKey() const {
    for (size_t i = 0; i < certificateCount; i++) {
        if (strcmp(certificates[i].name, "private_key") == 0) {
            return certificates[i].data;
        }
    }
    return nullptr;
}

SecurityToken SecurityManager::generateToken(uint32_t validitySeconds, uint8_t permissions) {
    SecurityToken token;

    uint8_t randomBytes[24];
    generateRandomBytes(randomBytes, sizeof(randomBytes));
    String encoded = base64Encode(randomBytes, sizeof(randomBytes));
    strncpy(token.token, encoded.c_str(), SECURITY_TOKEN_SIZE - 1);
    token.token[SECURITY_TOKEN_SIZE - 1] = '\0';

    token.createdAt = millis();
    token.expiresAt = token.createdAt + (validitySeconds * 1000);
    token.permissions = permissions;

    if (activeTokenCount < 4) {
        activeTokens[activeTokenCount++] = token;
    } else {
        for (size_t i = 0; i < 3; i++) {
            activeTokens[i] = activeTokens[i + 1];
        }
        activeTokens[3] = token;
    }

    return token;
}

bool SecurityManager::validateToken(const char* token) {
    int idx = findToken(token);
    if (idx < 0) return false;

    if (millis() > activeTokens[idx].expiresAt) {
        revokeToken(token);
        return false;
    }

    return true;
}

bool SecurityManager::revokeToken(const char* token) {
    int idx = findToken(token);
    if (idx < 0) return false;

    secureErase(&activeTokens[idx], sizeof(SecurityToken));

    for (size_t i = idx; i < activeTokenCount - 1; i++) {
        activeTokens[i] = activeTokens[i + 1];
    }
    activeTokenCount--;

    return true;
}

String SecurityManager::generateAPIKey() {
    uint8_t randomBytes[32];
    generateRandomBytes(randomBytes, sizeof(randomBytes));

    String apiKey = "mk_";
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", randomBytes[i]);
        apiKey += hex;
    }

    return apiKey;
}

bool SecurityManager::validateAPIKey(const String& apiKey) {
    if (!apiKey.startsWith("mk_") || apiKey.length() != 67) {
        return false;
    }

    char storedKey[128];
    if (retrieveCredential("api_key", storedKey, sizeof(storedKey))) {
        return apiKey == String(storedKey);
    }

    return false;
}

String SecurityManager::base64Encode(const uint8_t* input, size_t length) {
    size_t outputLen;
    mbedtls_base64_encode(nullptr, 0, &outputLen, input, length);

    char* output = (char*)malloc(outputLen + 1);
    if (!output) return "";

    mbedtls_base64_encode((unsigned char*)output, outputLen, &outputLen, input, length);
    output[outputLen] = '\0';

    String result(output);
    free(output);
    return result;
}

bool SecurityManager::base64Decode(const String& input, uint8_t* output, size_t* outputLen) {
    size_t len;
    int ret = mbedtls_base64_decode(output, 256, &len, (const unsigned char*)input.c_str(), input.length());
    if (ret == 0) {
        *outputLen = len;
        return true;
    }
    return false;
}

bool SecurityManager::secureErase(void* data, size_t length) {
    volatile uint8_t* p = (volatile uint8_t*)data;
    for (size_t i = 0; i < length; i++) {
        p[i] = 0xFF;
    }
    for (size_t i = 0; i < length; i++) {
        p[i] = 0x00;
    }
    for (size_t i = 0; i < length; i++) {
        p[i] = 0xAA;
    }
    for (size_t i = 0; i < length; i++) {
        p[i] = 0x00;
    }
    return true;
}

uint32_t SecurityManager::generateRandomNumber() {
    return esp_random();
}

void SecurityManager::generateRandomBytes(uint8_t* buffer, size_t length) {
    esp_fill_random(buffer, length);
}

void SecurityManager::generateIV() {
    generateRandomBytes(iv, SECURITY_IV_SIZE);
}

bool SecurityManager::saveCredentialsToFlash() {
    StorageManager storage;
    if (!storage.begin()) return false;

    String json = "{\"count\":" + String(credentialCount) + ",\"creds\":[";
    for (size_t i = 0; i < credentialCount; i++) {
        if (i > 0) json += ",";
        json += "{\"n\":\"" + String(credentials[i].name) + "\",";
        json += "\"v\":\"" + String(credentials[i].value) + "\",";
        json += "\"e\":" + String(credentials[i].encrypted ? "1" : "0") + "}";
    }
    json += "]}";

    return storage.saveLog(CREDENTIALS_FILE, json);
}

bool SecurityManager::loadCredentialsFromFlash() {
    StorageManager storage;
    if (!storage.begin()) return false;

    String json = storage.readLog(CREDENTIALS_FILE);
    if (json.length() == 0) return false;

    return true;
}

int SecurityManager::findCredential(const char* name) {
    for (size_t i = 0; i < credentialCount; i++) {
        if (strcmp(credentials[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int SecurityManager::findCertificate(const char* name) {
    for (size_t i = 0; i < certificateCount; i++) {
        if (strcmp(certificates[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int SecurityManager::findToken(const char* token) {
    for (size_t i = 0; i < activeTokenCount; i++) {
        if (strcmp(activeTokens[i].token, token) == 0) {
            return i;
        }
    }
    return -1;
}

bool SecurityManager::exportSecureConfig(String& output) {
    output = "{\"securityLevel\":" + String((int)currentLevel) + ",";
    output += "\"credentialCount\":" + String(credentialCount) + ",";
    output += "\"certificateCount\":" + String(certificateCount) + "}";
    return true;
}

bool SecurityManager::importSecureConfig(const String& input) {
    return true;
}
