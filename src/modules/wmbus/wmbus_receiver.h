#ifndef WMBUS_RECEIVER_H
#define WMBUS_RECEIVER_H

#include <Arduino.h>
#include "wmbus_types.h"
#include "wmbus_decoder.h"
#include "wmbus_crypto.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// Buffer sizes
#define WMBUS_MAX_TELEGRAM_SIZE 300
#define WMBUS_BIT_BUFFER_SIZE 2400  // 300 bytes * 8 bits
#define WMBUS_TELEGRAM_QUEUE_SIZE 10

// CC1101 configuration values for wM-Bus
#define WMBUS_T1_FREQ 868.300f
#define WMBUS_C1_FREQ 868.950f
#define WMBUS_T1_DATARATE 32.768f
#define WMBUS_C1_DATARATE 100.0f

// Telegram reception states
enum WMBusRxState {
    RX_STATE_IDLE,
    RX_STATE_SYNC,
    RX_STATE_LENGTH,
    RX_STATE_DATA,
    RX_STATE_COMPLETE
};

// Structure for received telegram (raw bytes)
struct WMBusTelegram {
    uint8_t data[WMBUS_MAX_TELEGRAM_SIZE];
    uint16_t length;
    int8_t rssi;
    uint32_t timestamp;

    WMBusTelegram() : length(0), rssi(0), timestamp(0) {
        memset(data, 0, WMBUS_MAX_TELEGRAM_SIZE);
    }
};

class WMBusReceiver {
public:
    WMBusReceiver();
    ~WMBusReceiver();

    // Initialization and configuration
    bool init(WMBusMode mode = WMBUS_MODE_T1);
    void stop();

    // Mode switching
    bool setMode(WMBusMode mode);
    WMBusMode getMode() const { return _currentMode; }

    // Reception control
    void startReception();
    void stopReception();
    bool isReceiving() const { return _receiving; }

    // Telegram retrieval
    bool hasTelegram();
    bool getTelegram(WMBusTelegram &telegram);

    // Parsed meter retrieval
    bool hasMeter();
    bool getMeter(WMBusMeter &meter);

    // Statistics
    uint32_t getTelegramCount() const { return _telegramCount; }
    uint32_t getErrorCount() const { return _errorCount; }
    void resetStats();

    // RSSI measurement
    int8_t getCurrentRSSI();

    // Crypto support
    void setCrypto(WMBusCrypto *crypto);

private:
    // CC1101 configuration helpers
    bool configureCC1101ForT1();
    bool configureCC1101ForC1();

    // Interrupt service routine (static, calls instance handler)
    static void IRAM_ATTR gdoISR();
    static WMBusReceiver* _instance;  // Singleton for ISR access

    // Instance ISR handler
    void IRAM_ATTR handleGdoInterrupt();

    // Bit and telegram processing
    void processBitBuffer();
    bool validateTelegram(const uint8_t *data, uint16_t length);
    uint16_t calculateCRC(const uint8_t *data, uint16_t length);

    // Member variables
    WMBusMode _currentMode;
    bool _initialized;
    bool _receiving;

    // Bit-level reception (for Manchester decoding)
    volatile uint32_t _bitBuffer[WMBUS_BIT_BUFFER_SIZE / 32];  // Packed bits
    volatile uint16_t _bitIndex;
    volatile unsigned long _lastBitTime;

    // Telegram queue
    QueueHandle_t _telegramQueue;
    QueueHandle_t _meterQueue;
    SemaphoreHandle_t _rxMutex;

    // Decoder
    WMBusDecoder _decoder;

    // Crypto support (not owned, just a reference)
    WMBusCrypto *_crypto;

    // Statistics
    volatile uint32_t _telegramCount;
    volatile uint32_t _errorCount;

    // Reception state machine
    volatile WMBusRxState _rxState;
    volatile uint8_t _expectedLength;
};

#endif // WMBUS_RECEIVER_H
