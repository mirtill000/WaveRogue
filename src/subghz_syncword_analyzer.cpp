#include "subghz_syncword_analyzer.h"
#include "config.h"
#include "ui_manager.h"
#include <RadioLib.h>
#include <Arduino.h>

namespace {
    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, SUBGHZ_GDO2_PIN);

    volatile uint16_t pulses[SUBGHZ_MAX_PULSES];
    volatile size_t pulseCount = 0;
    volatile uint32_t lastEdgeMicros = 0;
    volatile bool capturing = false;

    void IRAM_ATTR onEdge() {
        uint32_t now = micros();
        uint32_t delta = now - lastEdgeMicros;
        lastEdgeMicros = now;
        if (!capturing) return;
        if (pulseCount < SUBGHZ_MAX_PULSES) {
            pulses[pulseCount++] = (delta > 0xFFFF) ? 0xFFFF : (uint16_t)delta;
        }
    }

    void startCapture() {
        pulseCount = 0;
        lastEdgeMicros = micros();
        capturing = true;
        radio.receiveDirect();
        pinMode(SUBGHZ_GDO0_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN), onEdge, CHANGE);
    }

    void stopCapture() {
        capturing = false;
        detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
        radio.standby();
    }

    uint16_t detectUnit(const uint16_t* p, size_t n) {
        uint16_t minD = 0xFFFF;
        size_t look = min(n, (size_t)30);
        for (size_t i = 0; i < look; i++) {
            if (p[i] > 20 && p[i] < minD) minD = p[i]; // ignore near-zero noise glitches
        }
        return (minD == 0xFFFF) ? 500 : minD; // fall back to a plausible default
    }

    uint32_t reverseBits32(uint32_t v, int bitCount) {
        uint32_t r = 0;
        for (int i = 0; i < bitCount; i++) {
            r = (r << 1) | (v & 1);
            v >>= 1;
        }
        return r;
    }

    // Same NRZ run-length reconstruction used by the POCSAG module: round
    // each pulse to the nearest whole multiple of the detected unit and
    // emit that many repeated bits.
    uint32_t buildCandidateWord(const uint16_t* p, size_t n, uint16_t unit, int wantBits) {
        uint32_t word = 0;
        int bitsEmitted = 0;
        bool level = false;
        for (size_t i = 0; i < n && bitsEmitted < wantBits; i++) {
            int nBits = (int)((p[i] + unit / 2) / unit);
            if (nBits < 1) nBits = 1;
            if (nBits > 8) nBits = 8;
            for (int b = 0; b < nBits && bitsEmitted < wantBits; b++) {
                word = (word << 1) | (level ? 1 : 0);
                bitsEmitted++;
            }
            level = !level;
        }
        return word;
    }

    struct KnownSync { uint32_t value; const char* note; };
    // Deliberately short - see header comment. Add your own findings here.
    const KnownSync kKnown[] = {
        {0xD391, "CC1101 factory-default SYNC1:SYNC0 (unconfigured/cloned boards)"},
        {0x2DD4, "IEEE 802.15.4/Zigbee-style marker (usually 2.4GHz - reference only)"},
    };

    void redraw(uint32_t word, uint32_t wordRev) {
        UIManager::clearLog();
        UIManager::printLine("Candidate (32b): 0x" + String(word, HEX));
        UIManager::printLine("Bit-reversed:    0x" + String(wordRev, HEX));

        bool matched = false;
        for (const auto& k : kKnown) {
            uint16_t hi = (word >> 16) & 0xFFFF;
            uint16_t hiRev = (wordRev >> 16) & 0xFFFF;
            if (k.value == hi || k.value == hiRev || k.value == (word & 0xFFFF) ||
                k.value == (wordRev & 0xFFFF)) {
                UIManager::printLine("Match: " + String(k.note));
                matched = true;
            }
        }
        if (!matched) {
            UIManager::printLine("No match in local table -");
            UIManager::printLine("add it if you identify it.");
        }
    }
}

bool SubGhzSyncwordAnalyzer::begin() {
    int state = radio.begin(SUBGHZ_FREQ_MHZ, 4.8f, 48.0f, 135.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    radio.setOOK(true);
    UIManager::printLine("Capturing preamble/sync...");
    UIManager::printLine("(fingerprint is timing-based");
    UIManager::printLine(" - bit order is a guess)");
    startCapture();
    return true;
}

void SubGhzSyncwordAnalyzer::loop() {
    static uint32_t lastDump = 0;
    if (millis() - lastDump < 500) return;
    lastDump = millis();

    noInterrupts();
    size_t count = pulseCount;
    interrupts();
    if (count < 16) return;

    uint16_t unit = detectUnit((const uint16_t*)pulses, count);
    uint32_t word = buildCandidateWord((const uint16_t*)pulses, count, unit, 32);
    uint32_t wordRev = reverseBits32(word, 32);
    redraw(word, wordRev);

    noInterrupts();
    pulseCount = 0;
    interrupts();
}

void SubGhzSyncwordAnalyzer::end() {
    stopCapture();
}
