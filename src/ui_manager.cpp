#include "ui_manager.h"
#include <M5Cardputer.h>

namespace {
    // ---- Keyboard state, captured once per loop() tick by pollInput() ----
    bool hadChange = false;
    Keyboard_Class::KeysState keys;

    bool wordHas(char c) {
        if (!hadChange) return false;
        for (auto k : keys.word) {
            if (k == c) return true;
        }
        return false;
    }

    // ---- Scrolling log buffer for module screens ----
    constexpr int kMaxLogLines = 8;
    String logLines[kMaxLogLines];
    int logCount = 0;

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

    // ---- Generic scrollable list menu state ----
    constexpr int kVisibleRows = 6;
    constexpr int kRowHeight = 17;
    int selIndex = 0;
    int scrollOffset = 0;
    bool menuNeedsRedraw = true;

    void redrawListMenu(const char* title, const char* const* items, int count) {
        auto& d = M5Cardputer.Display;
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_CYAN, TFT_BLACK);
        d.setTextSize(1);
        d.setCursor(4, 2);
        d.println(title);
        d.drawFastHLine(0, 12, d.width(), TFT_DARKGREY);

        for (int row = 0; row < kVisibleRows; row++) {
            int i = scrollOffset + row;
            if (i >= count) break;
            int y = 15 + row * kRowHeight;
            if (i == selIndex) {
                d.fillRect(0, y, d.width(), kRowHeight - 1, TFT_DARKGREEN);
                d.setTextColor(TFT_WHITE, TFT_DARKGREEN);
            } else {
                d.setTextColor(TFT_GREEN, TFT_BLACK);
            }
            d.setCursor(6, y + 3);
            d.println(items[i]);
        }

        // Scroll indicators.
        d.setTextColor(TFT_YELLOW, TFT_BLACK);
        if (scrollOffset > 0) {
            d.setCursor(d.width() - 10, 15);
            d.print("^");
        }
        if (scrollOffset + kVisibleRows < count) {
            d.setCursor(d.width() - 10, d.height() - 12);
            d.print("v");
        }
    }
}

void UIManager::begin() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
}

void UIManager::pollInput() {
    M5Cardputer.update();
    hadChange = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
    if (hadChange) {
        keys = M5Cardputer.Keyboard.keysState();
    }
}

bool UIManager::isUp()    { return wordHas(UIKeys::UP); }
bool UIManager::isDown()  { return wordHas(UIKeys::DOWN); }
bool UIManager::isBack()  { return wordHas(UIKeys::BACK); }
bool UIManager::isEnter() { return hadChange && keys.enter; }

void UIManager::resetMenu() {
    selIndex = 0;
    scrollOffset = 0;
    menuNeedsRedraw = true;
}

int UIManager::pollListMenu(const char* title, const char* const* items, int count) {
    bool changed = menuNeedsRedraw;
    menuNeedsRedraw = false;

    if (isUp() && count > 0) {
        selIndex = (selIndex - 1 + count) % count;
        changed = true;
    } else if (isDown() && count > 0) {
        selIndex = (selIndex + 1) % count;
        changed = true;
    }

    if (selIndex < scrollOffset) {
        scrollOffset = selIndex;
    } else if (selIndex >= scrollOffset + kVisibleRows) {
        scrollOffset = selIndex - kVisibleRows + 1;
    }

    if (changed) {
        redrawListMenu(title, items, count);
    }

    if (isEnter() && count > 0) {
        return selIndex;
    }
    return -1;
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
