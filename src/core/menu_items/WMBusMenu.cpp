#include "WMBusMenu.h"
#include "core/display.h"
#include "core/settings.h"
#include "core/mykeyboard.h"
#include <time.h>

// Static crypto instance
WMBusCrypto WMBusMenu::crypto;

void WMBusMenu::optionsMenu() {
    options = {
        {"Scan Mode",    [=]() { scanMenu(); }       },
        {"View Data",    [=]() { viewDataMenu(); }   },
        {"Config",       [=]() { configMenu(); }     },
    };

    addOptionToMainMenu();
    delay(200);

    String txt = "wM-Bus (CC1101)";
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void WMBusMenu::scanMenu() {
    options = {
        {"T1 Mode (868.3)",    [=]() { startScan(WMBUS_MODE_T1); }   },
        {"C1 Mode (868.95)",   [=]() { startScan(WMBUS_MODE_C1); }   },
        {"Dual Mode",          [=]() { startScan(WMBUS_MODE_DUAL); } },
        {"Back",               [=]() { optionsMenu(); }              },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "Scan Mode");
}

void WMBusMenu::startScan(WMBusMode mode) {
    drawMainBorderWithTitle("wM-Bus Scan");

    // Load AES keys if available
    crypto.loadKeysFromFile("/BruceWMBus/config/aes_keys.txt");

    // Initialize receiver
    WMBusReceiver receiver;
    if (!receiver.init(mode)) {
        displayError("CC1101 init failed", true);
        return;
    }

    // Set crypto support
    receiver.setCrypto(&crypto);

    // Initialize storage
    WMBusStorage storage;
    if (!storage.init()) {
        displayError("Storage init failed", true);
        receiver.stop();
        return;
    }

    // Display mode
    tft.setTextSize(FP);
    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
    tft.setTextColor(bruceConfig.priColor);

    const char* modeStr = (mode == WMBUS_MODE_T1) ? "T1 (868.3 MHz)" :
                          (mode == WMBUS_MODE_C1) ? "C1 (868.95 MHz)" :
                          "Dual Mode";
    padprintln("Mode: " + String(modeStr));
    padprintln("");
    padprintln("Scanning for meters...");
    padprintln("Press ESC to stop");
    padprintln("");

    // Start reception
    receiver.startReception();

    // Main scanning loop
    uint32_t lastUpdate = 0;
    uint32_t lastModeSwitch = 0;
    uint32_t meterCount = 0;
    String lastMeterId = "None";
    int8_t lastRSSI = 0;
    WMBusMode currentMode = (mode == WMBUS_MODE_DUAL) ? WMBUS_MODE_T1 : mode;
    bool isDualMode = (mode == WMBUS_MODE_DUAL);

    while (!check(EscPress)) {
        // Dual mode: Switch between T1 and C1 every 5 seconds
        if (isDualMode && (millis() - lastModeSwitch > 5000)) {
            receiver.stopReception();

            // Toggle mode
            currentMode = (currentMode == WMBUS_MODE_T1) ? WMBUS_MODE_C1 : WMBUS_MODE_T1;
            receiver.setMode(currentMode);
            receiver.startReception();

            lastModeSwitch = millis();

            Serial.printf("[wM-Bus] Dual mode switched to %s\n",
                          currentMode == WMBUS_MODE_T1 ? "T1" : "C1");
        }

        // Update display every 500ms
        if (millis() - lastUpdate > 500) {
            // Clear update area
            tft.fillRect(BORDER_PAD_X, 60, tftWidth - 2 * BORDER_PAD_X, 80, bruceConfig.bgColor);

            tft.setCursor(BORDER_PAD_X, 60);
            tft.setTextColor(bruceConfig.priColor);

            padprintln("Meters found: " + String(meterCount));
            padprintln("Last meter: " + lastMeterId);
            padprintln("RSSI: " + String(lastRSSI) + " dBm");
            padprintln("");

            // Show current mode in dual mode
            if (isDualMode) {
                tft.setTextColor(TFT_CYAN);
                padprintln("Active: " + String(currentMode == WMBUS_MODE_T1 ? "T1" : "C1"));
            }

            // Animated scanning indicator
            static int dots = 0;
            String anim = "Scanning";
            for (int i = 0; i < (dots % 4); i++) anim += ".";
            tft.setTextColor(bruceConfig.priColor);
            padprintln(anim);
            dots++;

            // Show current RSSI
            int8_t currentRSSI = receiver.getCurrentRSSI();
            padprintln("Current RSSI: " + String(currentRSSI) + " dBm");

            lastUpdate = millis();
        }

        // Check for received & parsed meters
        WMBusMeter meter;
        if (receiver.hasMeter() && receiver.getMeter(meter)) {
            // Meter is already fully parsed by decoder!

            // Try to add to storage
            if (storage.addReading(meter)) {
                meterCount++;
                lastMeterId = meter.getIdString();
                lastRSSI = meter.rssi;

                // Flash success message with details
                tft.fillRect(BORDER_PAD_X, tftHeight - 60, tftWidth - 2 * BORDER_PAD_X, 40, bruceConfig.bgColor);
                tft.setCursor(BORDER_PAD_X, tftHeight - 60);
                tft.setTextColor(TFT_GREEN);
                padprintln("New: " + lastMeterId);
                padprintln(meter.getManufacturerName() + " " + meter.getMediumName());

                if (meter.encrypted) {
                    tft.setTextColor(TFT_YELLOW);
                    padprintln("Encrypted!");
                }

                delay(500);
            }
        }

        delay(10);
    }

    // Stop reception
    receiver.stopReception();
    receiver.stop();
    storage.flush();
    storage.close();

    displayInfo("Scan complete: " + String(meterCount) + " meters", true);
}

void WMBusMenu::viewDataMenu() {
    WMBusStorage storage;
    if (!storage.init()) {
        displayError("Storage init failed", true);
        return;
    }

    std::vector<WMBusMeter> meters = storage.getAllReadings();
    storage.close();

    if (meters.empty()) {
        displayWarning("No data to display", true);
        return;
    }

    int index = 0;
    bool redraw = true;
    const int MAX_VISIBLE = 8;

    while (!check(EscPress)) {
        if (redraw) {
            drawMainBorderWithTitle("Meter List");

            tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
            tft.setTextSize(FP);

            int start = max(0, index - MAX_VISIBLE / 2);
            int end = min(start + MAX_VISIBLE, (int)meters.size());

            for (int i = start; i < end; i++) {
                if (i == index) {
                    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
                } else {
                    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                }

                WMBusMeter &meter = meters[i];
                String line = meter.getIdString().substring(0, 12) + " " +
                              String(meter.rssi) + "dBm";
                padprintln(line);
            }

            printCenterFootnote(String(index + 1) + "/" + String(meters.size()));
            redraw = false;
        }

        if (check(NextPress)) {
            index++;
            if (index >= meters.size()) index = 0;
            redraw = true;
        }

        if (check(PrevPress)) {
            index--;
            if (index < 0) index = meters.size() - 1;
            redraw = true;
        }

        if (check(SelPress)) {
            displayMeterDetail(meters[index]);
            redraw = true;
        }

        delay(10);
    }
}

void WMBusMenu::displayMeterDetail(const WMBusMeter &meter) {
    drawMainBorderWithTitle("Meter Details");

    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor);

    padprintln("ID: " + meter.getIdString());
    padprintln("Manufacturer: " + meter.getManufacturerName());
    padprintln("Medium: " + meter.getMediumName());
    padprintln("");
    padprintln("Energy: " + String(meter.total_energy) + " Wh");
    padprintln("Flow Temp: " + formatTemperature(meter.flow_temp));
    padprintln("Return Temp: " + formatTemperature(meter.return_temp));
    padprintln("Power: " + String(meter.power) + " W");
    padprintln("");
    padprintln("RSSI: " + String(meter.rssi) + " dBm");
    padprintln("Time: " + formatTimestamp(meter.timestamp));
    padprintln("");

    if (meter.encrypted) {
        if (meter.decrypted) {
            tft.setTextColor(TFT_GREEN);
            padprintln("Decrypted OK");
        } else {
            tft.setTextColor(TFT_RED);
            padprintln("Encrypted (no key)");
        }
    }

    printCenterFootnote("Press any key");

    while (!check(AnyKeyPress)) {
        delay(10);
    }
}

void WMBusMenu::configMenu() {
    options = {
        {"Mode Selection",  [=]() { configureModeSelection(); } },
        {"AES Keys",        [=]() { configureAESKeys(); }       },
        {"Back",            [=]() { optionsMenu(); }            },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "wM-Bus Config");
}

void WMBusMenu::configureModeSelection() {
    options = {
        {"T1 Mode (868.3)",  []() { /* Save T1 mode preference */ } },
        {"C1 Mode (868.95)", []() { /* Save C1 mode preference */ } },
        {"Dual Mode",        []() { /* Save Dual mode preference */ } },
        {"Back",             [=]() { configMenu(); }                 },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "Select Mode");
}

void WMBusMenu::configureAESKeys() {
    // Load keys from file
    crypto.loadKeysFromFile("/BruceWMBus/config/aes_keys.txt");

    options = {
        {"Add Key",      [=]() { addAESKeyMenu(); }     },
        {"View Keys",    [=]() { viewAESKeysMenu(); }   },
        {"Remove Key",   [=]() { removeAESKeyMenu(); }  },
        {"Back",         [=]() { configMenu(); }        },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "AES Keys");
}

void WMBusMenu::addAESKeyMenu() {
    drawMainBorderWithTitle("Add AES Key");

    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor);

    padprintln("Enter Meter ID:");
    padprintln("(16 hex chars)");
    padprintln("");

    String meterId = keyboard("", 16, "0123456789ABCDEF");
    if (meterId.isEmpty() || meterId.length() != 16) {
        displayError("Invalid Meter ID", true);
        return;
    }

    drawMainBorderWithTitle("Add AES Key");
    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor);

    padprintln("Meter ID: " + meterId);
    padprintln("");
    padprintln("Enter AES Key:");
    padprintln("(32 hex chars)");
    padprintln("");

    String key = keyboard("", 32, "0123456789ABCDEF");
    if (key.isEmpty() || key.length() != 32) {
        displayError("Invalid AES Key", true);
        return;
    }

    // Add key to crypto
    if (crypto.addKey(meterId, key)) {
        // Save to file
        crypto.saveKeysToFile("/BruceWMBus/config/aes_keys.txt");
        displayInfo("Key added successfully", true);
    } else {
        displayError("Failed to add key", true);
    }
}

void WMBusMenu::viewAESKeysMenu() {
    drawMainBorderWithTitle("AES Keys");

    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor);

    size_t keyCount = crypto.getKeyCount();
    padprintln("Total keys: " + String(keyCount));
    padprintln("");

    if (keyCount == 0) {
        padprintln("No keys configured");
        padprintln("");
        padprintln("Use 'Add Key' to add");
        padprintln("AES-128 keys for");
        padprintln("encrypted meters");
    } else {
        padprintln("Keys are stored in:");
        padprintln("/BruceWMBus/config/");
        padprintln("aes_keys.txt");
        padprintln("");
        padprintln("Format:");
        padprintln("METER_ID,AES_KEY");
    }

    padprintln("");
    printCenterFootnote("Press any key");

    while (!check(AnyKeyPress)) {
        delay(10);
    }
}

void WMBusMenu::removeAESKeyMenu() {
    drawMainBorderWithTitle("Remove AES Key");

    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor);

    padprintln("Enter Meter ID:");
    padprintln("(16 hex chars)");
    padprintln("");

    String meterId = keyboard("", 16, "0123456789ABCDEF");
    if (meterId.isEmpty() || meterId.length() != 16) {
        displayError("Invalid Meter ID", true);
        return;
    }

    // Remove key
    crypto.removeKey(meterId);
    crypto.saveKeysToFile("/BruceWMBus/config/aes_keys.txt");

    displayInfo("Key removed", true);
}

void WMBusMenu::drawIcon(float scale) {
    clearIconArea();

    // Draw a meter/gauge icon
    int radius = scale * 20;
    int centerX = iconCenterX;
    int centerY = iconCenterY;

    // Outer circle
    tft.drawCircle(centerX, centerY, radius, bruceConfig.priColor);

    // Dial needle (pointing to 45 degrees)
    int needleLen = radius - 5;
    float angle = 0.75 * PI;  // 135 degrees (45 from vertical)
    int x2 = centerX + needleLen * cos(angle);
    int y2 = centerY + needleLen * sin(angle);
    tft.drawLine(centerX, centerY, x2, y2, bruceConfig.priColor);

    // Center dot
    tft.fillCircle(centerX, centerY, 3, bruceConfig.priColor);

    // Scale marks
    for (int i = 0; i < 8; i++) {
        float a = (i * PI / 4) + (PI / 2);
        int x1 = centerX + (radius - 5) * cos(a);
        int y1 = centerY + (radius - 5) * sin(a);
        int x2 = centerX + radius * cos(a);
        int y2 = centerY + radius * sin(a);
        tft.drawLine(x1, y1, x2, y2, bruceConfig.priColor);
    }
}

void WMBusMenu::drawIconImg() {
    // No custom image yet
}

String WMBusMenu::formatTimestamp(uint32_t timestamp) {
    if (timestamp == 0) return "N/A";

    time_t t = timestamp;
    struct tm *timeinfo = localtime(&t);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buffer);
}

String WMBusMenu::formatTemperature(int16_t temp_centidegree) {
    float temp = temp_centidegree / 100.0;
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%.2f C", temp);
    return String(buffer);
}
