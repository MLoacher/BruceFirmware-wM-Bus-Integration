# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Bruce is an ESP32-based offensive security/red team firmware supporting multiple hardware platforms (M5Stack, LilyGo, etc.). It provides WiFi/BLE attacks, RF operations, RFID/NFC capabilities, IR control, and many other security testing features.

**Key Technology Stack:**
- Framework: Arduino-ESP32 v3.x (ESP-IDF 5.5)
- Build System: PlatformIO
- Primary Language: C++ (C++17)
- Supported Chips: ESP32, ESP32-S3, ESP32-C5 (5GHz WiFi support)
- Graphics: TFT_eSPI with sprite-based double buffering
- BLE: NimBLE stack
- Filesystem: LittleFS (internal) + SD card (external)

## Building the Firmware

### Build a Specific Board

```bash
# Build for a specific device (change default_envs in platformio.ini or use -e flag)
pio run -e m5stack-cardputer

# Build for M5StickC Plus2 (example)
pio run -e m5stack-cplus2

# Build for LilyGo T-Embed CC1101
pio run -e lilygo-t-embed-cc1101

# Upload to device
pio run -e m5stack-cardputer --target upload

# Monitor serial output
pio device monitor --baud 115200
```

### Build Outputs

The build process (`build.py`) automatically merges three binaries into a single flashable file:
- `bootloader.bin` (offset varies by chip: 0x1000 for ESP32, 0x0000 for S3, 0x2000 for C5)
- `partitions.bin` (0x8000)
- `firmware.bin` (0x10000)
- **Output**: `Bruce-[env_name].bin` in project root

### Flash Manually

```bash
# Using esptool.py (change port and binary name as needed)
esptool.py --port /dev/ttyACM0 write_flash 0x00000 Bruce-m5stack-cardputer.bin

# For M5Stack devices, can also use m5burner or M5Launcher OTA
```

### Code Formatting

The project uses clang-format with LLVM-based style:

```bash
# Format a specific file
clang-format -i src/core/main_menu.cpp

# Format all modified files before committing
clang-format -i $(git diff --name-only --cached | grep -E '\.(cpp|h)$')
```

**Key Style Rules:**
- IndentWidth: 4 spaces (no tabs)
- ColumnLimit: 110 characters
- BreakBeforeBraces: Attach (K&R style)
- Short functions/lambdas on single lines allowed

## Architecture Overview

### Directory Structure

```
Bruce/
├── boards/                    # Board-specific configurations
│   ├── _boards_json/         # PlatformIO board definitions (.json)
│   ├── [board-name]/         # Per-board customization
│   │   ├── interface.cpp     # Board GPIO setup (weak function overrides)
│   │   ├── [board].ini       # PlatformIO build flags
│   │   └── pins_arduino.h    # Pin mappings for TFT/SD/peripherals
├── src/
│   ├── core/                 # Core framework
│   │   ├── config.h          # BruceConfig (persistent settings)
│   │   ├── configPins.h      # BruceConfigPins (runtime pin config)
│   │   ├── display.cpp       # UI rendering (loopOptions, menus)
│   │   ├── main_menu.cpp     # Main menu system
│   │   ├── menu_items/       # Feature menu implementations
│   │   ├── wifi/            # WiFi core (webInterface, common functions)
│   │   └── serial_commands/  # CLI interface commands
│   ├── modules/              # Feature modules (isolated)
│   │   ├── wifi/            # WiFi attacks (deauth, evil portal, etc.)
│   │   ├── rf/              # RF operations (CC1101, SubGHz)
│   │   ├── rfid/            # RFID/NFC (PN532, RC522, ST25R3916, etc.)
│   │   ├── ir/              # Infrared (TV-B-Gone, custom IR)
│   │   ├── ble/             # BLE operations (spam, scan)
│   │   ├── NRF24/           # NRF24L01 operations
│   │   ├── bjs_interpreter/ # JavaScript interpreter (Duktape)
│   │   └── others/          # Misc (audio, QR codes, iButton, badusb)
│   └── main.cpp             # Entry point (setup/loop)
├── include/
│   ├── globals.h            # Global state (extern declarations)
│   ├── interface.h          # Hardware abstraction layer
│   ├── MenuItemInterface.h  # Menu item base class
│   └── precompiler_flags.h  # Default build flags for all boards
└── platformio.ini           # Main build configuration
```

### Initialization Flow (main.cpp)

```
setup()
├── Serial initialization (115200 baud)
├── setup_gpio()                    # Board-specific early GPIO (weak function)
├── TFT initialization
├── begin_storage()                 # LittleFS + SD card mounting
│   ├── bruceConfig.fromFile()     # Load /bruce.conf (settings)
│   └── bruceConfigPins.fromFile() # Load /brucePins.conf (pin config)
├── begin_tft()                    # Configure display orientation/brightness
├── init_clock()                   # RTC/NTP time setup
├── _post_setup_gpio()             # Late GPIO setup (weak function, after config loaded)
├── xTaskCreate(taskInputHandler)  # FreeRTOS input handler task (priority 2)
└── mainMenu.begin()               # Enter main menu loop

loop()
├── JavaScript interpreter handler (if running)
├── tft.fillScreen()
└── mainMenu.begin()               # Main menu system
```

**Key Pattern: Weak Functions**
- `_setup_gpio()`: Called early (before storage) for critical hardware init (e.g., SD card power)
- `_post_setup_gpio()`: Called after config loaded (e.g., keyboard controllers, I2C devices)
- These are defined as `__attribute__((weak))` in main.cpp, allowing boards to override via `interface.cpp`

### Menu System

All feature menus inherit from `MenuItemInterface`:

```cpp
class MenuItemInterface {
    virtual void optionsMenu(void) = 0;      // Menu actions/logic
    virtual void drawIcon(float scale) = 0;  // Icon rendering (vector)
    virtual void drawIconImg() = 0;          // Theme image rendering
    virtual bool getTheme() = 0;             // Theme availability check
};
```

Main menu construction (`main_menu.cpp`):
- Builds menu list from feature menu instances
- Filters disabled menus via `bruceConfig.disabledMenus`
- Handles navigation via `loopOptions()` in `display.cpp`
- Supports both button input and touchscreen

### Hardware Module Integration

**Runtime Module Detection:**
Modules initialize based on runtime pin configuration:

```cpp
// CC1101 RF module (example)
if (bruceConfigPins.CC1101_bus.mosi != GPIO_NUM_NC) {
    initCC1101once(&spiInstance);
}

// RFID module selection
switch(bruceConfigPins.rfidModule) {
    case PN532_I2C_MODULE: /* init PN532 */
    case RC522_SPI_MODULE: /* init RC522 */
    case M5_RFID2_MODULE:  /* init M5 RFID2 */
    // ...
}
```

**SPI Bus Sharing Strategy:**
The firmware intelligently shares SPI buses to minimize GPIO usage:

```cpp
// If CC1101 shares pins with display, use display's SPI instance
if (CC1101_MOSI == TFT_MOSI)
    initCC1101once(&tft.getSPIinstance());
else if (CC1101_MOSI == SDCARD_MOSI)
    initCC1101once(&sdcardSPI);
else
    initCC1101once(NULL);  // Create dedicated SPI instance
```

### Configuration System (Three Layers)

**Layer 1: Hardware Definition** (`boards/[board]/pins_arduino.h`)
```cpp
#define TFT_CS 37
#define TFT_MOSI 35
#define SDCARD_CS 12
#define GROVE_SDA 2
#define GROVE_SCL 1
```

**Layer 2: Board Interface** (`boards/[board]/interface.cpp`)
```cpp
void _setup_gpio() {
    // Early GPIO (before config loaded)
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);  // SD card power, etc.
}

void _post_setup_gpio() {
    // Late setup (after config loaded, can access bruceConfig)
    Wire1.begin(KEYBOARD_SDA, KEYBOARD_SCL);
}
```

**Layer 3: Runtime Configuration** (`BruceConfigPins` in `/brucePins.conf`)
- User-configurable pins stored in JSON
- Allows changing module pins without reflashing
- Examples: RF/RFID module selection, IR TX/RX pins, I2C/SPI bus assignments

### Global State Management

`globals.h` centralizes shared state:
```cpp
extern bool wifiConnected, BLEConnected, sdcardMounted;
extern volatile bool NextPress, SelPress, EscPress, AnyKeyPress;
extern BruceConfig bruceConfig;          // Persistent settings
extern BruceConfigPins bruceConfigPins;  // Pin configurations
extern TFT_eSprite sprite, draw;         // Display sprites (double buffering)
extern tft_logger tft;                   // TFT wrapper with logging
```

**FreeRTOS Tasks:**
- `taskInputHandler` (priority 2): Non-blocking keyboard/button scanning
- `serialCommandsHandlerTask`: CLI interface
- `interpreterTask` (16KB stack): JavaScript execution

### Build Configuration System

**Multi-Board Support:**
- Each board has an environment in `platformio.ini` (e.g., `[env:m5stack-cardputer]`)
- Board-specific `.ini` files in `boards/[board]/` are auto-included via `extra_configs`
- Custom board definitions in `boards/_boards_json/` extend standard ESP32 boards

**Common Build Flags:**
- `-DBRUCE_VERSION="dev"` (or version from git tag)
- `-DGIT_COMMIT_HASH` (injected by `pre_build_current_year.py`)
- `-DLITE_VERSION=1` (for M5Launcher compatibility, removes large features)
- Hardware flags: `-DHAS_RGB_LED`, `-DMIC_SPM1423`, `-DFM_SI4713`, etc.
- Module pins: `-DCC1101_GDO0_PIN`, `-DTFT_WIDTH`, `-DSDCARD_CS`, etc.

**LITE_VERSION Mode:**
Removes heavy features for devices with limited flash:
```cpp
#if !defined(LITE_VERSION)
    // JavaScript Interpreter
    // SSH client
    // WireGuard VPN
    // BLE beacon scanning
    // Large WiFi features (Responder, ARP attacks)
#endif
```
Saves ~500KB.

## Adding a New Feature Module

1. **Create module directory:** `src/modules/[module_name]/`
2. **Implement module functions** (e.g., `my_feature.cpp/.h`)
3. **Create menu item:**
   - `src/core/menu_items/MyFeatureMenu.cpp/.h`
   - Inherit from `MenuItemInterface`
   - Implement `optionsMenu()`, `drawIcon()`, etc.
4. **Register in main menu:**
   - Add instance in `src/core/main_menu.h`
   - Add to `_menuItems` vector in `main_menu.cpp` constructor
5. **Add dependencies:**
   - Update `platformio.ini` `lib_deps` if external libraries needed
6. **Update documentation:**
   - Add to README.md feature list
   - Update wiki if applicable

**Example Menu Item Structure:**
```cpp
class MyFeatureMenu : public MenuItemInterface {
public:
    void optionsMenu(void);
    void drawIcon(float scale);
    void drawIconImg();
    bool getTheme();
    String getName() { return "MyFeature"; }
};
```

## Adding a New Board

1. **Create board directory:** `boards/[board-name]/`
2. **Define hardware:**
   - `pins_arduino.h`: Pin definitions (TFT, SD, I2C, etc.)
   - `interface.cpp`: Implement `_setup_gpio()` and `_post_setup_gpio()` if needed
3. **Create PlatformIO config:**
   - `[board-name].ini`: Build flags, partition table, lib_deps
   - `boards/_boards_json/[board-name].json`: Board definition (extends esp32dev/esp32-s3, etc.)
4. **Set build flags:**
   - Display: `-DTFT_WIDTH`, `-DTFT_HEIGHT`, `-DTFT_ROTATION`
   - Hardware features: `-DHAS_RGB_LED`, `-DMIC_SPM1423`, `-DHAS_NS4168_SPKR`
   - Module support: `-DUSE_CC1101_VIA_SPI`, `-DUSE_NRF24_VIA_SPI`
   - Pin assignments: `-DSDCARD_CS=12`, `-DCC1101_GDO0_PIN=2`, etc.
5. **Add environment to `platformio.ini`:**
   ```ini
   [env:my-new-board]
   extends = [board].ini
   ```
6. **Test build:**
   ```bash
   pio run -e my-new-board
   ```

**Template:** Use `boards/_New-Device-Model/` as starting point.

## Important Architectural Patterns

### Module Isolation
Each module is self-contained and can be disabled/removed without breaking others:
```
modules/wifi/
├── evil_portal.cpp
├── deauther.cpp
├── sniffer.cpp
└── responder.cpp
```

### Display Abstraction
Supports both TFT displays and headless operation:
```cpp
#if defined(HAS_SCREEN)
    extern tft_logger tft;           // TFT wrapper
    extern TFT_eSprite sprite, draw; // Hardware sprites
#else
    extern SerialDisplayClass &sprite; // Serial output fallback
#endif
```

### Settings Persistence
```
Storage Hierarchy:
1. SD Card: /bruce.conf, /brucePins.conf (if present)
2. LittleFS: /bruce.conf, /brucePins.conf (fallback)
3. EEPROM: Legacy support (deprecated)
```

Loading order: Try SD first, fallback to LittleFS, create defaults if missing.

### Theme System
- Custom boot images (JPG/GIF)
- Custom menu icons (PNG)
- Color schemes (primary, secondary, background)
- Stored in `/theme.json` on SD/LittleFS

## Key Files for Developers

**Essential Files:**
- [src/main.cpp](src/main.cpp) - Entry point, initialization flow
- [include/globals.h](include/globals.h) - Global state declarations
- [src/core/main_menu.cpp](src/core/main_menu.cpp) - Menu system
- [src/core/display.cpp](src/core/display.cpp) - UI rendering (`loopOptions()` navigation)
- [src/core/config.h](src/core/config.h) - `BruceConfig` settings class
- [src/core/configPins.h](src/core/configPins.h) - `BruceConfigPins` pin configuration
- [include/MenuItemInterface.h](include/MenuItemInterface.h) - Menu item interface
- [include/precompiler_flags.h](include/precompiler_flags.h) - Default build flags
- [platformio.ini](platformio.ini) - Build configuration
- [build.py](build.py) - Post-build binary merging

**Board-Specific:**
- [boards/[board]/interface.cpp](boards/_New-Device-Model/interface.cpp) - GPIO initialization
- [boards/[board]/pins_arduino.h](boards/_New-Device-Model/pins_arduino.h) - Pin definitions
- [boards/[board]/[board].ini](boards/_New-Device-Model/_New-Device-Model.ini) - Build config

## Common Pitfalls & Notes

### Memory Management
- ESP32 has limited RAM; use PSRAM when available
- Large buffers (e.g., audio, images) should use PSRAM via `ps_malloc()`
- Sprites use PSRAM if available, otherwise DRAM

### SPI Bus Conflicts
- Always check if module shares SPI bus with TFT/SD before initializing
- Use `initCC1101once()`, `initRfid()` patterns that handle bus sharing
- Never create multiple SPI instances on same pins

### GPIO Conflicts
- Check `pins_arduino.h` for pin assignments before adding hardware
- Use `GPIO_NUM_NC` (not connected) to disable optional features
- Some boards share I2C bus between display, RTC, and expansion

### Compiler Warnings
- The build system enables strict warnings (`-Wall`, `-Wclobbered`, etc.)
- Fix warnings before submitting PRs
- `-Wno-conversion-null` is disabled globally (legacy code)

### LITE_VERSION Compatibility
- If adding large features (>50KB), wrap in `#if !defined(LITE_VERSION)`
- M5Launcher requires LITE_VERSION builds for some devices
- Check `platformio.ini` for LITE environments (e.g., `LAUNCHER_*`)

### Partition Tables
- 4MB: `custom_4Mb.csv` (small flash devices)
- 8MB: `custom_8Mb.csv` (most devices)
- 16MB: `custom_16Mb.csv` (large flash devices)
- OTA partition size is validated in `build.py`

### Testing
- Test on actual hardware when possible (emulator not available)
- Check serial output: `pio device monitor --baud 115200`
- Use WebUI for remote access/debugging (`bruceConfig.webUI.user`)
- CLI available via serial for scripting/automation

## Serial CLI Interface

The firmware includes a comprehensive CLI accessible via serial (115200 baud):

```bash
# Connect via serial
pio device monitor --baud 115200

# Example commands (see src/core/serial_commands/ for full list)
help                    # List all commands
wifi scan               # Scan for WiFi networks
wifi connect SSID PASS  # Connect to WiFi
rf tx 433920000 file.sub # Transmit RF signal
badusb run script.txt   # Execute BadUSB script
settings list           # Show current settings
```

Implemented via `SimpleCLI` library in `src/core/serial_commands/`.

## Version 2.0 Roadmap

Current development focuses on:
- **Arduino-ESP32 v3.x** framework (ESP-IDF 5.5) ✅
- **ESP32-C5 support** (5GHz WiFi) ✅
- Planned features:
  - RFID emulation on PN532
  - NRF24 mousejacking/keyboard jacking
  - BLE session hijacking
  - RF RollJam attacks
- Refactoring goals:
  - Common input handler (reduce per-device code)
  - TFT middleware layer (support ArduinoGFX, LovyanGFX)
  - Move FM radio to JavaScript module

See [2.0_road_path.md](2.0_road_path.md) for full roadmap.

## License & Disclaimer

This software is distributed under the **AGPL v3 License**. It is intended for **legal and authorized security testing only**. Use of this software for malicious or unauthorized activities is strictly prohibited.

**Important:** This is offensive security firmware. Always:
- Obtain written authorization before testing
- Operate within legal boundaries
- Follow responsible disclosure practices
- Understand local RF/wireless regulations
