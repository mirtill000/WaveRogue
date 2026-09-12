// =============================================================================
// WaveRogue - main.cpp
//
// RF security-auditing firmware for the M5Stack Cardputer (ESP32-S3).
// FOR AUTHORIZED SECURITY AUDITING AND EDUCATIONAL USE ONLY - see README.md.
//
// This file only wires the pieces together: draw the two-level menu
// (category -> leaf module), dispatch into whichever module is selected,
// and return one level up on ESC/back. All actual radio/protocol logic
// lives in the per-module .cpp files.
// =============================================================================
#include <M5Cardputer.h>
#include "ui_manager.h"
#include "lora_auditor.h"
#include "lora_beacon_scanner.h"
#include "gwmp_sniffer.h"
#include "subghz_auditor.h"
#include "subghz_static_code.h"
#include "subghz_weather_decoder.h"
#include "subghz_wmbus_scanner.h"
#include "subghz_bug_detector.h"
#include "subghz_pocsag_scanner.h"
#include "subghz_syncword_analyzer.h"

namespace {
    const char* kTopMenuItems[] = {
        "LoRa Tools",
        "Sub-GHz Tools",
    };
    constexpr int kTopMenuCount = sizeof(kTopMenuItems) / sizeof(kTopMenuItems[0]);

    const char* kLoraMenuItems[] = {
        "1. Sniffer & Meta-Analyzer",
        "2. Wardriver (GPS+SD)",
        "3. Rogue Gateway (Honeypot)",
        "4. DevAddr Mapper",
        "5. NetID / Provider ID",
        "6. Class-B Beacon Scanner",
        "7. Plaintext Payload Detect",
        "8. GWMP Gateway Metadata",
    };
    const AppState kLoraModuleStates[] = {
        AppState::LORA_SNIFFER,          AppState::LORA_WARDRIVE,
        AppState::LORA_ROGUE_GW,         AppState::LORA_DEVADDR_SCAN,
        AppState::LORA_NETID_ID,         AppState::LORA_BEACON_SCAN,
        AppState::LORA_PLAINTEXT_DETECT, AppState::LORA_GWMP_SNIFF,
    };
    constexpr int kLoraMenuCount = sizeof(kLoraMenuItems) / sizeof(kLoraMenuItems[0]);

    const char* kSubghzMenuItems[] = {
        "1. Raw Sniffer (OOK/ASK)",
        "2. Replay Vuln. Tester",
        "3. Static-Code Discovery",
        "4. Weather/TPMS Decoder",
        "5. Wireless M-Bus Scanner",
        "6. Analog Bug Detector",
        "7. POCSAG Pager Scanner",
        "8. Sync-Word Analyzer",
    };
    const AppState kSubghzModuleStates[] = {
        AppState::SUBGHZ_SNIFFER,      AppState::SUBGHZ_REPLAY,
        AppState::SUBGHZ_STATIC_CODE,  AppState::SUBGHZ_WEATHER_TPMS,
        AppState::SUBGHZ_WMBUS_SCAN,   AppState::SUBGHZ_BUG_DETECT,
        AppState::SUBGHZ_POCSAG_SCAN,  AppState::SUBGHZ_SYNCWORD_ANALYZER,
    };
    constexpr int kSubghzMenuCount = sizeof(kSubghzMenuItems) / sizeof(kSubghzMenuItems[0]);

    AppState currentState = AppState::MENU_TOP;

    AppState parentMenuOf(AppState s) {
        switch (s) {
            case AppState::LORA_SNIFFER:
            case AppState::LORA_WARDRIVE:
            case AppState::LORA_ROGUE_GW:
            case AppState::LORA_DEVADDR_SCAN:
            case AppState::LORA_NETID_ID:
            case AppState::LORA_BEACON_SCAN:
            case AppState::LORA_PLAINTEXT_DETECT:
            case AppState::LORA_GWMP_SNIFF:
                return AppState::MENU_LORA;
            default:
                return AppState::MENU_SUBGHZ;
        }
    }

    void enterModule(AppState state) {
        UIManager::clearLog();
        switch (state) {
            case AppState::LORA_SNIFFER:
                UIManager::drawHeader("LoRaWAN Sniffer");
                LoraAuditor::begin();
                break;
            case AppState::LORA_WARDRIVE:
                UIManager::drawHeader("LoRa Wardriver");
                LoraAuditor::begin();
                LoraAuditor::wardriveBegin();
                break;
            case AppState::LORA_ROGUE_GW:
                UIManager::drawHeader("Rogue LoRa GW");
                LoraAuditor::begin();
                LoraAuditor::rogueGatewayBegin();
                break;
            case AppState::LORA_DEVADDR_SCAN:
                UIManager::drawHeader("DevAddr Mapper");
                LoraAuditor::begin();
                LoraAuditor::devAddrScanBegin();
                break;
            case AppState::LORA_NETID_ID:
                UIManager::drawHeader("NetID Extractor");
                LoraAuditor::begin();
                LoraAuditor::netIdScanBegin();
                break;
            case AppState::LORA_BEACON_SCAN:
                UIManager::drawHeader("Class-B Beacon Scan");
                LoraBeaconScanner::begin();
                break;
            case AppState::LORA_PLAINTEXT_DETECT:
                UIManager::drawHeader("Plaintext Detector");
                LoraAuditor::begin();
                LoraAuditor::plaintextScanBegin();
                break;
            case AppState::LORA_GWMP_SNIFF:
                UIManager::drawHeader("GWMP Metadata (WiFi)");
                GwmpSniffer::begin();
                break;

            case AppState::SUBGHZ_SNIFFER:
                UIManager::drawHeader("Sub-GHz Sniffer");
                SubGhzAuditor::begin();
                SubGhzAuditor::sniffBegin();
                break;
            case AppState::SUBGHZ_REPLAY:
                UIManager::drawHeader("Sub-GHz Replay Test");
                SubGhzAuditor::begin();
                SubGhzAuditor::replayBegin();
                break;
            case AppState::SUBGHZ_STATIC_CODE:
                UIManager::drawHeader("Static-Code Discovery");
                SubGhzStaticCode::begin();
                break;
            case AppState::SUBGHZ_WEATHER_TPMS:
                UIManager::drawHeader("Weather/TPMS Decoder");
                SubGhzWeatherDecoder::begin();
                break;
            case AppState::SUBGHZ_WMBUS_SCAN:
                UIManager::drawHeader("Wireless M-Bus Scan");
                SubGhzWmbusScanner::begin();
                break;
            case AppState::SUBGHZ_BUG_DETECT:
                UIManager::drawHeader("Analog Bug Detector");
                SubGhzBugDetector::begin();
                break;
            case AppState::SUBGHZ_POCSAG_SCAN:
                UIManager::drawHeader("POCSAG Pager Scan");
                SubGhzPocsagScanner::begin();
                break;
            case AppState::SUBGHZ_SYNCWORD_ANALYZER:
                UIManager::drawHeader("Sync-Word Analyzer");
                SubGhzSyncwordAnalyzer::begin();
                break;
            default:
                break;
        }
    }

    void exitModule(AppState state) {
        switch (state) {
            case AppState::LORA_WARDRIVE:
                LoraAuditor::wardriveEnd();
                break;
            case AppState::LORA_ROGUE_GW:
                LoraAuditor::rogueGatewayEnd();
                break;
            case AppState::LORA_DEVADDR_SCAN:
                LoraAuditor::devAddrScanEnd();
                break;
            case AppState::LORA_NETID_ID:
                LoraAuditor::netIdScanEnd();
                break;
            case AppState::LORA_BEACON_SCAN:
                LoraBeaconScanner::end();
                break;
            case AppState::LORA_PLAINTEXT_DETECT:
                LoraAuditor::plaintextScanEnd();
                break;
            case AppState::LORA_GWMP_SNIFF:
                GwmpSniffer::end();
                break;

            case AppState::SUBGHZ_SNIFFER:
                SubGhzAuditor::sniffEnd();
                break;
            case AppState::SUBGHZ_REPLAY:
                SubGhzAuditor::replayEnd();
                break;
            case AppState::SUBGHZ_STATIC_CODE:
                SubGhzStaticCode::end();
                break;
            case AppState::SUBGHZ_WEATHER_TPMS:
                SubGhzWeatherDecoder::end();
                break;
            case AppState::SUBGHZ_WMBUS_SCAN:
                SubGhzWmbusScanner::end();
                break;
            case AppState::SUBGHZ_BUG_DETECT:
                SubGhzBugDetector::end();
                break;
            case AppState::SUBGHZ_POCSAG_SCAN:
                SubGhzPocsagScanner::end();
                break;
            case AppState::SUBGHZ_SYNCWORD_ANALYZER:
                SubGhzSyncwordAnalyzer::end();
                break;
            default:
                break; // LORA_SNIFFER: nothing to tear down beyond leaving RX mode
        }
    }

    void dispatchLoop(AppState state) {
        switch (state) {
            case AppState::LORA_SNIFFER:            LoraAuditor::sniffLoop(); break;
            case AppState::LORA_WARDRIVE:            LoraAuditor::wardriveLoop(); break;
            case AppState::LORA_ROGUE_GW:            LoraAuditor::rogueGatewayLoop(); break;
            case AppState::LORA_DEVADDR_SCAN:        LoraAuditor::devAddrScanLoop(); break;
            case AppState::LORA_NETID_ID:            LoraAuditor::netIdScanLoop(); break;
            case AppState::LORA_BEACON_SCAN:         LoraBeaconScanner::loop(); break;
            case AppState::LORA_PLAINTEXT_DETECT:    LoraAuditor::plaintextScanLoop(); break;
            case AppState::LORA_GWMP_SNIFF:          GwmpSniffer::loop(); break;

            case AppState::SUBGHZ_SNIFFER:           SubGhzAuditor::sniffLoop(); break;
            case AppState::SUBGHZ_REPLAY:            SubGhzAuditor::replayLoop(); break;
            case AppState::SUBGHZ_STATIC_CODE:       SubGhzStaticCode::loop(); break;
            case AppState::SUBGHZ_WEATHER_TPMS:      SubGhzWeatherDecoder::loop(); break;
            case AppState::SUBGHZ_WMBUS_SCAN:        SubGhzWmbusScanner::loop(); break;
            case AppState::SUBGHZ_BUG_DETECT:        SubGhzBugDetector::loop(); break;
            case AppState::SUBGHZ_POCSAG_SCAN:       SubGhzPocsagScanner::loop(); break;
            case AppState::SUBGHZ_SYNCWORD_ANALYZER: SubGhzSyncwordAnalyzer::loop(); break;
            default:
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    UIManager::begin();
    UIManager::resetMenu();
}

void loop() {
    UIManager::pollInput();

    switch (currentState) {
        case AppState::MENU_TOP: {
            int sel = UIManager::pollListMenu("WaveRogue - RF Auditor", kTopMenuItems, kTopMenuCount);
            if (sel == 0) {
                currentState = AppState::MENU_LORA;
                UIManager::resetMenu();
            } else if (sel == 1) {
                currentState = AppState::MENU_SUBGHZ;
                UIManager::resetMenu();
            }
            break;
        }
        case AppState::MENU_LORA: {
            if (UIManager::isBack()) {
                currentState = AppState::MENU_TOP;
                UIManager::resetMenu();
                break;
            }
            int sel = UIManager::pollListMenu("LoRa Tools", kLoraMenuItems, kLoraMenuCount);
            if (sel >= 0) {
                currentState = kLoraModuleStates[sel];
                enterModule(currentState);
            }
            break;
        }
        case AppState::MENU_SUBGHZ: {
            if (UIManager::isBack()) {
                currentState = AppState::MENU_TOP;
                UIManager::resetMenu();
                break;
            }
            int sel = UIManager::pollListMenu("Sub-GHz Tools", kSubghzMenuItems, kSubghzMenuCount);
            if (sel >= 0) {
                currentState = kSubghzModuleStates[sel];
                enterModule(currentState);
            }
            break;
        }
        default: {
            // Inside a leaf module: check for ESC/back first so we never
            // get stuck, then go back up to whichever category menu we
            // came from (not all the way to the top - nicer to navigate).
            if (UIManager::isBack()) {
                AppState parent = parentMenuOf(currentState);
                exitModule(currentState);
                currentState = parent;
                UIManager::resetMenu();
                break;
            }
            dispatchLoop(currentState);
            break;
        }
    }
}
