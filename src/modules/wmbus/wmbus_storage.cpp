#include "wmbus_storage.h"
#include "core/sd_functions.h"
#include "core/globals.h"
#include <time.h>

WMBusStorage::WMBusStorage()
    : _fs(nullptr), _initialized(false), _currentFile(""),
      _recordCount(0), _currentFileRecords(0), _writeCounter(0),
      _storageMutex(nullptr) {
}

WMBusStorage::~WMBusStorage() {
    close();
}

bool WMBusStorage::init() {
    if (_initialized) {
        Serial.println("[wM-Bus Storage] Already initialized");
        return true;
    }

    Serial.println("[wM-Bus Storage] Initializing...");

    // Create mutex
    _storageMutex = xSemaphoreCreateMutex();
    if (_storageMutex == nullptr) {
        Serial.println("[wM-Bus Storage] Failed to create mutex");
        return false;
    }

    // Get filesystem (SD card preferred, LittleFS fallback)
    if (!getFsStorage(_fs)) {
        Serial.println("[wM-Bus Storage] Failed to access storage");
        vSemaphoreDelete(_storageMutex);
        return false;
    }

    // Create storage directory if it doesn't exist
    if (!(*_fs).exists(WMBUS_STORAGE_DIR)) {
        if (!(*_fs).mkdir(WMBUS_STORAGE_DIR)) {
            Serial.println("[wM-Bus Storage] Failed to create directory");
            vSemaphoreDelete(_storageMutex);
            return false;
        }
        Serial.printf("[wM-Bus Storage] Created directory: %s\n", WMBUS_STORAGE_DIR);
    }

    // Generate new filename
    if (!createNewFile()) {
        Serial.println("[wM-Bus Storage] Failed to create initial file");
        vSemaphoreDelete(_storageMutex);
        return false;
    }

    _initialized = true;
    Serial.printf("[wM-Bus Storage] Initialized successfully. File: %s\n", _currentFile.c_str());
    return true;
}

void WMBusStorage::close() {
    if (!_initialized) return;

    flush();

    if (_storageMutex) {
        vSemaphoreDelete(_storageMutex);
        _storageMutex = nullptr;
    }

    _initialized = false;
    Serial.println("[wM-Bus Storage] Closed");
}

String WMBusStorage::generateFilename() {
    // Generate timestamp-based filename: YYYYMMDD_HHMMSS_wmbus.csv
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);

    return String(timestamp) + "_wmbus.csv";
}

bool WMBusStorage::createNewFile() {
    if (xSemaphoreTake(_storageMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[wM-Bus Storage] Failed to acquire mutex");
        return false;
    }

    // Flush current file if open
    flush();

    // Generate new filename
    _currentFile = generateFilename();
    String filepath = String(WMBUS_STORAGE_DIR) + "/" + _currentFile;

    // Create file with CSV header
    File file = (*_fs).open(filepath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.printf("[wM-Bus Storage] Failed to create file: %s\n", filepath.c_str());
        xSemaphoreGive(_storageMutex);
        return false;
    }

    // Write CSV header
    if (!writeCSVHeader(file)) {
        file.close();
        xSemaphoreGive(_storageMutex);
        return false;
    }

    file.close();
    _currentFileRecords = 0;

    Serial.printf("[wM-Bus Storage] Created new file: %s\n", filepath.c_str());

    xSemaphoreGive(_storageMutex);
    return true;
}

bool WMBusStorage::writeCSVHeader(File &file) {
    // CSV header matching our data structure
    file.println("Timestamp,MeterID,Manufacturer,MfrName,Version,Medium,MediumName,"
                 "TotalEnergy,FlowTemp,ReturnTemp,Power,Volume,FlowRate,"
                 "RSSI,Encrypted,Decrypted,AESKeyIndex");
    return true;
}

bool WMBusStorage::addReading(const WMBusMeter &meter) {
    if (!_initialized) {
        Serial.println("[wM-Bus Storage] Not initialized");
        return false;
    }

    // Check for duplicate
    uint64_t uid = meter.getUniqueId();
    if (_seenMeterIds.find(uid) != _seenMeterIds.end()) {
        // Already seen this meter
        return false;
    }

    if (xSemaphoreTake(_storageMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[wM-Bus Storage] Failed to acquire mutex");
        return false;
    }

    // Add to seen set
    _seenMeterIds.insert(uid);

    // Check if we need a new file
    if (_currentFileRecords >= WMBUS_MAX_RECORDS_PER_FILE) {
        xSemaphoreGive(_storageMutex);
        createNewFile();
        if (xSemaphoreTake(_storageMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return false;
        }
    }

    // Open file in append mode
    String filepath = String(WMBUS_STORAGE_DIR) + "/" + _currentFile;
    File file = (*_fs).open(filepath.c_str(), FILE_APPEND);
    if (!file) {
        Serial.printf("[wM-Bus Storage] Failed to open file: %s\n", filepath.c_str());
        xSemaphoreGive(_storageMutex);
        return false;
    }

    // Write CSV record
    bool success = writeCSVRecord(file, meter);
    file.close();

    if (success) {
        _recordCount++;
        _currentFileRecords++;
        _writeCounter++;

        // Flush every 10 writes for power-loss safety
        if (_writeCounter % 10 == 0) {
            flush();
        }
    }

    xSemaphoreGive(_storageMutex);
    return success;
}

bool WMBusStorage::writeCSVRecord(File &file, const WMBusMeter &meter) {
    // Format: Timestamp,MeterID,Manufacturer,MfrName,Version,Medium,MediumName,
    //         TotalEnergy,FlowTemp,ReturnTemp,Power,Volume,FlowRate,
    //         RSSI,Encrypted,Decrypted,AESKeyIndex

    char line[512];
    snprintf(line, sizeof(line),
             "%lu,%s,%04X,%s,%02X,%02X,%s,%lu,%.2f,%.2f,%u,%.3f,%.3f,%d,%d,%d,%u\n",
             meter.timestamp,
             meter.getIdString().c_str(),
             meter.manufacturer,
             meter.getManufacturerName().c_str(),
             meter.version,
             meter.medium,
             meter.getMediumName().c_str(),
             meter.total_energy,
             meter.flow_temp / 100.0,
             meter.return_temp / 100.0,
             meter.power,
             meter.volume / 1000.0,
             meter.flow_rate / 1000.0,
             meter.rssi,
             meter.encrypted ? 1 : 0,
             meter.decrypted ? 1 : 0,
             meter.aes_key_index
    );

    file.print(line);
    return true;
}

bool WMBusStorage::flush() {
    if (!_initialized || _currentFile.isEmpty()) return true;

    if (xSemaphoreTake(_storageMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    String filepath = String(WMBUS_STORAGE_DIR) + "/" + _currentFile;
    if ((*_fs).exists(filepath.c_str())) {
        File file = (*_fs).open(filepath.c_str(), FILE_APPEND);
        if (file) {
            file.flush();
            file.close();
        }
    }

    xSemaphoreGive(_storageMutex);
    return true;
}

bool WMBusStorage::isSeenBefore(const WMBusMeter &meter) {
    uint64_t uid = meter.getUniqueId();
    return _seenMeterIds.find(uid) != _seenMeterIds.end();
}

void WMBusStorage::clearSeenMeters() {
    if (xSemaphoreTake(_storageMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        _seenMeterIds.clear();
        Serial.println("[wM-Bus Storage] Cleared seen meters cache");
        xSemaphoreGive(_storageMutex);
    }
}

String WMBusStorage::getCurrentFilePath() const {
    if (_currentFile.isEmpty()) return "";
    return String(WMBUS_STORAGE_DIR) + "/" + _currentFile;
}

size_t WMBusStorage::getCurrentFileSize() {
    if (!_initialized || _currentFile.isEmpty()) return 0;

    String filepath = getCurrentFilePath();
    if (!(*_fs).exists(filepath.c_str())) return 0;

    File file = (*_fs).open(filepath.c_str(), FILE_READ);
    if (!file) return 0;

    size_t size = file.size();
    file.close();

    return size;
}

std::vector<WMBusMeter> WMBusStorage::getAllReadings(const String &filepath) {
    std::vector<WMBusMeter> readings;

    if (!_initialized) return readings;

    // Use specified file or current file
    String path = filepath.isEmpty() ? getCurrentFilePath() : filepath;

    if (!(*_fs).exists(path.c_str())) {
        Serial.printf("[wM-Bus Storage] File not found: %s\n", path.c_str());
        return readings;
    }

    File file = (*_fs).open(path.c_str(), FILE_READ);
    if (!file) {
        Serial.printf("[wM-Bus Storage] Failed to open file: %s\n", path.c_str());
        return readings;
    }

    // Skip CSV header
    if (file.available()) {
        file.readStringUntil('\n');
    }

    // Parse CSV records
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.isEmpty()) continue;

        // TODO: Parse CSV line into WMBusMeter struct
        // For now, we'll implement basic parsing in Phase 2
        // This is primarily for display purposes
    }

    file.close();

    Serial.printf("[wM-Bus Storage] Loaded %d readings from %s\n",
                  readings.size(), path.c_str());

    return readings;
}

bool WMBusStorage::exportToCSV(const String &outputPath) {
    // This will be used for USB export via serial CLI
    // For now, files are already in CSV format
    // In Phase 5, we can add merging of multiple files
    return true;
}
