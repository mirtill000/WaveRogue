#include "ui_manager.h"
#include <M5Cardputer.h>

namespace {
    const char* kMenuItems[] = {
        "1. LoRaWAN Sniffer",
        "2. LoRa Wardriver (GPS+SD)",
        "3. Rogue LoRa GW (Honeypot)",
        "4. Sub-GHz Sniffer (OOK/ASK)",
        "5. Sub-GHz Replay Tester",
    };
    constexpr int kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

    int selectedIndex = 0;

    // Scrolling log buffer for module screens.
    constexpr int kMaxLogLines = 8;
    String logLines[kMaxLogLines];
    int logCount = 0;

    void redrawMenu() {
        auto& d = M5Cardputer.Display;
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_GREEN, TFT_BLACK);
        d.setTextSize(1);
        d.setCursor(4, 2);
        d.println("WaveRogue - RF Auditor");
        d.drawFastHLine(0, 12, d.width(), TFT_DARKGREY);

        for (int i = 0; i < kMenuCount; i++) {
            int y = 18 + i * 20;
            if (i == selectedIndex) {
                d.fillRect(0, y - 2, d.width(), 18, TFT_DARKGREEN);
                d.setTextColor(TFT_WHITE, TFT_DARKGREEN);
            } else {
                d.setTextColor(TFT_GREEN, TFT_BLACK);
            }
            d.setCursor(6, y);
            d.println(kMenuItems[i]);
        }
    }

    void redrawLog() {
        auto& d = M5Cardputer.Display;
        // Log area starts below the header (drawn separately by drawHeader).
        d.fillRect(0, 14, d.width(), d.height() - 14, TFT_BLACK);
        d.setTextColor(TFT_GREEN, TFT_BLACK);
        d.setTextSize(1);
        for (int i = 0; i < logCount; i++) {
            d.setCursor(2, 16 + i * 14);
            d.println(logLines[i]);
        }
    }
}

void UIManager::begin() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    redrawMenu();
}

AppState UIManager::pollMenu() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return AppState::MENU;
    }

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    bool changed = false;

    for (auto c : status.word) {
        if (c == UIKeys::UP) {
            selectedIndex = (selectedIndex - 1 + kMenuCount) % kMenuCount;
            changed = true;
        } else if (c == UIKeys::DOWN) {
            selectedIndex = (selectedIndex + 1) % kMenuCount;
            changed = true;
        }
    }

    if (changed) {
        redrawMenu();
    }

    if (status.enter) {
        // AppState::MENU == 0, module states start at 1.
        return static_cast<AppState>(selectedIndex + 1);
    }

    return AppState::MENU;
}

bool UIManager::backPressed() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return false;
    }
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    for (auto c : status.word) {
        if (c == UIKeys::BACK) return true;
    }
    return false;
}

bool UIManager::enterPressed() {
    // NOTE: does not call M5Cardputer.update() itself - callers typically
    // already polled backPressed()/keyboard this cycle. Modules that only
    // need Enter (no back-check in the same tick) should call
    // M5Cardputer.update() before this.
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return false;
    }
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    return status.enter;
}

void UIManager::drawHeader(const char* title) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setTextSize(1);
    d.setCursor(4, 2);
    d.println(title);
    d.drawFastHLine(0, 12, d.width(), TFT_DARKGREY);
    clearLog();
}

void UIManager::printLine(const String& line) {
    if (logCount < kMaxLogLines) {
        logLines[logCount++] = line;
    } else {
        // Scroll: drop oldest line.
        for (int i = 1; i < kMaxLogLines; i++) logLines[i - 1] = logLines[i];
        logLines[kMaxLogLines - 1] = line;
    }
    redrawLog();
    Serial.println(line);
}

void UIManager::clearLog() {
    logCount = 0;
    redrawLog();
}
