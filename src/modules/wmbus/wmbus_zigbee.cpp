#include "wmbus_zigbee.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wmbus_zigbee";

// Zigbee cluster IDs
#define ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT      0x0402
#define ESP_ZB_ZCL_CLUSTER_ID_SIMPLE_METERING       0x0702
#define ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT 0x0B04

// Zigbee attribute IDs
#define ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE      0x0000
#define ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION  0x0000
#define ESP_ZB_ZCL_ATTR_ELECTRICAL_ACTIVE_POWER     0x050B

WMBusZigbee::WMBusZigbee()
    : _epList(nullptr), _endpoint(1), _connected(false),
      _lastPublishTime(0), _publishCount(0), _errorCount(0) {
}

WMBusZigbee::~WMBusZigbee() {
    // Cleanup is handled by esp-zigbee stack
}

bool WMBusZigbee::begin() {
    Serial.println("[wM-Bus Zigbee] Initializing Zigbee End Device...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Zigbee platform
    esp_zb_platform_config_t platform_config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));

    // Create endpoint list
    _epList = esp_zb_ep_list_create();

    // Create endpoint with sensors
    createEndpoint();

    // Register device
    esp_zb_device_register(_epList);

    // Set signal handler
    esp_zb_core_action_handler_register(signalHandler);

    // Start Zigbee stack
    ESP_ERROR_CHECK(esp_zb_start(false));

    Serial.println("[wM-Bus Zigbee] Started, waiting for network join...");
    return true;
}

void WMBusZigbee::createEndpoint() {
    // Create basic cluster list for the endpoint
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    // Basic cluster (mandatory)
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "wMBus");
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, "wMBus Meter");
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Temperature Measurement cluster (flow temperature)
    createTemperatureSensor();
    esp_zb_attribute_list_t *temp_cluster = esp_zb_temperature_meas_cluster_create(NULL);
    esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, temp_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Simple Metering cluster (energy consumption)
    createEnergySensor();
    esp_zb_attribute_list_t *metering_cluster = esp_zb_metering_cluster_create(NULL);
    esp_zb_cluster_list_add_metering_cluster(cluster_list, metering_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Electrical Measurement cluster (current power)
    createPowerSensor();
    esp_zb_attribute_list_t *electrical_cluster = esp_zb_electrical_meas_cluster_create(NULL);
    esp_zb_cluster_list_add_electrical_meas_cluster(cluster_list, electrical_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Create endpoint
    esp_zb_ep_list_add_ep(_epList, cluster_list, _endpoint, ESP_ZB_AF_HA_PROFILE_ID, ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID);

    Serial.printf("[wM-Bus Zigbee] Created endpoint %d with sensors\n", _endpoint);
}

void WMBusZigbee::createTemperatureSensor() {
    // Temperature is reported in 0.01°C units
    // This will be set when we receive meter data
}

void WMBusZigbee::createPowerSensor() {
    // Power is reported in Watts
}

void WMBusZigbee::createEnergySensor() {
    // Energy is reported in Wh
}

void WMBusZigbee::loop() {
    // Zigbee stack runs in background, nothing needed here
}

bool WMBusZigbee::isConnected() {
    return _connected;
}

bool WMBusZigbee::shouldPublish(const WMBusMeter &meter) {
    uint32_t now = millis();

    // Throttle: Only publish every 30 seconds
    if (now - _lastPublishTime > WMBUS_ZIGBEE_REPORT_INTERVAL) {
        _lastPublishTime = now;
        return true;
    }

    return false;
}

bool WMBusZigbee::publishMeter(const WMBusMeter &meter) {
    if (!_connected) {
        Serial.println("[wM-Bus Zigbee] Not connected to network");
        return false;
    }

    // Check throttle
    if (!shouldPublish(meter)) {
        return true;  // Not an error, just throttled
    }

    // Store current meter
    _currentMeter = meter;

    // Update Zigbee attributes
    if (updateAttributes(meter)) {
        Serial.printf("[wM-Bus Zigbee] Published: %s\n", meter.getIdString().c_str());
        _publishCount++;
        return true;
    } else {
        Serial.printf("[wM-Bus Zigbee] Publish failed: %s\n", meter.getIdString().c_str());
        _errorCount++;
        return false;
    }
}

bool WMBusZigbee::updateAttributes(const WMBusMeter &meter) {
    esp_err_t ret;

    // Update flow temperature (in 0.01°C)
    int16_t temp_value = meter.flow_temp;  // Already in centidegree
    ret = esp_zb_zcl_set_attribute_val(
        _endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE,
        &temp_value,
        false
    );
    if (ret != ESP_OK) {
        Serial.printf("[wM-Bus Zigbee] Failed to update temperature: %d\n", ret);
        return false;
    }

    // Update energy (total consumption in Wh)
    uint32_t energy_value = meter.total_energy;
    ret = esp_zb_zcl_set_attribute_val(
        _endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_SIMPLE_METERING,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION,
        &energy_value,
        false
    );
    if (ret != ESP_OK) {
        Serial.printf("[wM-Bus Zigbee] Failed to update energy: %d\n", ret);
        return false;
    }

    // Update power (current power in W)
    uint16_t power_value = meter.power;
    ret = esp_zb_zcl_set_attribute_val(
        _endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_ACTIVE_POWER,
        &power_value,
        false
    );
    if (ret != ESP_OK) {
        Serial.printf("[wM-Bus Zigbee] Failed to update power: %d\n", ret);
        return false;
    }

    Serial.printf("[wM-Bus Zigbee] Updated: Temp=%.2f°C Energy=%luWh Power=%uW\n",
                  meter.flow_temp / 100.0, meter.total_energy, meter.power);

    return true;
}

void WMBusZigbee::signalHandler(esp_zb_app_signal_t *signal_struct) {
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            Serial.println("[wM-Bus Zigbee] Initialized");
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (err_status == ESP_OK) {
                Serial.println("[wM-Bus Zigbee] Device started, joining network...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                Serial.printf("[wM-Bus Zigbee] Device start failed: %d\n", err_status);
            }
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (err_status == ESP_OK) {
                esp_zb_ieee_addr_t extended_pan_id;
                esp_zb_get_extended_pan_id(extended_pan_id);
                Serial.printf("[wM-Bus Zigbee] Joined network successfully (PANID: 0x%04hx, ExtPANID: ",
                             esp_zb_get_pan_id());
                for (int i = 7; i >= 0; i--) {
                    Serial.printf("%02x", extended_pan_id[i]);
                }
                Serial.println(")");
            } else {
                Serial.printf("[wM-Bus Zigbee] Network steering failed: %d\n", err_status);
                Serial.println("[wM-Bus Zigbee] Retrying in 5 seconds...");
                esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                      ESP_ZB_BDB_MODE_NETWORK_STEERING, 5000);
            }
            break;

        default:
            Serial.printf("[wM-Bus Zigbee] Signal: 0x%x, status: %d\n", sig_type, err_status);
            break;
    }
}

void WMBusZigbee::attributeHandler(uint16_t cluster_id, uint16_t attr_id) {
    Serial.printf("[wM-Bus Zigbee] Attribute read: cluster=0x%04x attr=0x%04x\n",
                  cluster_id, attr_id);
}

void WMBusZigbee::resetStats() {
    _publishCount = 0;
    _errorCount = 0;
}
