// =============================================================================
// WaveRogue - ui_manager.h
//
// Menu + scrolling log UI for the Cardputer's 240x135 TFT, driven by its
// built-in keyboard. The tool now has 16 leaf modules, too many for one
// flat screen, so navigation is two levels deep:
//
//   MENU_TOP  -->  MENU_LORA or MENU_SUBGHZ  -->  a leaf module
//
// Both the top menu and the two category submenus are rendered by the same
// generic, scrollable pollListMenu() widget.
// =============================================================================
#pragma once
#include <Arduino.h>

enum class AppState {
    MENU_TOP = 0,
    MENU_LORA,
    MENU_SUBGHZ,

    // ---- LoRa leaf modules ----
    LORA_SNIFFER,           // Module: LoRaWAN Sniffer & Meta-Analyzer
    LORA_WARDRIVE,          // Module: LoRa Wardriving & Heat-Mapper
    LORA_ROGUE_GW,          // Module: Rogue LoRa Gateway Emulator (Honeypot)
    LORA_DEVADDR_SCAN,      // Module: DevAddr Mapper (Device Address Scanner)
    LORA_NETID_ID,          // Module: NetID Extractor (Provider Identifier)
    LORA_BEACON_SCAN,       // Module: Class-B Gateway Beacon Scanner
    LORA_PLAINTEXT_DETECT,  // Module: Plaintext/Weak-Crypto Payload Detector
    LORA_GWMP_SNIFF,        // Module: Gateway Backhaul (GWMP) Metadata Extractor

    // ---- Sub-GHz leaf modules ----
    SUBGHZ_SNIFFER,          // Module: Raw Sniffer & Protocol Analyzer (OOK/ASK)
    SUBGHZ_REPLAY,           // Module: Replay Vulnerability Tester
    SUBGHZ_STATIC_CODE,      // Module: Static-Code Legacy System Discovery
    SUBGHZ_WEATHER_TPMS,     // Module: Weather/TPMS Telemetry Decoder
    SUBGHZ_WMBUS_SCAN,       // Module: Wireless M-Bus Smart Meter Scanner
    SUBGHZ_BUG_DETECT,       // Module: Analog Bug / Carrier Detector
    SUBGHZ_POCSAG_SCAN,      // Module: POCSAG/FLEX Pager Scanner
    SUBGHZ_SYNCWORD_ANALYZER // Module: Preamble/Sync-Word Analyzer
};

// Keys used for navigation. The Cardputer keyboard has no dedicated arrow
// keys; these map to the punctuation row commonly used as arrow-emulation
// in M5Cardputer example sketches. Adjust if your keyboard firmware/layout
// differs.
namespace UIKeys {
    constexpr char UP     = ';';
    constexpr char DOWN   = '.';
    constexpr char LEFT   = ',';
    constexpr char RIGHT  = '/';
    constexpr char BACK   = '`';   // acts as "ESC" -> go up one menu level
}

namespace UIManager {
    // One-time hardware bring-up (display + keyboard). Call once in setup().
    void begin();

    // Reads the keyboard exactly once. Call this ONE time per loop()
    // iteration, before any of the query functions below or pollListMenu().
    void pollInput();

    bool isUp();
    bool isDown();
    bool isEnter();
    bool isBack();

    // Call when (re-)entering any list menu (top menu, a category submenu)
    // so it redraws from scratch with the selection reset to the top.
    void resetMenu();

    // Generic scrollable menu widget. `title` is only used for the header
    // text - selection/scroll state is a single shared instance reset via
    // resetMenu(), since only one list menu is ever visible at a time.
    // Returns the selected index once Enter is pressed, otherwise -1.
    int pollListMenu(const char* title, const char* const* items, int count);

    void drawHeader(const char* title);
    void printLine(const String& line);   // appends a line to the scrolling log area
    void clearLog();
}
