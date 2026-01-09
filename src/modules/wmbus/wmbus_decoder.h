#ifndef WMBUS_DECODER_H
#define WMBUS_DECODER_H

#include <Arduino.h>
#include <vector>
#include "wmbus_types.h"
#include "wmbus_crypto.h"

// Manchester decoder states
enum ManchesterState {
    MANCHESTER_IDLE,
    MANCHESTER_SYNC,
    MANCHESTER_DATA
};

// wM-Bus frame format constants
#define WMBUS_PREAMBLE_LENGTH 16
#define WMBUS_SYNC_WORD 0x543D  // wM-Bus sync word for T1/C1 mode

// CRC polynomial for wM-Bus
#define WMBUS_CRC_POLY 0x3D65

class WMBusDecoder {
public:
    WMBusDecoder();

    // Manchester decoding
    void reset();
    bool addBit(bool bit, unsigned long timestamp_us);
    bool hasCompleteFrame();
    std::vector<uint8_t> getFrame();

    // wM-Bus frame parsing
    bool parseFrame(const std::vector<uint8_t> &frame, WMBusMeter &meter);

    // Crypto support
    void setCrypto(WMBusCrypto *crypto) { _crypto = crypto; }

    // Statistics
    uint32_t getFrameCount() const { return _frameCount; }
    uint32_t getErrorCount() const { return _errorCount; }

private:
    // Manchester decoding
    bool decodeManchesterPair(bool bit1, bool bit2, uint8_t &decoded_bit);
    void processByte(uint8_t byte);
    bool validateCRC(const std::vector<uint8_t> &data);
    uint16_t calculateCRC(const uint8_t *data, size_t length);

    // wM-Bus frame parsing
    bool parseHeader(const uint8_t *data, size_t length, WMBusMeter &meter);
    bool parseDataRecords(const uint8_t *data, size_t length, size_t offset, WMBusMeter &meter);
    bool parseDIFVIF(const uint8_t *data, size_t length, size_t &offset,
                     uint8_t &dataType, uint32_t &value);

    // Manufacturer code decoding
    String decodeManufacturer(uint16_t mfr_code);

    // Data extraction helpers
    uint32_t extractValue(const uint8_t *data, size_t length, uint8_t dataLength);
    int32_t extractSignedValue(const uint8_t *data, size_t length, uint8_t dataLength);

    // Manchester decoder state
    ManchesterState _state;
    std::vector<bool> _bitBuffer;
    std::vector<uint8_t> _byteBuffer;
    unsigned long _lastBitTime;
    bool _lastBit;
    uint8_t _bitPair;
    uint8_t _currentByte;
    uint8_t _bitCount;

    // Frame state
    bool _frameComplete;
    uint8_t _expectedLength;
    uint16_t _syncWord;

    // Statistics
    uint32_t _frameCount;
    uint32_t _errorCount;

    // Crypto support
    WMBusCrypto *_crypto;

    // Timing constants (microseconds)
    static const unsigned long BIT_TIME_T1 = 30;  // ~32.768 kbps
    static const unsigned long BIT_TIME_C1 = 10;  // ~100 kbps
};

#endif // WMBUS_DECODER_H
