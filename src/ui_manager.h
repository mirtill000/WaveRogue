// =============================================================================
// WaveRogue - ui_manager.h
//
// Menu + scrolling log UI for the Cardputer's 240x135 TFT, driven by its
// built-in keyboard. Navigation is two levels deep:
//
//   MENU_TOP  -->  MENU_LORA, MENU_SUBGHZ (band select) or MENU_NFC  -->  a leaf module
//
// Both the top menu and the category submenus (including the Sub-GHz
// band-select list) are rendered by the same generic, scrollable
// pollListMenu() widget.
// =============================================================================
#pragma once
#include <Arduino.h>

enum class AppState {
    MENU_TOP = 0,
    MENU_LORA,
    MENU_SUBGHZ,
    MENU_NFC,

    // ---- LoRa leaf modules ----
    LORA_SNIFFER,           // Module: LoRaWAN Sniffer & Meta-Analyzer
    LORA_WARDRIVE,          // Module: LoRa Wardriving & Heat-Mapper
    LORA_ROGUE_GW,          // Module: Rogue LoRa Gateway Emulator (Honeypot)
    LORA_DEVADDR_SCAN,      // Module: DevAddr Mapper (Device Address Scanner)
    LORA_NETID_ID,          // Module: NetID Extractor (Provider Identifier)
    LORA_BEACON_SCAN,       // Module: Class-B Gateway Beacon Scanner
    LORA_PLAINTEXT_DETECT,  // Module: Plaintext/Weak-Crypto Payload Detector
    LORA_GWMP_SNIFF,        // Module: Gateway Backhaul (GWMP) Metadata Extractor

    // ---- Sub-GHz leaf module ----
    // MENU_SUBGHZ is the band-select list (315/433/868/915 MHz); picking a
    // band enters this single leaf state, with the chosen band threaded
    // through main.cpp into SubGhzAudit::begin().
    SUBGHZ_AUDIT,            // Module: Sub-GHz Audit (band-scoped sweep/lock/decode)

    // ---- NFC leaf modules ----
    NFC_READER               // Module: NFC Reader/Writer + Mifare default-key auditor
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

    // Persistent one-line status indicator pinned to the bottom of the
    // screen (below the scrolling log), with a small spinner so it's
    // visibly "alive" even when the text itself doesn't change tick to
    // tick - e.g. "Acquiring GPS satellites...", "Scanning...",
    // "Listening for beacon...". Cheap to call every loop() iteration:
    // internally throttled to redraw only when the text changes or
    // enough time has passed to advance the spinner.
    void setStatus(const String& text);
}
