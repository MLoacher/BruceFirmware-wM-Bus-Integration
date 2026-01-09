#include "wmbus_crypto.h"
#include "core/sd_functions.h"
#include <FS.h>

WMBusCrypto::WMBusCrypto() {
    mbedtls_aes_init(&_aesContext);
}

WMBusCrypto::~WMBusCrypto() {
    mbedtls_aes_free(&_aesContext);
    clearAllKeys();
}

bool WMBusCrypto::addKey(const String &meterIdHex, const uint8_t *key) {
    if (key == nullptr) return false;

    // Normalize meter ID (uppercase, no spaces)
    String normalizedId = meterIdHex;
    normalizedId.toUpperCase();
    normalizedId.trim();

    // Store in map (this will create/overwrite if exists)
    memcpy(_keys[normalizedId], key, WMBUS_AES_KEY_SIZE);

    Serial.printf("[wM-Bus Crypto] Added key for meter: %s\n", normalizedId.c_str());
    return true;
}

bool WMBusCrypto::addKey(const String &meterIdHex, const String &keyHex) {
    uint8_t keyBytes[WMBUS_AES_KEY_SIZE];

    if (!hexStringToBytes(keyHex, keyBytes, WMBUS_AES_KEY_SIZE)) {
        Serial.println("[wM-Bus Crypto] Invalid key hex string");
        return false;
    }

    return addKey(meterIdHex, keyBytes);
}

bool WMBusCrypto::hasKey(const String &meterIdHex) {
    String normalizedId = meterIdHex;
    normalizedId.toUpperCase();
    normalizedId.trim();

    return _keys.find(normalizedId) != _keys.end();
}

bool WMBusCrypto::getKey(const String &meterIdHex, uint8_t *keyOut) {
    if (keyOut == nullptr) return false;

    String normalizedId = meterIdHex;
    normalizedId.toUpperCase();
    normalizedId.trim();

    auto it = _keys.find(normalizedId);
    if (it == _keys.end()) {
        return false;
    }

    memcpy(keyOut, it->second, WMBUS_AES_KEY_SIZE);
    return true;
}

void WMBusCrypto::removeKey(const String &meterIdHex) {
    String normalizedId = meterIdHex;
    normalizedId.toUpperCase();
    normalizedId.trim();

    auto it = _keys.find(normalizedId);
    if (it != _keys.end()) {
        _keys.erase(it);
        Serial.printf("[wM-Bus Crypto] Removed key for meter: %s\n", normalizedId.c_str());
    }
}

void WMBusCrypto::clearAllKeys() {
    _keys.clear();
    Serial.println("[wM-Bus Crypto] Cleared all keys");
}

bool WMBusCrypto::decrypt(WMBusMeter &meter, const uint8_t *encryptedData,
                          size_t dataLength, uint8_t *decryptedOut) {
    if (encryptedData == nullptr || decryptedOut == nullptr || dataLength == 0) {
        return false;
    }

    // Check if we have a key for this meter
    String meterId = meter.getIdString();
    uint8_t key[WMBUS_AES_KEY_SIZE];

    if (!getKey(meterId, key)) {
        Serial.printf("[wM-Bus Crypto] No key available for meter: %s\n", meterId.c_str());
        return false;
    }

    // Build initialization vector (IV) from meter data
    // wM-Bus uses: M-field (2) + A-field (6) + AccessNumber (1) + padding
    uint8_t iv[16];
    buildInitializationVector(meter, iv);

    // Set AES key
    if (mbedtls_aes_setkey_enc(&_aesContext, key, 128) != 0) {
        Serial.println("[wM-Bus Crypto] Failed to set AES key");
        return false;
    }

    // AES-128 CTR mode decryption
    size_t nc_off = 0;
    uint8_t stream_block[16];
    memset(stream_block, 0, sizeof(stream_block));

    if (mbedtls_aes_crypt_ctr(&_aesContext, dataLength, &nc_off, iv,
                              stream_block, encryptedData, decryptedOut) != 0) {
        Serial.println("[wM-Bus Crypto] AES decryption failed");
        return false;
    }

    Serial.printf("[wM-Bus Crypto] Successfully decrypted %d bytes for meter %s\n",
                  dataLength, meterId.c_str());
    return true;
}

void WMBusCrypto::buildInitializationVector(const WMBusMeter &meter, uint8_t *iv) {
    // wM-Bus IV format (16 bytes):
    // Bytes 0-1:  Manufacturer (M-field, little-endian)
    // Bytes 2-7:  Meter ID (A-field, 6 bytes)
    // Byte 8:     Version
    // Byte 9:     Medium
    // Bytes 10-15: Padding (0x00)

    memset(iv, 0, 16);

    // M-field (manufacturer, little-endian)
    iv[0] = meter.manufacturer & 0xFF;
    iv[1] = (meter.manufacturer >> 8) & 0xFF;

    // A-field (meter ID, first 6 bytes)
    memcpy(&iv[2], meter.id, 6);

    // Version
    iv[8] = meter.version;

    // Medium
    iv[9] = meter.medium;

    // Remaining bytes stay 0x00 (already set by memset)
}

bool WMBusCrypto::loadKeysFromFile(const String &filepath) {
    FS *fs;
    if (!getFsStorage(fs)) {
        Serial.println("[wM-Bus Crypto] Failed to access storage");
        return false;
    }

    if (!(*fs).exists(filepath.c_str())) {
        Serial.printf("[wM-Bus Crypto] Key file not found: %s\n", filepath.c_str());
        return false;
    }

    File file = (*fs).open(filepath.c_str(), FILE_READ);
    if (!file) {
        Serial.printf("[wM-Bus Crypto] Failed to open key file: %s\n", filepath.c_str());
        return false;
    }

    int keysLoaded = 0;

    // File format: Each line is "METER_ID_HEX,AES_KEY_HEX"
    // Example: 1234567890ABCDEF,0123456789ABCDEF0123456789ABCDEF
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.isEmpty() || line.startsWith("#")) {
            continue;  // Skip empty lines and comments
        }

        int commaPos = line.indexOf(',');
        if (commaPos <= 0) {
            Serial.printf("[wM-Bus Crypto] Invalid line format: %s\n", line.c_str());
            continue;
        }

        String meterId = line.substring(0, commaPos);
        String keyHex = line.substring(commaPos + 1);

        meterId.trim();
        keyHex.trim();

        if (addKey(meterId, keyHex)) {
            keysLoaded++;
        }
    }

    file.close();

    Serial.printf("[wM-Bus Crypto] Loaded %d keys from %s\n", keysLoaded, filepath.c_str());
    return keysLoaded > 0;
}

bool WMBusCrypto::saveKeysToFile(const String &filepath) {
    FS *fs;
    if (!getFsStorage(fs)) {
        Serial.println("[wM-Bus Crypto] Failed to access storage");
        return false;
    }

    File file = (*fs).open(filepath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.printf("[wM-Bus Crypto] Failed to create key file: %s\n", filepath.c_str());
        return false;
    }

    // Write header comment
    file.println("# wM-Bus AES-128 Keys");
    file.println("# Format: METER_ID_HEX,AES_KEY_HEX");
    file.println("");

    // Write all keys
    for (const auto &entry : _keys) {
        String meterId = entry.first;
        String keyHex = bytesToHexString(entry.second, WMBUS_AES_KEY_SIZE);

        file.printf("%s,%s\n", meterId.c_str(), keyHex.c_str());
    }

    file.close();

    Serial.printf("[wM-Bus Crypto] Saved %d keys to %s\n", _keys.size(), filepath.c_str());
    return true;
}

bool WMBusCrypto::hexStringToBytes(const String &hex, uint8_t *bytes, size_t maxLen) {
    if (bytes == nullptr) return false;

    String cleanHex = hex;
    cleanHex.trim();
    cleanHex.replace(" ", "");
    cleanHex.toUpperCase();

    if (cleanHex.length() != maxLen * 2) {
        Serial.printf("[wM-Bus Crypto] Invalid hex length: %d (expected %d)\n",
                      cleanHex.length(), maxLen * 2);
        return false;
    }

    for (size_t i = 0; i < maxLen; i++) {
        String byteStr = cleanHex.substring(i * 2, i * 2 + 2);
        bytes[i] = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
    }

    return true;
}

String WMBusCrypto::bytesToHexString(const uint8_t *bytes, size_t len) {
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", bytes[i]);
        hex += String(buf);
    }
    return hex;
}
