wM-Bus Modul für Bruce Firmware - Implementierungsplan
Überblick
Basierend auf der umfassenden Analyse der Bruce Firmware erstelle ich hier einen konkreten, schrittweisen Plan zur Integration eines wM-Bus (Wireless M-Bus) Moduls für das LILYGO T-Embed CC1101.
Executive Summary
Gute Nachricht: Bruce's CC1101-Implementierung ist perfekt geeignet für wM-Bus!
868 MHz wird unterstützt (868.3, 868.95 MHz vorhanden)
2-FSK Modulation möglich (Mode 0)
Data Rates 32.768 kbps (T1) und 100 kbps (C1) unterstützt
On-the-fly Rekonfiguration funktioniert bereits
Alle benötigten Storage/UI/Menu-Patterns existieren
1. Architektur-Entscheidungen
1.1 Modul-Struktur

src/modules/wmbus/
├── wmbus_receiver.cpp/h    # CC1101 Config + Interrupt-basierte Reception
├── wmbus_decoder.cpp/h     # Manchester Decoder + wM-Bus Parser
├── wmbus_storage.cpp/h     # CSV Storage + Deduplication
├── wmbus_mqtt.cpp/h        # MQTT Client (Home Mode)
├── wmbus_types.h           # Data Structures (WMBusMeter, etc.)

src/core/menu_items/
├── WMBusMenu.cpp/h         # Haupt-Menü Integration
1.2 Kernentscheidungen
Entscheidung	Begründung
Interrupt-basierte RX	Einfacher als RMT, bereits in rf_listen.cpp bewiesen, Bit-Level-Kontrolle
ESPHome wmbusmeters Component	ESP32-optimiert, kleinere Codebase als full wmbusmeters, bewährter Manchester Decoder
SD Card Storage	Primär für große Datasets (2000+ Zähler), LittleFS als Fallback
std::set Deduplication	Memory-efficient O(log n), bereits in sniffer.cpp verwendet
PubSubClient MQTT	Lightweight, weit verbreitet, Home Assistant kompatibel
CSV Format	Universal, text-based, einfach zu debuggen
2. CC1101 Konfiguration für wM-Bus
2.1 T1 Mode (868.3 MHz, 32.768 kbps)

bool initWMBusT1() {
    initRfModule("rx", 868.300);
    ELECHOUSE_cc1101.setModulation(2);      // 2-FSK
    ELECHOUSE_cc1101.setDRate(32.768);      // Data rate
    ELECHOUSE_cc1101.setDeviation(40);      // ±40 kHz
    ELECHOUSE_cc1101.setRxBW(162);          // 162 kHz RX bandwidth
    ELECHOUSE_cc1101.setPktFormat(3);       // Async serial mode
    ELECHOUSE_cc1101.setSyncMode(0);        // No preamble/sync
    ELECHOUSE_cc1101.SetRx();
    return true;
}
2.2 C1 Mode (868.95 MHz, 100 kbps)

bool initWMBusC1() {
    initRfModule("rx", 868.950);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setDRate(100);         // 100 kbps
    ELECHOUSE_cc1101.setDeviation(50);      // ±50 kHz
    ELECHOUSE_cc1101.setRxBW(325);          // 325 kHz
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.SetRx();
    return true;
}
2.3 Reception Strategy
Interrupt-basiert auf GDO0 Pin (Muster aus rf_listen.cpp):

void IRAM_ATTR wmbusGdoInterrupt() {
    unsigned long now = micros();
    bool bitValue = digitalRead(CC1101_GDO0_PIN);
    // Manchester decoding in ISR oder Queue für Processing
}

attachInterrupt(digitalPinToInterrupt(CC1101_GDO0_PIN),
                wmbusGdoInterrupt, CHANGE);
3. Protokoll-Decoder
3.1 Source Code
Empfehlung: Code aus ESPHome wmbusmeters Component portieren
Quelle: https://github.com/SzczepanLeon/esphome-components/tree/main/components/wmbus
Bereits ESP32-optimiert
Bewährter Manchester Decoder
Kleinere Codebase als full wmbusmeters
Apache 2.0 Lizenz
3.2 Komponenten
Manchester Decoder - Differential Manchester Decoding
Packet Parser - wM-Bus Frame Structure (L/C/M/A-Field, DIF/VIF Records)
AES-128 Decryption - mbedtls (ESP32 Hardware AES)
CRC Validation - Packet Integrity Check
4. Data Storage Implementation
4.1 Storage Strategy
Folgt dem wardriving.cpp Muster:

class WMBusStorage {
private:
    std::set<uint64_t> seenMeterIds;  // Memory-efficient deduplication
    String currentFile;               // Timestamp-basierter Filename
    FS *fs;                          // SD/LittleFS
    SemaphoreHandle_t mutex;

public:
    bool init();                     // getFsStorage() + dir erstellen
    bool addReading(WMBusMeter);     // Dedupe + CSV append
    void flush();                    // Alle 10 writes
    std::vector<WMBusMeter> getAll();
};
4.2 CSV Format

Timestamp,MeterID,Manufacturer,Medium,TotalConsumption,FlowTemp,ReturnTemp,FlowRate,RSSI,Decrypted
1704808822,1234567890ABCDEF,4024,06,12345,5523,4512,150,-45,1
4.3 File Organization

/BruceWMBus/
├── 20260109_140532_wmbus.csv  (Auto-generated timestamp)
├── 20260109_153045_wmbus.csv  (Neues File nach 1000 Einträgen oder 1MB)
└── config/
    └── aes_keys.txt
4.4 Power-Loss Safety
FILE_APPEND Mode
file.close() nach jedem Batch (auto-sync)
Mutex Protection (Multi-Threading)
Flush alle 10 Schreibvorgänge
5. UI Implementation
5.1 Menü-Struktur

Main Menu
└── wM-Bus
    ├── Scan Mode
    │   ├── T1 Mode (868.3)
    │   ├── C1 Mode (868.95)
    │   └── Dual Mode
    ├── Home Mode
    ├── View Data
    │   ├── Meter List (pagination)
    │   └── Detail View
    ├── Export CSV
    └── Settings
        ├── AES Keys
        ├── Mode Selection
        └── MQTT Config
5.2 Live Scanning Display
Folgt dem WiFi Sniffer Muster (Update alle 500ms):

void scanMode() {
    drawMainBorderWithTitle("wM-Bus Scan");

    uint32_t lastUpdate = 0;
    int meterCount = 0;

    while (!check(EscPress)) {
        if (millis() - lastUpdate > 500) {
            // Clear area
            tft.fillRect(BORDER_PAD_X, 40, tftWidth - 2*BORDER_PAD_X, 80, bgColor);

            // Update display
            tft.setCursor(BORDER_PAD_X, 40);
            padprintln("Meters: " + String(meterCount));
            padprintln("Last: " + lastMeterId);
            padprintln("Scanning...");

            lastUpdate = millis();
        }

        // Process packets
        if (newPacketAvailable()) {
            WMBusMeter m = parsePacket();
            storage.addReading(m);
            meterCount++;
        }
    }
}
5.3 Meter List (Pagination)
Folgt dem File Browser Muster (sd_functions.cpp):

void viewDataMenu() {
    std::vector<WMBusMeter> meters = storage.getAll();
    int index = 0;
    const int MAX_VISIBLE = 10;

    while (!check(EscPress)) {
        drawMainBorderWithTitle("Meter List");

        int start = (index / MAX_VISIBLE) * MAX_VISIBLE;
        for (int i = start; i < min(start + MAX_VISIBLE, meters.size()); i++) {
            if (i == index) tft.print(">");
            tft.printf("%02X%02X... %ddBm\n",
                meters[i].id[0], meters[i].id[1], meters[i].rssi);
        }

        if (check(NextPress)) index++;
        if (check(PrevPress)) index--;
        if (check(SelPress)) showDetail(meters[index]);
    }
}
6. MQTT Integration (Home Mode)
6.1 Library Addition
platformio.ini erweitern:

lib_deps =
    ...existing...
    knolleary/PubSubClient@^2.8
6.2 Home Assistant Auto-Discovery

void publishDiscovery(const WMBusMeter &meter) {
    String topic = "homeassistant/sensor/wmbus_" + meterId + "/config";

    JsonDocument doc;
    doc["name"] = "wM-Bus " + meterId;
    doc["unique_id"] = "wmbus_" + meterId;
    doc["state_topic"] = "homeassistant/sensor/wmbus_" + meterId + "/state";
    doc["device_class"] = "energy";
    doc["unit_of_measurement"] = "kWh";

    mqttClient.publish(topic.c_str(), payload, true);
}
6.3 State Publishing

void publishState(const WMBusMeter &meter) {
    String topic = "homeassistant/sensor/wmbus_" + meterId + "/state";

    JsonDocument doc;
    doc["consumption"] = meter.total_consumption;
    doc["flow_temp"] = meter.flow_temp / 100.0;
    doc["rssi"] = meter.rssi;

    mqttClient.publish(topic.c_str(), payload);
}
7. Serial Export (CSV via CLI)
7.1 Bestehende CLI nutzen
Bruce hat bereits Serial CLI in src/core/serial_commands/. Wir fügen hinzu:

// wmbus_commands.cpp
bool wmbusExportCommand(const String &filepath) {
    File file = fs->open(filepath, FILE_READ);
    while (file.available()) {
        serialDevice->println(file.readStringUntil('\n'));
    }
    file.close();
}
Registrierung in serialcmds.cpp:

cli.addCommand("wmbus_export", wmbusExportCommand);
cli.addCommand("wmbus_list", wmbusListCommand);
PC-Usage:

$ wmbus_list
$ wmbus_export /BruceWMBus/20260109_140532.csv > data.csv
8. Konfiguration (BruceConfig)
8.1 Settings erweitern
config.h (neue Felder):

class BruceConfig : public BruceTheme {
public:
    struct WMBusConfig {
        uint8_t mode;              // 0=T1, 1=C1, 2=Dual
        String mqttServer;
        int mqttPort;
        String mqttUser;
        String mqttPass;
        std::map<String, String> aesKeys;  // MeterID -> AES key
    };

    WMBusConfig wmbus = {
        .mode = 0,
        .mqttServer = "192.168.1.100",
        .mqttPort = 1883,
        .mqttUser = "",
        .mqttPass = "",
        .aesKeys = {}
    };
};
8.2 JSON Serialization
config.cpp (toJson/fromFile erweitern):

// toJson()
JsonObject wmbusObj = doc.createNestedObject("wmbus");
wmbusObj["mode"] = wmbus.mode;
wmbusObj["mqtt_server"] = wmbus.mqttServer;
// ... etc

// fromFile()
if (!setting["wmbus"].isNull()) {
    wmbus.mode = setting["wmbus"]["mode"];
    wmbus.mqttServer = setting["wmbus"]["mqtt_server"];
    // ... etc
}
9. Main Menu Integration
9.1 Dateien anpassen
src/core/main_menu.h:

#include "menu_items/WMBusMenu.h"

class MainMenu {
    WMBusMenu wmbusMenu;  // ADD
};
src/core/main_menu.cpp (Zeile ~14):

MainMenu::MainMenu() {
    _menuItems = {
        &wifiMenu,
        &bleMenu,
        &rfMenu,
        &wmbusMenu,    // ADD HIER (nach rfMenu)
        &rfidMenu,
        // ...
    };
}
10. Implementierungs-Phasen (MVP)
Phase 1: Basic Reception & Storage (Woche 1-2)
Ziel: Empfang und Speicherung von wM-Bus Paketen Dateien erstellen:
src/modules/wmbus/wmbus_types.h
src/modules/wmbus/wmbus_receiver.cpp/h
src/modules/wmbus/wmbus_storage.cpp/h
src/core/menu_items/WMBusMenu.cpp/h
Tasks:
CC1101 T1 Mode Konfiguration
Interrupt-basierte Reception (GDO0)
Einfacher Manchester Decoder (aus ESPHome portieren)
CSV Storage mit Deduplication
Basis-Menü (nur "Scan Mode")
Test: Empfang von echten Wärmezählern, CSV-Output überprüfen
Phase 2: Protocol Parsing (Woche 3-4)
Ziel: wM-Bus Daten parsen Tasks:
wM-Bus Packet Parser (L/C/M/A-Fields)
DIF/VIF Data Records parsen
Display auf Screen (Live-Anzeige)
Meter List View (Pagination)
Meter Detail View
Test: Korrekte Werte von verschiedenen Zähler-Typen
Phase 3: Advanced Features (Woche 5-6)
Ziel: C1 Mode, Dual Mode, AES Tasks:
C1 Mode Implementation (100 kbps)
Dual Mode (T1/C1 wechselnd)
AES-128 Decryption (mbedtls)
AES Key Management UI
Config Persistence (BruceConfig)
Test: Verschlüsselte Zähler
Phase 4: MQTT & Home Assistant (Woche 7-8)
Ziel: Home Mode Tasks:
PubSubClient zu platformio.ini
MQTT Client Implementation
Home Assistant Auto-Discovery
Reconnection Handling
Throttled Publishing (30s)
Test: Home Assistant Integration
Phase 5: Polish & Export (Woche 9-10)
Ziel: USB Export, Power Optimization Tasks:
Serial CLI Commands (wmbus_export, wmbus_list)
Power Optimization (CC1101 Sleep, Duty Cycling)
Display Dimming
UI Polish
Dokumentation
Test: 6-Stunden Batterie-Test
11. Kritische Dateien (Referenzen)
Zu studierende Dateien:
src/modules/rf/rf_utils.cpp (Zeilen 84-200)
CC1101 Initialization Pattern
Frequency Management
src/modules/rf/rf_listen.cpp (Zeilen 137-267)
Interrupt-basierte Reception
GDO0 Pin Handling
src/modules/wifi/sniffer.cpp (Zeilen 76-92, 583-610)
PSRAM Buffer Management
std::set Deduplication
Mutex-geschützte File Writes
src/modules/gps/wardriving.cpp (Zeilen 199-270)
CSV Storage Pattern
FILE_APPEND Mode
Timestamp-basierte Filenames
src/core/display.cpp (Zeile 450+)
loopOptions() Navigation
Pagination Patterns
src/core/config.cpp (Zeilen 4-79, 81-384)
JSON Serialization/Deserialization
Settings Persistence
Zu modifizierende Dateien:
src/core/main_menu.h - WMBusMenu hinzufügen
src/core/main_menu.cpp (Zeile ~14) - Menu registrieren
src/core/config.h (nach Zeile 34) - WMBusConfig struct
src/core/config.cpp - JSON Serialization erweitern
platformio.ini - PubSubClient library
Neu zu erstellende Dateien:

src/modules/wmbus/
├── wmbus_types.h
├── wmbus_receiver.cpp
├── wmbus_receiver.h
├── wmbus_decoder.cpp
├── wmbus_decoder.h
├── wmbus_storage.cpp
├── wmbus_storage.h
├── wmbus_mqtt.cpp
├── wmbus_mqtt.h

src/core/menu_items/
├── WMBusMenu.cpp
├── WMBusMenu.h

src/core/serial_commands/
├── wmbus_commands.cpp
├── wmbus_commands.h
12. Power Optimization
12.1 CC1101 Power Modes

void enterLowPower() {
    ELECHOUSE_cc1101.setSidle();  // ~100µA
    ELECHOUSE_cc1101.goSleep();   // ~1µA
}
12.2 Duty Cycling

// Scan 5s, sleep 1s
if (millis() - lastActivity > 5000) {
    enterLowPower();
    delay(1000);
    wakeFromLowPower();
}
12.3 Display Dimming

if (millis() - lastInteraction > 30000) {
    setBrightness(bruceConfig.dimmerSet);
}
13. Offene Fragen an den User
Bevor wir mit der Implementierung starten, brauche ich noch Klärung zu:
AES Keys: Hast du bereits AES Keys für deine Zähler? Oder arbeiten wir erstmal nur mit unverschlüsselten?
Test-Hardware: Hast du bereits Zugang zu wM-Bus Wärmezählern zum Testen? (Diehl Sharky 774 erwähnt)
Home Assistant: Ist Home Assistant bereits bei dir im Einsatz? (Für Phase 4 Testing)
Priorität: Welche Phase ist dir am wichtigsten?
Scan Mode (mobil) = Phasen 1-3
Home Mode (MQTT) = Phase 4
ESPHome Component Lizenz: Bist du einverstanden, Code aus dem ESPHome Component zu portieren (Apache 2.0 Lizenz)?
14. Nächste Schritte
Nach Klärung der offenen Fragen:
Phase 1 starten: Basic Reception & Storage
Erste Testdaten sammeln: Mit echten Zählern
Iterativ verfeinern: Basierend auf Real-World Feedback
Geschätzte Gesamt-Entwicklungszeit: 8-10 Wochen für volle Implementation MVP (Scan Mode only): 3-4 Wochen
