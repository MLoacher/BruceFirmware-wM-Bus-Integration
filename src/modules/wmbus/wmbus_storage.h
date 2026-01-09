#ifndef WMBUS_STORAGE_H
#define WMBUS_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <set>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "wmbus_types.h"

// Storage directory
#define WMBUS_STORAGE_DIR "/BruceWMBus"

// Maximum file size before creating new file (1MB)
#define WMBUS_MAX_FILE_SIZE (1024 * 1024)

// Maximum records before creating new file
#define WMBUS_MAX_RECORDS_PER_FILE 1000

class WMBusStorage {
public:
    WMBusStorage();
    ~WMBusStorage();

    // Initialization
    bool init();
    void close();

    // Meter management
    bool addReading(const WMBusMeter &meter);
    bool isSeenBefore(const WMBusMeter &meter);
    void clearSeenMeters();

    // File operations
    bool flush();
    bool createNewFile();
    String getCurrentFilePath() const;

    // Data retrieval
    std::vector<WMBusMeter> getAllReadings(const String &filepath = "");
    uint32_t getMeterCount() const { return _seenMeterIds.size(); }

    // Statistics
    uint32_t getTotalRecords() const { return _recordCount; }
    uint32_t getCurrentFileRecords() const { return _currentFileRecords; }
    size_t getCurrentFileSize();

    // Export
    bool exportToCSV(const String &outputPath);

private:
    // File path generation
    String generateFilename();

    // CSV operations
    bool writeCSVHeader(File &file);
    bool writeCSVRecord(File &file, const WMBusMeter &meter);

    // Member variables
    FS *_fs;                        // Filesystem (SD or LittleFS)
    bool _initialized;

    String _currentFile;            // Current CSV filename
    std::set<uint64_t> _seenMeterIds;  // Deduplication set

    uint32_t _recordCount;          // Total records across all files
    uint32_t _currentFileRecords;   // Records in current file
    uint32_t _writeCounter;         // Counter for periodic flush

    SemaphoreHandle_t _storageMutex;  // Thread-safe access
};

#endif // WMBUS_STORAGE_H
