#include "wmbus_receiver.h"
#include "core/globals.h"
#include "modules/rf/rf_utils.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Static instance for ISR
WMBusReceiver* WMBusReceiver::_instance = nullptr;

WMBusReceiver::WMBusReceiver()
    : _currentMode(WMBUS_MODE_T1), _initialized(false), _receiving(false),
      _bitIndex(0), _lastBitTime(0), _telegramQueue(nullptr), _rxMutex(nullptr),
      _telegramCount(0), _errorCount(0), _rxState(RX_STATE_IDLE), _expectedLength(0), _crypto(nullptr) {
    memset((void*)_bitBuffer, 0, sizeof(_bitBuffer));
}

WMBusReceiver::~WMBusReceiver() {
    stop();
}

bool WMBusReceiver::init(WMBusMode mode) {
    if (_initialized) {
        Serial.println("[wM-Bus] Already initialized");
        return true;
    }

    Serial.println("[wM-Bus] Initializing receiver...");

    // Create FreeRTOS objects
    _telegramQueue = xQueueCreate(WMBUS_TELEGRAM_QUEUE_SIZE, sizeof(WMBusTelegram));
    if (_telegramQueue == nullptr) {
        Serial.println("[wM-Bus] Failed to create telegram queue");
        return false;
    }

    _meterQueue = xQueueCreate(WMBUS_TELEGRAM_QUEUE_SIZE, sizeof(WMBusMeter));
    if (_meterQueue == nullptr) {
        Serial.println("[wM-Bus] Failed to create meter queue");
        vQueueDelete(_telegramQueue);
        return false;
    }

    _rxMutex = xSemaphoreCreateMutex();
    if (_rxMutex == nullptr) {
        Serial.println("[wM-Bus] Failed to create mutex");
        vQueueDelete(_telegramQueue);
        vQueueDelete(_meterQueue);
        return false;
    }

    // Set singleton instance for ISR
    _instance = this;

    // Configure CC1101
    if (!setMode(mode)) {
        Serial.println("[wM-Bus] Failed to configure CC1101");
        vQueueDelete(_telegramQueue);
        vSemaphoreDelete(_rxMutex);
        return false;
    }

    _initialized = true;
    Serial.println("[wM-Bus] Initialization complete");
    return true;
}

void WMBusReceiver::stop() {
    if (!_initialized) return;

    stopReception();

    if (_telegramQueue) {
        vQueueDelete(_telegramQueue);
        _telegramQueue = nullptr;
    }

    if (_meterQueue) {
        vQueueDelete(_meterQueue);
        _meterQueue = nullptr;
    }

    if (_rxMutex) {
        vSemaphoreDelete(_rxMutex);
        _rxMutex = nullptr;
    }

    // Return CC1101 to idle
    ELECHOUSE_cc1101.setSidle();

    _initialized = false;
    _instance = nullptr;

    Serial.println("[wM-Bus] Receiver stopped");
}

bool WMBusReceiver::setMode(WMBusMode mode) {
    if (xSemaphoreTake(_rxMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[wM-Bus] Failed to acquire mutex");
        return false;
    }

    bool success = false;

    switch (mode) {
        case WMBUS_MODE_T1:
            success = configureCC1101ForT1();
            break;
        case WMBUS_MODE_C1:
            success = configureCC1101ForC1();
            break;
        case WMBUS_MODE_DUAL:
            // Start with T1, will switch in main loop
            success = configureCC1101ForT1();
            break;
    }

    if (success) {
        _currentMode = mode;
        Serial.printf("[wM-Bus] Mode set to %s\n",
                      mode == WMBUS_MODE_T1 ? "T1" :
                      mode == WMBUS_MODE_C1 ? "C1" : "Dual");
    }

    xSemaphoreGive(_rxMutex);
    return success;
}

bool WMBusReceiver::configureCC1101ForT1() {
    Serial.println("[wM-Bus] Configuring for T1 mode (868.3 MHz, 32.768 kbps)");

    // Initialize RF module (from rf_utils.cpp pattern)
    if (!initRfModule("rx", WMBUS_T1_FREQ)) {
        Serial.println("[wM-Bus] Failed to init RF module");
        return false;
    }

    // T1 Mode: 2-FSK, 32.768 kbps, Manchester encoding
    ELECHOUSE_cc1101.setModulation(2);         // 2-FSK
    ELECHOUSE_cc1101.setDRate(WMBUS_T1_DATARATE);  // 32.768 kbps
    ELECHOUSE_cc1101.setDeviation(40);         // ±40 kHz deviation
    ELECHOUSE_cc1101.setRxBW(162);             // 162 kHz RX bandwidth
    ELECHOUSE_cc1101.setPktFormat(3);          // Asynchronous serial mode
    ELECHOUSE_cc1101.setSyncMode(0);           // No preamble/sync
    ELECHOUSE_cc1101.setDcFilterOff(false);    // DC filter ON

    // Set GDO0 to output serial data
    ELECHOUSE_cc1101.setCCMode(1);             // GDO0 = serial clock output

    // Enter RX mode
    ELECHOUSE_cc1101.SetRx();

    delay(10);  // Let CC1101 settle

    return true;
}

bool WMBusReceiver::configureCC1101ForC1() {
    Serial.println("[wM-Bus] Configuring for C1 mode (868.95 MHz, 100 kbps)");

    // Initialize RF module
    if (!initRfModule("rx", WMBUS_C1_FREQ)) {
        Serial.println("[wM-Bus] Failed to init RF module");
        return false;
    }

    // C1 Mode: 2-FSK, 100 kbps, Manchester encoding
    ELECHOUSE_cc1101.setModulation(2);         // 2-FSK
    ELECHOUSE_cc1101.setDRate(WMBUS_C1_DATARATE);  // 100 kbps
    ELECHOUSE_cc1101.setDeviation(50);         // ±50 kHz deviation
    ELECHOUSE_cc1101.setRxBW(325);             // 325 kHz RX bandwidth
    ELECHOUSE_cc1101.setPktFormat(3);          // Asynchronous serial mode
    ELECHOUSE_cc1101.setSyncMode(0);           // No preamble/sync
    ELECHOUSE_cc1101.setDcFilterOff(false);    // DC filter ON

    // Set GDO0 to output serial data
    ELECHOUSE_cc1101.setCCMode(1);

    // Enter RX mode
    ELECHOUSE_cc1101.SetRx();

    delay(10);

    return true;
}

void WMBusReceiver::startReception() {
    if (!_initialized) {
        Serial.println("[wM-Bus] Not initialized");
        return;
    }

    if (_receiving) {
        Serial.println("[wM-Bus] Already receiving");
        return;
    }

    // Reset reception state
    _bitIndex = 0;
    _rxState = RX_STATE_IDLE;
    _lastBitTime = 0;
    memset((void*)_bitBuffer, 0, sizeof(_bitBuffer));

    // Reset decoder
    _decoder.reset();

    // Attach interrupt to GDO0 pin
    attachInterrupt(digitalPinToInterrupt(bruceConfigPins.CC1101_bus.io0),
                    gdoISR, CHANGE);

    _receiving = true;
    Serial.println("[wM-Bus] Reception started");
}

void WMBusReceiver::stopReception() {
    if (!_receiving) return;

    // Detach interrupt
    detachInterrupt(digitalPinToInterrupt(bruceConfigPins.CC1101_bus.io0));

    _receiving = false;
    Serial.println("[wM-Bus] Reception stopped");
}

void IRAM_ATTR WMBusReceiver::gdoISR() {
    if (_instance) {
        _instance->handleGdoInterrupt();
    }
}

void IRAM_ATTR WMBusReceiver::handleGdoInterrupt() {
    unsigned long now = micros();
    bool bitValue = digitalRead(bruceConfigPins.CC1101_bus.io0);

    // Feed bit to Manchester decoder
    // Note: decoder.addBit() is called from ISR, but decoder handles this safely
    if (_decoder.addBit(bitValue, now)) {
        // Frame is complete!
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Get frame from decoder (this also resets decoder for next frame)
        std::vector<uint8_t> frame = _decoder.getFrame();

        if (frame.size() > 0) {
            // Create telegram struct
            WMBusTelegram telegram;
            telegram.length = min((size_t)frame.size(), (size_t)WMBUS_MAX_TELEGRAM_SIZE);
            memcpy(telegram.data, frame.data(), telegram.length);
            telegram.timestamp = millis() / 1000;  // Unix timestamp (approx)
            telegram.rssi = getCurrentRSSI();

            // Queue telegram
            xQueueSendFromISR(_telegramQueue, &telegram, &xHigherPriorityTaskWoken);

            // Parse telegram into meter data
            WMBusMeter meter;
            meter.timestamp = telegram.timestamp;
            meter.rssi = telegram.rssi;

            if (_decoder.parseFrame(frame, meter)) {
                // Queue parsed meter
                xQueueSendFromISR(_meterQueue, &meter, &xHigherPriorityTaskWoken);
            }

            _telegramCount++;
        }

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    _lastBitTime = now;
}

void WMBusReceiver::processBitBuffer() {
    // This function will be called from main loop
    // to decode Manchester and extract telegrams
    // TODO: Implement Manchester decoding in Phase 2
}

bool WMBusReceiver::hasTelegram() {
    if (!_telegramQueue) return false;
    return uxQueueMessagesWaiting(_telegramQueue) > 0;
}

bool WMBusReceiver::getTelegram(WMBusTelegram &telegram) {
    if (!_telegramQueue) return false;

    return xQueueReceive(_telegramQueue, &telegram, 0) == pdTRUE;
}

bool WMBusReceiver::hasMeter() {
    if (!_meterQueue) return false;
    return uxQueueMessagesWaiting(_meterQueue) > 0;
}

bool WMBusReceiver::getMeter(WMBusMeter &meter) {
    if (!_meterQueue) return false;

    return xQueueReceive(_meterQueue, &meter, 0) == pdTRUE;
}

int8_t WMBusReceiver::getCurrentRSSI() {
    if (!_initialized) return -128;

    // Get RSSI from CC1101
    int rssi_dec = ELECHOUSE_cc1101.getRssi();

    // Convert to dBm
    int8_t rssi_dbm;
    if (rssi_dec >= 128) {
        rssi_dbm = (rssi_dec - 256) / 2 - 74;
    } else {
        rssi_dbm = rssi_dec / 2 - 74;
    }

    return rssi_dbm;
}

void WMBusReceiver::resetStats() {
    _telegramCount = 0;
    _errorCount = 0;
}

bool WMBusReceiver::validateTelegram(const uint8_t *data, uint16_t length) {
    // Basic validation
    if (length < 10) return false;  // Minimum telegram size

    // Check L-field (length byte)
    uint8_t lField = data[0];
    if (lField != length - 1) return false;  // Length mismatch

    // TODO: Add CRC check in Phase 2

    return true;
}

uint16_t WMBusReceiver::calculateCRC(const uint8_t *data, uint16_t length) {
    // wM-Bus uses CRC-16 (polynomial 0x3D65)
    // TODO: Implement CRC calculation in Phase 2
    return 0;
}

void WMBusReceiver::setCrypto(WMBusCrypto *crypto) {
    _crypto = crypto;
    _decoder.setCrypto(crypto);
    Serial.println("[wM-Bus] Crypto support enabled");
}
