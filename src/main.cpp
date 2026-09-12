// =============================================================================
// WaveRogue - main.cpp
//
// RF security-auditing firmware for the M5Stack Cardputer (ESP32-S3).
// FOR AUTHORIZED SECURITY AUDITING AND EDUCATIONAL USE ONLY - see README.md.
//
// This file only wires the pieces together: draw the menu, dispatch into
// whichever module is selected, and return to the menu on ESC/back. All
// actual radio/protocol logic lives in lora_auditor.* and subghz_auditor.*.
// =============================================================================
#include <M5Cardputer.h>
#include "ui_manager.h"
#include "lora_auditor.h"
#include "subghz_auditor.h"

namespace {
    AppState currentState = AppState::MENU;
    bool moduleInitialized = false;

    void enterModule(AppState state) {
        UIManager::clearLog();
        switch (state) {
            case AppState::MODULE_LORA_SNIFFER:
                UIManager::drawHeader("LoRaWAN Sniffer");
                LoraAuditor::begin();
                break;
            case AppState::MODULE_LORA_WARDRIVE:
                UIManager::drawHeader("LoRa Wardriver");
                LoraAuditor::begin();
                LoraAuditor::wardriveBegin();
                break;
            case AppState::MODULE_LORA_ROGUE_GW:
                UIManager::drawHeader("Rogue LoRa GW");
                LoraAuditor::begin();
                LoraAuditor::rogueGatewayBegin();
                break;
            case AppState::MODULE_SUBGHZ_SNIFFER:
                UIManager::drawHeader("Sub-GHz Sniffer");
                SubGhzAuditor::begin();
                SubGhzAuditor::sniffBegin();
                break;
            case AppState::MODULE_SUBGHZ_REPLAY:
                UIManager::drawHeader("Sub-GHz Replay Test");
                SubGhzAuditor::begin();
                SubGhzAuditor::replayBegin();
                break;
            default:
                break;
        }
        moduleInitialized = true;
    }

    void exitModule(AppState state) {
        switch (state) {
            case AppState::MODULE_LORA_SNIFFER:
                break; // nothing to tear down beyond leaving RX mode
            case AppState::MODULE_LORA_WARDRIVE:
                LoraAuditor::wardriveEnd();
                break;
            case AppState::MODULE_LORA_ROGUE_GW:
                LoraAuditor::rogueGatewayEnd();
                break;
            case AppState::MODULE_SUBGHZ_SNIFFER:
                SubGhzAuditor::sniffEnd();
                break;
            case AppState::MODULE_SUBGHZ_REPLAY:
                SubGhzAuditor::replayEnd();
                break;
            default:
                break;
        }
        moduleInitialized = false;
    }
}

void setup() {
    Serial.begin(115200);
    UIManager::begin();
}

void loop() {
    if (currentState == AppState::MENU) {
        AppState next = UIManager::pollMenu();
        if (next != AppState::MENU) {
            currentState = next;
            enterModule(currentState);
        }
        return;
    }

    // Inside a module: check for ESC/back first so we never get stuck.
    if (UIManager::backPressed()) {
        exitModule(currentState);
        currentState = AppState::MENU;
        UIManager::begin(); // redraw main menu
        return;
    }

    switch (currentState) {
        case AppState::MODULE_LORA_SNIFFER:
            LoraAuditor::sniffLoop();
            break;
        case AppState::MODULE_LORA_WARDRIVE:
            LoraAuditor::wardriveLoop();
            break;
        case AppState::MODULE_LORA_ROGUE_GW:
            LoraAuditor::rogueGatewayLoop();
            break;
        case AppState::MODULE_SUBGHZ_SNIFFER:
            SubGhzAuditor::sniffLoop();
            break;
        case AppState::MODULE_SUBGHZ_REPLAY:
            SubGhzAuditor::replayLoop();
            break;
        default:
            break;
    }
}
