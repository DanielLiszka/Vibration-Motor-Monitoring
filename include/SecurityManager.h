#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

#define SECURITY_KEY_SIZE 32
#define SECURITY_IV_SIZE 16
#define SECURITY_HASH_SIZE 32
#define SECURITY_TOKEN_SIZE 64
#define SECURITY_MAX_CERTS 4

enum SecurityLevel {
    SECURITY_NONE = 0,
    SECURITY_BASIC,
    SECURITY_TLS,
    SECURITY_MUTUAL_TLS
};

struct Credential {
    char name[32];
    char value[256];
    bool encrypted;
    uint32_t expiry;
};

struct Certificate {
    char name[32];
    char* data;
    size_t length;
    uint32_t expiry;
    bool isCA;
};

struct SecurityToken {
    char token[SECURITY_TOKEN_SIZE];
    uint32_t createdAt;
    uint32_t expiresAt;
    uint8_t permissions;
};

class SecurityManager {
public:
    SecurityManager();
    ~SecurityManager();

    bool begin();

    bool setEncryptionKey(const uint8_t* key, size_t length);
    bool generateEncryptionKey();
    bool encrypt(const uint8_t* input, size_t inputLen, uint8_t* output, size_t* outputLen);
    bool decrypt(const uint8_t* input, size_t inputLen, uint8_t* output, size_t* outputLen);

    bool hash(const uint8_t* input, size_t inputLen, uint8_t* output);
    bool hashString(const String& input, String& output);
    bool verifyHash(const uint8_t* input, size_t inputLen, const uint8_t* expectedHash);

    bool storeCredential(const char* name, const char* value, bool encrypt = true);
    bool retrieveCredential(const char* name, char* value, size_t maxLen);
    bool deleteCredential(const char* name);
    bool credentialExists(const char* name);

    bool storeCertificate(const char* name, const char* certData, size_t length, bool isCA = false);
    bool getCertificate(const char* name, char** certData, size_t* length);
    bool deleteCertificate(const char* name);
    const char* getRootCA() const;
    const char* getClientCert() const;
    const char* getPrivateKey() const;

    SecurityToken generateToken(uint32_t validitySeconds, uint8_t permissions = 0xFF);
    bool validateToken(const char* token);
    bool revokeToken(const char* token);

    String generateAPIKey();
    bool validateAPIKey(const String& apiKey);

    String base64Encode(const uint8_t* input, size_t length);
    bool base64Decode(const String& input, uint8_t* output, size_t* outputLen);

    void setSecurityLevel(SecurityLevel level) { currentLevel = level; }
    SecurityLevel getSecurityLevel() const { return currentLevel; }

    bool secureErase(void* data, size_t length);
    uint32_t generateRandomNumber();
    void generateRandomBytes(uint8_t* buffer, size_t length);

    bool exportSecureConfig(String& output);
    bool importSecureConfig(const String& input);

private:
    uint8_t encryptionKey[SECURITY_KEY_SIZE];
    uint8_t iv[SECURITY_IV_SIZE];
    bool keyInitialized;

    Credential credentials[8];
    size_t credentialCount;

    Certificate certificates[SECURITY_MAX_CERTS];
    size_t certificateCount;

    SecurityToken activeTokens[4];
    size_t activeTokenCount;

    SecurityLevel currentLevel;

    mbedtls_aes_context aesContext;

    void generateIV();
    bool saveCredentialsToFlash();
    bool loadCredentialsFromFlash();
    int findCredential(const char* name);
    int findCertificate(const char* name);
    int findToken(const char* token);
};

#endif
