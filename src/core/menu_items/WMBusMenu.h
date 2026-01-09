#ifndef __WMBUS_MENU_H__
#define __WMBUS_MENU_H__

#include <MenuItemInterface.h>
#include "modules/wmbus/wmbus_receiver.h"
#include "modules/wmbus/wmbus_storage.h"
#include "modules/wmbus/wmbus_crypto.h"

class WMBusMenu : public MenuItemInterface {
public:
    WMBusMenu() : MenuItemInterface("wM-Bus") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    void drawIconImg();
    bool getTheme() { return false; }  // No custom theme yet

private:
    // Sub-menus
    void scanMenu();
    void viewDataMenu();
    void configMenu();

    // Scan mode
    void startScan(WMBusMode mode);
    void displayLiveScanning(WMBusReceiver &receiver, WMBusStorage &storage);

    // View data
    void displayMeterList();
    void displayMeterDetail(const WMBusMeter &meter);

    // Configuration
    void configureModeSelection();
    void configureAESKeys();
    void addAESKeyMenu();
    void removeAESKeyMenu();
    void viewAESKeysMenu();

    // Helper functions
    String formatTimestamp(uint32_t timestamp);
    String formatTemperature(int16_t temp_centidegree);

    // Crypto instance (shared across all operations)
    static WMBusCrypto crypto;
};

#endif // __WMBUS_MENU_H__
