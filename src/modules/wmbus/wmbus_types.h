#ifndef WMBUS_TYPES_H
#define WMBUS_TYPES_H

#include <Arduino.h>
#include <cstdint>

// wM-Bus operating modes
enum WMBusMode {
    WMBUS_MODE_T1 = 0,   // 868.3 MHz, 32.768 kbps
    WMBUS_MODE_C1 = 1,   // 868.95 MHz, 100 kbps
    WMBUS_MODE_DUAL = 2  // Alternate between T1 and C1
};

// wM-Bus medium types (according to EN 13757-3)
enum WMBusMedium {
    WMBUS_MEDIUM_OTHER = 0x00,
    WMBUS_MEDIUM_OIL = 0x01,
    WMBUS_MEDIUM_ELECTRICITY = 0x02,
    WMBUS_MEDIUM_GAS = 0x03,
    WMBUS_MEDIUM_HEAT = 0x04,
    WMBUS_MEDIUM_STEAM = 0x05,
    WMBUS_MEDIUM_HOT_WATER = 0x06,
    WMBUS_MEDIUM_WATER = 0x07,
    WMBUS_MEDIUM_HEAT_COST = 0x08,
    WMBUS_MEDIUM_COMPRESSED_AIR = 0x09,
    WMBUS_MEDIUM_COOLING_LOAD_OUTLET = 0x0A,
    WMBUS_MEDIUM_COOLING_LOAD_INLET = 0x0B,
    WMBUS_MEDIUM_HEAT_INLET = 0x0C,
    WMBUS_MEDIUM_HEAT_COOLING = 0x0D,
    WMBUS_MEDIUM_BUS_SYSTEM = 0x0E,
    WMBUS_MEDIUM_UNKNOWN = 0x0F,
    WMBUS_MEDIUM_COLD_WATER = 0x16,
    WMBUS_MEDIUM_DUAL_WATER = 0x17,
    WMBUS_MEDIUM_PRESSURE = 0x18,
    WMBUS_MEDIUM_AD_CONVERTER = 0x19
};

// Structure to hold a wM-Bus meter reading
struct WMBusMeter {
    uint8_t id[8];              // 8-byte meter ID (BCD encoded address)
    uint16_t manufacturer;      // Manufacturer code (e.g., 0x4024 = Diehl)
    uint8_t version;            // Device version
    uint8_t medium;             // Medium type (see WMBusMedium enum)

    // Parsed data fields (specific to heat meters)
    uint32_t total_energy;      // Total energy consumption (Wh)
    int16_t flow_temp;          // Flow temperature (0.01°C)
    int16_t return_temp;        // Return temperature (0.01°C)
    uint16_t power;             // Current power (W)
    uint32_t volume;            // Total volume (0.001 m³)
    uint16_t flow_rate;         // Current flow rate (0.001 m³/h)

    // Reception metadata
    int8_t rssi;                // Signal strength (dBm)
    uint32_t timestamp;         // Unix timestamp of reception
    bool encrypted;             // Was the telegram encrypted?
    bool decrypted;             // Was decryption successful?
    uint8_t aes_key_index;      // Index of AES key used (if decrypted)

    // Constructor with default values
    WMBusMeter() : manufacturer(0), version(0), medium(0),
                   total_energy(0), flow_temp(0), return_temp(0),
                   power(0), volume(0), flow_rate(0),
                   rssi(0), timestamp(0), encrypted(false),
                   decrypted(false), aes_key_index(0xFF) {
        memset(id, 0, 8);
    }

    // Convert meter ID to string (hex format)
    String getIdString() const {
        char buffer[25];
        snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
        return String(buffer);
    }

    // Get manufacturer name from code
    String getManufacturerName() const {
        // Manufacturer codes are 3 ASCII characters encoded in 2 bytes
        char mfr[4];
        mfr[0] = ((manufacturer >> 10) & 0x1F) + 64;
        mfr[1] = ((manufacturer >> 5) & 0x1F) + 64;
        mfr[2] = (manufacturer & 0x1F) + 64;
        mfr[3] = 0;
        return String(mfr);
    }

    // Get human-readable medium name
    String getMediumName() const {
        switch (medium) {
            case WMBUS_MEDIUM_ELECTRICITY: return "Electricity";
            case WMBUS_MEDIUM_GAS: return "Gas";
            case WMBUS_MEDIUM_HEAT: return "Heat";
            case WMBUS_MEDIUM_HOT_WATER: return "Hot Water";
            case WMBUS_MEDIUM_WATER: return "Water";
            case WMBUS_MEDIUM_COLD_WATER: return "Cold Water";
            case WMBUS_MEDIUM_HEAT_COST: return "Heat Cost";
            case WMBUS_MEDIUM_STEAM: return "Steam";
            default: return "Unknown";
        }
    }

    // Get unique 64-bit ID for deduplication
    uint64_t getUniqueId() const {
        uint64_t uid = 0;
        memcpy(&uid, id, 8);
        return uid;
    }
};

// Configuration structure for wM-Bus module
struct WMBusConfig {
    WMBusMode mode;             // Operating mode (T1, C1, or Dual)
    bool auto_save;             // Auto-save received telegrams to SD
    uint16_t dual_mode_interval; // Switch interval in dual mode (ms)

    // MQTT settings (for Home Mode)
    String mqtt_server;
    uint16_t mqtt_port;
    String mqtt_user;
    String mqtt_pass;
    bool mqtt_ha_discovery;     // Enable Home Assistant auto-discovery

    // AES keys (meter ID -> key mapping)
    // Will be stored in BruceConfig

    WMBusConfig() : mode(WMBUS_MODE_T1), auto_save(true),
                    dual_mode_interval(5000), mqtt_server(""),
                    mqtt_port(1883), mqtt_user(""), mqtt_pass(""),
                    mqtt_ha_discovery(true) {}
};

#endif // WMBUS_TYPES_H
