#include "subghz_bug_detector.h"
#include "config.h"
#include "ui_manager.h"
#include "subghz_rf_switch.h"
#include <RadioLib.h>
#include <SPI.h>

namespace {
    SPIClass subghzSPI(HSPI);
    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, RADIOLIB_NC, subghzSPI);

    constexpr float kFreqList[] = BUG_SCAN_FREQ_LIST_MHZ;
    constexpr int kFreqCount = sizeof(kFreqList) / sizeof(kFreqList[0]);

    int currentFreqIdx = 0;
    uint32_t freqEnteredAt = 0;
    uint32_t aboveThresholdSince = 0; // 0 = not currently above threshold
    bool flaggedThisFreq = false;
    uint32_t lastStatusUpdate = 0;

    void tuneTo(int idx) {
        // Hopping between the 433MHz and 868/915MHz entries in the list
        // means the antenna path has to be re-selected on every hop, not
        // just once at begin() - see subghz_rf_switch.h.
        SubGhzRfSwitch::selectForFrequency(kFreqList[idx]);
        radio.setFrequency(kFreqList[idx]);
        radio.startReceive();
        freqEnteredAt = millis();
        aboveThresholdSince = 0;
        flaggedThisFreq = false;
    }
}

bool SubGhzBugDetector::begin() {
    subghzSPI.begin(SUBGHZ_SPI_SCK_PIN, SUBGHZ_SPI_MISO_PIN, SUBGHZ_SPI_MOSI_PIN, SUBGHZ_CS_PIN);

    int state = radio.begin(kFreqList[0], 4.8f, 48.0f, 135.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    currentFreqIdx = 0;
    tuneTo(0);
    UIManager::printLine("Scanning for continuous");
    UIManager::printLine("carriers (possible bugs)...");
    UIManager::printLine("(CC1101 range only: 300-348,");
    UIManager::printLine(" 387-464, 779-928 MHz)");
    return true;
}

void SubGhzBugDetector::loop() {
    float rssi = radio.getRSSI();
    uint32_t now = millis();
    bool above = rssi > BUG_CARRIER_RSSI_THRESHOLD_DBM;

    if (above) {
        if (aboveThresholdSince == 0) aboveThresholdSince = now;
        uint32_t dur = now - aboveThresholdSince;
        if (dur >= (uint32_t)BUG_CARRIER_MIN_DURATION_MS && !flaggedThisFreq) {
            flaggedThisFreq = true;
            UIManager::clearLog();
            UIManager::printLine("[!] CONTINUOUS CARRIER!");
            UIManager::printLine(String(kFreqList[currentFreqIdx], 3) + "MHz for " +
                                  String(dur / 1000.0f, 1) + "s");
            UIManager::printLine("RSSI: " + String(rssi, 0) + "dBm");
            UIManager::printLine("Possible active bug/Tx.");
        }
    } else {
        aboveThresholdSince = 0;
    }

    if (!flaggedThisFreq && now - lastStatusUpdate > 300) {
        lastStatusUpdate = now;
        UIManager::clearLog();
        UIManager::printLine("Freq: " + String(kFreqList[currentFreqIdx], 3) + "MHz (" +
                              String(currentFreqIdx + 1) + "/" + String(kFreqCount) + ")");
        UIManager::printLine("RSSI: " + String(rssi, 0) + "dBm");
        UIManager::printLine(above ? "Signal present..." : "(quiet)");
    }

    // Give a suspicious signal the full detection window before hopping
    // away; otherwise cycle through the list at the normal dwell time.
    uint32_t dwellLimit = above ? ((uint32_t)BUG_CARRIER_MIN_DURATION_MS + 1000) : (uint32_t)BUG_SCAN_DWELL_MS;
    if (now - freqEnteredAt > dwellLimit) {
        currentFreqIdx = (currentFreqIdx + 1) % kFreqCount;
        tuneTo(currentFreqIdx);
    }
}

void SubGhzBugDetector::end() {
    radio.standby();
}
