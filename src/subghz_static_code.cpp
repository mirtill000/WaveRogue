#include "subghz_static_code.h"
#include "config.h"
#include "ui_manager.h"
#include <RadioLib.h>
#include <Arduino.h>

namespace {
    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, SUBGHZ_GDO2_PIN);

    // ---- Live edge capture (filled by ISR) ----
    volatile uint16_t livePulses[STATICCODE_MAX_PULSES_PER_SESSION];
    volatile size_t liveCount = 0;
    volatile uint32_t lastEdgeMicros = 0;
    volatile bool capturing = false;

    void IRAM_ATTR onEdge() {
        uint32_t now = micros();
        uint32_t delta = now - lastEdgeMicros;
        lastEdgeMicros = now;
        if (!capturing) return;
        if (liveCount < STATICCODE_MAX_PULSES_PER_SESSION) {
            livePulses[liveCount++] = (delta > 0xFFFF) ? 0xFFFF : (uint16_t)delta;
        }
    }

    // ---- Session history ----
    struct Session {
        uint16_t pulses[STATICCODE_MAX_PULSES_PER_SESSION];
        size_t count = 0;
        int repeatCount = 1;
    };
    Session history[STATICCODE_HISTORY_SIZE];
    int historyCount = 0;

    bool sessionsMatch(const uint16_t* a, size_t countA, const uint16_t* b, size_t countB) {
        // Allow a little length slop (jitter at capture start/end can drop
        // or add an edge or two) before comparing pulse-by-pulse.
        size_t n = min(countA, countB);
        if (n < 8) return false; // too short to mean anything
        size_t lenDiff = (countA > countB) ? (countA - countB) : (countB - countA);
        if (lenDiff > n / 8 + 4) return false;

        size_t mismatches = 0;
        for (size_t i = 0; i < n; i++) {
            int32_t diff = (int32_t)a[i] - (int32_t)b[i];
            if (diff < 0) diff = -diff;
            if (diff > STATICCODE_MATCH_TOLERANCE_US) mismatches++;
        }
        return mismatches <= n / 10; // allow up to 10% noisy pulses
    }

    void startCapture() {
        liveCount = 0;
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

    void redrawHistory() {
        UIManager::clearLog();
        UIManager::printLine("Sessions captured: " + String(historyCount));
        int staticCount = 0;
        for (int i = 0; i < historyCount; i++) {
            if (history[i].repeatCount >= 2) staticCount++;
        }
        if (staticCount > 0) {
            UIManager::printLine("[!] " + String(staticCount) + " STATIC code(s)!");
            UIManager::printLine("    100% replay-vulnerable.");
        }
        int shown = 0;
        for (int i = historyCount - 1; i >= 0 && shown < 5; i--, shown++) {
            char buf[40];
            snprintf(buf, sizeof(buf), "#%d: %u pulses x%d%s", i, (unsigned)history[i].count,
                     history[i].repeatCount, history[i].repeatCount >= 2 ? " STATIC" : "");
            UIManager::printLine(String(buf));
        }
    }

    // Finalizes whatever is in the live buffer as one completed session:
    // compares it against history and either bumps a repeat counter or
    // stores it as a new distinct code.
    void commitSession() {
        noInterrupts();
        size_t count = liveCount;
        static uint16_t snapshot[STATICCODE_MAX_PULSES_PER_SESSION];
        for (size_t i = 0; i < count; i++) snapshot[i] = livePulses[i];
        liveCount = 0;
        interrupts();

        if (count < 8) return; // noise/nothing meaningful

        for (int i = 0; i < historyCount; i++) {
            if (sessionsMatch(snapshot, count, history[i].pulses, history[i].count)) {
                history[i].repeatCount++;
                redrawHistory();
                return;
            }
        }

        int idx;
        if (historyCount < STATICCODE_HISTORY_SIZE) {
            idx = historyCount++;
        } else {
            // History full: drop the oldest slot to make room.
            for (int i = 0; i < STATICCODE_HISTORY_SIZE - 1; i++) history[i] = history[i + 1];
            idx = STATICCODE_HISTORY_SIZE - 1;
        }
        history[idx].count = count;
        history[idx].repeatCount = 1;
        for (size_t i = 0; i < count; i++) history[idx].pulses[i] = snapshot[i];
        redrawHistory();
    }
}

bool SubGhzStaticCode::begin() {
    int state = radio.begin(SUBGHZ_FREQ_MHZ, 4.8f, 48.0f, 135.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    radio.setOOK(true);

    historyCount = 0;
    UIManager::printLine("Press the remote's button");
    UIManager::printLine("several times, with gaps");
    UIManager::printLine("between presses. A repeat");
    UIManager::printLine("of the exact same code =");
    UIManager::printLine("fixed code (replay-able).");
    startCapture();
    return true;
}

void SubGhzStaticCode::loop() {
    noInterrupts();
    size_t count = liveCount;
    uint32_t idleFor = micros() - lastEdgeMicros;
    interrupts();

    // A long enough silence after having captured something means the
    // button was released - finalize this session now.
    if (count > 0 && idleFor > (uint32_t)STATICCODE_SESSION_GAP_MS * 1000UL) {
        commitSession();
    }
}

void SubGhzStaticCode::end() {
    stopCapture();
}
