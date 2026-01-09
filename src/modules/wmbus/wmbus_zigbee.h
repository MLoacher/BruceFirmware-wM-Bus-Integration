#ifndef WMBUS_ZIGBEE_H
#define WMBUS_ZIGBEE_H

#include <Arduino.h>
#include "esp_zigbee_core.h"
#include "wmbus_types.h"

// Zigbee configuration
#define WMBUS_ZIGBEE_CHANNEL       11                    // Zigbee channel (11-26)
#define WMBUS_ZIGBEE_REPORT_INTERVAL 30000               // Report every 30 seconds
#define WMBUS_ZIGBEE_PAN_ID        0x1A62                // Default PAN ID

/**
 * WMBusZigbee - Zigbee End Device for wM-Bus meter integration
 *
 * This class implements a Zigbee End Device that exposes wM-Bus meter data
 * as standard Zigbee sensor clusters. It provides:
 * - Temperature sensors (flow/return)
 * - Power sensor (current power consumption)
 * - Energy meter (total energy)
 * - Automatic Home Assistant discovery via ZHA/Zigbee2MQTT
 *
 * Usage:
 *   WMBusZigbee zigbee;
 *   zigbee.begin();
 *   zigbee.publishMeter(meter);
 *   zigbee.loop();
 */
class WMBusZigbee {
public:
    WMBusZigbee();
    ~WMBusZigbee();

    // Initialization and lifecycle
    bool begin();
    void loop();
    bool isConnected();

    // Publishing meter data
    bool publishMeter(const WMBusMeter &meter);

    // Statistics
    uint32_t getPublishCount() const { return _publishCount; }
    uint32_t getErrorCount() const { return _errorCount; }
    void resetStats();

private:
    // Zigbee stack
    esp_zb_ep_list_t *_epList;
    uint8_t _endpoint;
    bool _connected;

    // Current meter state
    WMBusMeter _currentMeter;
    uint32_t _lastPublishTime;

    // Statistics
    uint32_t _publishCount;
    uint32_t _errorCount;

    // Helper functions
    void createEndpoint();
    void createTemperatureSensor();
    void createPowerSensor();
    void createEnergySensor();
    bool updateAttributes(const WMBusMeter &meter);
    bool shouldPublish(const WMBusMeter &meter);

    // Zigbee callbacks
    static void signalHandler(esp_zb_app_signal_t *signal_struct);
    static void attributeHandler(uint16_t cluster_id, uint16_t attr_id);
};

#endif // WMBUS_ZIGBEE_H
