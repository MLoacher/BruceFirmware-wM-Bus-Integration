#include "wmbus_decoder.h"

WMBusDecoder::WMBusDecoder()
    : _state(MANCHESTER_IDLE), _lastBitTime(0), _lastBit(false),
      _bitPair(0), _currentByte(0), _bitCount(0), _frameComplete(false),
      _expectedLength(0), _syncWord(0), _frameCount(0), _errorCount(0), _crypto(nullptr) {
}

void WMBusDecoder::reset() {
    _state = MANCHESTER_IDLE;
    _bitBuffer.clear();
    _byteBuffer.clear();
    _frameComplete = false;
    _expectedLength = 0;
    _syncWord = 0;
    _currentByte = 0;
    _bitCount = 0;
}

bool WMBusDecoder::addBit(bool bit, unsigned long timestamp_us) {
    _lastBitTime = timestamp_us;

    switch (_state) {
        case MANCHESTER_IDLE:
            // Look for preamble (alternating 1010... pattern)
            _bitBuffer.push_back(bit);
            if (_bitBuffer.size() > 32) {
                _bitBuffer.erase(_bitBuffer.begin());
            }

            // Check for sync pattern (at least 16 alternating bits)
            if (_bitBuffer.size() >= 16) {
                bool validPreamble = true;
                for (size_t i = 1; i < 16; i++) {
                    if (_bitBuffer[i] == _bitBuffer[i-1]) {
                        validPreamble = false;
                        break;
                    }
                }

                if (validPreamble) {
                    _state = MANCHESTER_SYNC;
                    _bitBuffer.clear();
                    Serial.println("[wM-Bus Decoder] Preamble detected");
                }
            }
            break;

        case MANCHESTER_SYNC:
            // Collect bits for sync word detection
            _bitBuffer.push_back(bit);

            // Manchester encoding: 2 bits = 1 data bit
            if (_bitBuffer.size() >= 2) {
                uint8_t decoded_bit;
                if (decodeManchesterPair(_bitBuffer[0], _bitBuffer[1], decoded_bit)) {
                    _bitPair = (_bitPair << 1) | decoded_bit;
                    _bitCount++;

                    if (_bitCount == 16) {
                        _syncWord = _bitPair;
                        if (_syncWord == WMBUS_SYNC_WORD) {
                            _state = MANCHESTER_DATA;
                            _byteBuffer.clear();
                            _currentByte = 0;
                            _bitCount = 0;
                            Serial.printf("[wM-Bus Decoder] Sync word found: 0x%04X\n", _syncWord);
                        } else {
                            // Wrong sync word, back to idle
                            Serial.printf("[wM-Bus Decoder] Wrong sync: 0x%04X\n", _syncWord);
                            _state = MANCHESTER_IDLE;
                            _errorCount++;
                        }
                        _bitPair = 0;
                        _bitCount = 0;
                    }
                }
                _bitBuffer.erase(_bitBuffer.begin(), _bitBuffer.begin() + 2);
            }
            break;

        case MANCHESTER_DATA:
            // Decode Manchester bits into bytes
            _bitBuffer.push_back(bit);

            if (_bitBuffer.size() >= 2) {
                uint8_t decoded_bit;
                if (decodeManchesterPair(_bitBuffer[0], _bitBuffer[1], decoded_bit)) {
                    _currentByte = (_currentByte << 1) | decoded_bit;
                    _bitCount++;

                    if (_bitCount == 8) {
                        processByte(_currentByte);
                        _currentByte = 0;
                        _bitCount = 0;
                    }
                } else {
                    // Manchester decoding error
                    Serial.println("[wM-Bus Decoder] Manchester decode error");
                    _state = MANCHESTER_IDLE;
                    _errorCount++;
                }
                _bitBuffer.erase(_bitBuffer.begin(), _bitBuffer.begin() + 2);
            }
            break;
    }

    return _frameComplete;
}

bool WMBusDecoder::decodeManchesterPair(bool bit1, bool bit2, uint8_t &decoded_bit) {
    // Differential Manchester encoding:
    // 10 = 1
    // 01 = 0
    if (bit1 && !bit2) {
        decoded_bit = 1;
        return true;
    } else if (!bit1 && bit2) {
        decoded_bit = 0;
        return true;
    } else {
        // Invalid Manchester pair
        return false;
    }
}

void WMBusDecoder::processByte(uint8_t byte) {
    _byteBuffer.push_back(byte);

    // First byte is L-field (length)
    if (_byteBuffer.size() == 1) {
        _expectedLength = byte + 1;  // L-field + all following bytes
        Serial.printf("[wM-Bus Decoder] L-field: %d bytes expected\n", _expectedLength);
    }

    // Check if frame is complete
    if (_byteBuffer.size() >= _expectedLength && _expectedLength > 0) {
        // Validate CRC
        if (validateCRC(_byteBuffer)) {
            _frameComplete = true;
            _frameCount++;
            Serial.printf("[wM-Bus Decoder] Frame complete (%d bytes)\n", _byteBuffer.size());
        } else {
            Serial.println("[wM-Bus Decoder] CRC error");
            _errorCount++;
            reset();
        }
    }

    // Prevent buffer overflow
    if (_byteBuffer.size() > WMBUS_MAX_TELEGRAM_SIZE) {
        Serial.println("[wM-Bus Decoder] Buffer overflow");
        reset();
        _errorCount++;
    }
}

bool WMBusDecoder::validateCRC(const std::vector<uint8_t> &data) {
    if (data.size() < 2) return false;

    // wM-Bus uses CRC-16 with blocks
    // For now, simplified validation (TODO: implement proper block CRC)
    // A proper implementation would check CRC for each block

    // Minimum valid telegram: L + C + M(2) + A(6) + CI = 11 bytes
    if (data.size() < 11) {
        Serial.printf("[wM-Bus Decoder] Frame too short: %d bytes\n", data.size());
        return false;
    }

    // Check L-field consistency
    uint8_t lField = data[0];
    if (lField + 1 != data.size()) {
        Serial.printf("[wM-Bus Decoder] L-field mismatch: %d vs %d\n",
                      lField + 1, data.size());
        return false;
    }

    // For Phase 2, we accept frames with valid L-field
    // Full CRC validation will be added in Phase 3
    return true;
}

uint16_t WMBusDecoder::calculateCRC(const uint8_t *data, size_t length) {
    // CRC-16 calculation with polynomial 0x3D65 (wM-Bus specific)
    uint16_t crc = 0x0000;

    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ WMBUS_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool WMBusDecoder::hasCompleteFrame() {
    return _frameComplete;
}

std::vector<uint8_t> WMBusDecoder::getFrame() {
    std::vector<uint8_t> frame = _byteBuffer;
    reset();  // Reset for next frame
    return frame;
}

bool WMBusDecoder::parseFrame(const std::vector<uint8_t> &frame, WMBusMeter &meter) {
    if (frame.size() < 11) {
        Serial.println("[wM-Bus Parser] Frame too short");
        return false;
    }

    Serial.println("[wM-Bus Parser] Parsing frame...");

    // Parse header (first 10 bytes after L-field)
    if (!parseHeader(frame.data(), frame.size(), meter)) {
        Serial.println("[wM-Bus Parser] Header parse failed");
        return false;
    }

    // Parse data records (DIF/VIF format)
    if (frame.size() > 10) {
        size_t dataOffset = 10;  // After L, C, M(2), A(6)

        // Check if there's a CI-field
        if (frame.size() > dataOffset) {
            uint8_t ciField = frame[dataOffset];
            dataOffset++;
            Serial.printf("[wM-Bus Parser] CI-Field: 0x%02X\n", ciField);

            // CI-Field 0x72 or 0x78 indicates encrypted data
            if (ciField == 0x72 || ciField == 0x78) {
                meter.encrypted = true;
                Serial.println("[wM-Bus Parser] Encrypted telegram detected");

                // Try to decrypt if crypto is available
                if (_crypto && frame.size() > dataOffset) {
                    size_t encryptedDataLen = frame.size() - dataOffset;
                    uint8_t *decryptedData = new uint8_t[encryptedDataLen];

                    if (_crypto->decrypt(meter, &frame[dataOffset], encryptedDataLen, decryptedData)) {
                        meter.decrypted = true;
                        Serial.println("[wM-Bus Parser] Decryption successful");

                        // Parse decrypted data records
                        // Create temporary frame with decrypted payload
                        std::vector<uint8_t> decryptedFrame(frame.begin(), frame.begin() + dataOffset);
                        decryptedFrame.insert(decryptedFrame.end(), decryptedData, decryptedData + encryptedDataLen);

                        parseDataRecords(decryptedFrame.data(), decryptedFrame.size(), dataOffset, meter);

                        delete[] decryptedData;
                    } else {
                        Serial.println("[wM-Bus Parser] Decryption failed - no key available");
                        delete[] decryptedData;
                    }
                }
            }
        }

        // Parse data records if not encrypted
        if (!meter.encrypted && frame.size() > dataOffset) {
            parseDataRecords(frame.data(), frame.size(), dataOffset, meter);
        }
    }

    return true;
}

bool WMBusDecoder::parseHeader(const uint8_t *data, size_t length, WMBusMeter &meter) {
    // wM-Bus header format:
    // Byte 0:    L-field (length)
    // Byte 1:    C-field (control)
    // Byte 2-3:  M-field (manufacturer)
    // Byte 4-7:  A-field (address/ID in BCD)
    // Byte 8:    Version
    // Byte 9:    Medium

    if (length < 10) return false;

    // C-field
    uint8_t cField = data[1];
    Serial.printf("[wM-Bus Parser] C-Field: 0x%02X\n", cField);

    // M-field (manufacturer, little-endian)
    meter.manufacturer = data[2] | (data[3] << 8);
    Serial.printf("[wM-Bus Parser] Manufacturer: 0x%04X (%s)\n",
                  meter.manufacturer, decodeManufacturer(meter.manufacturer).c_str());

    // A-field (meter ID in BCD, 4 bytes)
    memcpy(meter.id, &data[4], 4);
    // Pad remaining ID bytes with zeros
    memset(&meter.id[4], 0, 4);

    Serial.printf("[wM-Bus Parser] Meter ID: %02X%02X%02X%02X\n",
                  meter.id[0], meter.id[1], meter.id[2], meter.id[3]);

    // Version
    meter.version = data[8];
    Serial.printf("[wM-Bus Parser] Version: 0x%02X\n", meter.version);

    // Medium
    meter.medium = data[9];
    Serial.printf("[wM-Bus Parser] Medium: 0x%02X (%s)\n",
                  meter.medium, meter.getMediumName().c_str());

    return true;
}

bool WMBusDecoder::parseDataRecords(const uint8_t *data, size_t length,
                                     size_t offset, WMBusMeter &meter) {
    Serial.printf("[wM-Bus Parser] Parsing data records from offset %d\n", offset);

    // Parse DIF/VIF records (simplified for Phase 2)
    while (offset < length) {
        uint8_t dif = data[offset++];
        if (offset >= length) break;

        // Check for extension DIF
        while (dif & 0x80) {
            if (offset >= length) return false;
            dif = data[offset++];
        }

        // Parse VIF (Value Information Field)
        if (offset >= length) break;
        uint8_t vif = data[offset++];

        // Check for extension VIF
        while (vif & 0x80) {
            if (offset >= length) return false;
            vif = data[offset++];
        }

        // Determine data length from DIF
        uint8_t dataLength = 0;
        uint8_t difData = dif & 0x0F;
        switch (difData) {
            case 0x01: dataLength = 1; break;
            case 0x02: dataLength = 2; break;
            case 0x03: dataLength = 3; break;
            case 0x04: dataLength = 4; break;
            case 0x06: dataLength = 6; break;
            case 0x07: dataLength = 8; break;
            default: dataLength = 0; break;
        }

        if (dataLength == 0 || offset + dataLength > length) {
            break;
        }

        // Extract value
        uint32_t value = extractValue(&data[offset], length - offset, dataLength);
        offset += dataLength;

        Serial.printf("[wM-Bus Parser] DIF=0x%02X VIF=0x%02X Value=%lu\n",
                      dif, vif, value);

        // Map VIF to meter fields (simplified)
        // VIF encoding according to EN 13757-3
        uint8_t vifBase = vif & 0x7F;

        if (vifBase >= 0x00 && vifBase <= 0x07) {
            // Energy (Wh to MWh)
            meter.total_energy = value;
            Serial.printf("[wM-Bus Parser] Energy: %lu Wh\n", value);
        } else if (vifBase >= 0x10 && vifBase <= 0x17) {
            // Volume (m³)
            meter.volume = value;
            Serial.printf("[wM-Bus Parser] Volume: %lu\n", value);
        } else if (vifBase >= 0x58 && vifBase <= 0x5B) {
            // Flow temperature (°C)
            meter.flow_temp = extractSignedValue(&data[offset - dataLength],
                                                  dataLength, dataLength);
            Serial.printf("[wM-Bus Parser] Flow temp: %.2f C\n",
                          meter.flow_temp / 100.0);
        } else if (vifBase >= 0x5C && vifBase <= 0x5F) {
            // Return temperature (°C)
            meter.return_temp = extractSignedValue(&data[offset - dataLength],
                                                    dataLength, dataLength);
            Serial.printf("[wM-Bus Parser] Return temp: %.2f C\n",
                          meter.return_temp / 100.0);
        } else if (vifBase >= 0x2B && vifBase <= 0x2F) {
            // Power (W to MW)
            meter.power = value;
            Serial.printf("[wM-Bus Parser] Power: %u W\n", value);
        } else if (vifBase >= 0x38 && vifBase <= 0x3F) {
            // Flow rate
            meter.flow_rate = value;
            Serial.printf("[wM-Bus Parser] Flow rate: %u\n", value);
        }
    }

    return true;
}

uint32_t WMBusDecoder::extractValue(const uint8_t *data, size_t length, uint8_t dataLength) {
    uint32_t value = 0;

    for (uint8_t i = 0; i < dataLength && i < length && i < 4; i++) {
        value |= ((uint32_t)data[i]) << (i * 8);
    }

    return value;
}

int32_t WMBusDecoder::extractSignedValue(const uint8_t *data, size_t length, uint8_t dataLength) {
    int32_t value = 0;

    for (uint8_t i = 0; i < dataLength && i < length && i < 4; i++) {
        value |= ((int32_t)data[i]) << (i * 8);
    }

    // Sign extension for 1, 2, 3 byte values
    if (dataLength == 1 && (data[0] & 0x80)) {
        value |= 0xFFFFFF00;
    } else if (dataLength == 2 && (data[1] & 0x80)) {
        value |= 0xFFFF0000;
    } else if (dataLength == 3 && (data[2] & 0x80)) {
        value |= 0xFF000000;
    }

    return value;
}

String WMBusDecoder::decodeManufacturer(uint16_t mfr_code) {
    // Manufacturer codes are 3 ASCII characters encoded in 2 bytes
    // Each character is offset by 64 from its position in alphabet
    char mfr[4];
    mfr[0] = ((mfr_code >> 10) & 0x1F) + 64;
    mfr[1] = ((mfr_code >> 5) & 0x1F) + 64;
    mfr[2] = (mfr_code & 0x1F) + 64;
    mfr[3] = 0;
    return String(mfr);
}
