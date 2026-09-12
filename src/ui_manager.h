// =============================================================================
// WaveRogue - ui_manager.h
//
// Minimal menu + scrolling log UI for the Cardputer's 240x135 TFT, driven by
// its built-in keyboard. Kept deliberately dumb (no animations, no icons) so
// it stays legible at this resolution and easy to extend.
// =============================================================================
#pragma once
#include <Arduino.h>

enum class AppState {
    MENU = 0,
    MODULE_LORA_SNIFFER,
    MODULE_LORA_WARDRIVE,
    MODULE_LORA_ROGUE_GW,
    MODULE_SUBGHZ_SNIFFER,
    MODULE_SUBGHZ_REPLAY,
    MODULE_COUNT
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
    constexpr char BACK   = '`';   // acts as "ESC" -> return to main menu
}

namespace UIManager {
    void begin();

    // Main menu: returns the newly selected AppState once the user presses
    // Enter, or AppState::MENU if still browsing (call every loop() while
    // in the MENU state).
    AppState pollMenu();

    // Returns true if the user pressed the BACK/ESC key this poll - modules
    // should check this every iteration of their own loop and, if true,
    // clean up and return control to main.cpp so it can go back to MENU.
    bool backPressed();

    // Returns true once (edge-triggered) if Enter/Return was pressed - used
    // by Module 5 to trigger a replay transmission on demand.
    bool enterPressed();

    void drawHeader(const char* title);
    void printLine(const String& line);   // appends a line to the scrolling log area
    void clearLog();
}
