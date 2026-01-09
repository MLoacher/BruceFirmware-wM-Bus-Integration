#ifndef WMBUS_CRYPTO_H
#define WMBUS_CRYPTO_H

#include <Arduino.h>
#include <map>
#include <mbedtls/aes.h>
#include "wmbus_types.h"

// AES-128 key size
#define WMBUS_AES_KEY_SIZE 16

// wM-Bus uses AES-128 CTR mode
class WMBusCrypto {
public:
    WMBusCrypto();
    ~WMBusCrypto();

    // Key management
    bool addKey(const String &meterIdHex, const uint8_t *key);
    bool addKey(const String &meterIdHex, const String &keyHex);
    bool hasKey(const String &meterIdHex);
    bool getKey(const String &meterIdHex, uint8_t *keyOut);
    void removeKey(const String &meterIdHex);
    void clearAllKeys();
    size_t getKeyCount() const { return _keys.size(); }

    // Decryption
    bool decrypt(WMBusMeter &meter, const uint8_t *encryptedData,
                 size_t dataLength, uint8_t *decryptedOut);

    // Key file I/O
    bool loadKeysFromFile(const String &filepath);
    bool saveKeysToFile(const String &filepath);

private:
    // AES context
    mbedtls_aes_context _aesContext;

    // Key storage: MeterID (hex string) -> AES key (16 bytes)
    std::map<String, uint8_t[WMBUS_AES_KEY_SIZE]> _keys;

    // Helper functions
    bool hexStringToBytes(const String &hex, uint8_t *bytes, size_t maxLen);
    String bytesToHexString(const uint8_t *bytes, size_t len);
    void buildInitializationVector(const WMBusMeter &meter, uint8_t *iv);
};

#endif // WMBUS_CRYPTO_H
