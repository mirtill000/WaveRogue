#include "subghz_auditor.h"
#include "config.h"
#include "ui_manager.h"
#include <RadioLib.h>
#include <Arduino.h>

namespace {

    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, SUBGHZ_GDO2_PIN);

    // -------------------------------------------------------------------
    // Raw pulse capture state. Filled by the ISR below while the CC1101
    // is in "direct mode" (GDO0 mirrors the raw demodulated OOK/ASK
    // signal instead of the chip framing packets itself). We only record
    // *edge timing*, not the demodulated bytes, because we don't know the
    // protocol in advance - that's the whole point of Module 4.
    // -------------------------------------------------------------------
    volatile uint32_t pulseDurations[SUBGHZ_MAX_PULSES];
    volatile size_t pulseCount = 0;
    volatile uint32_t lastEdgeMicros = 0;
    volatile bool capturing = false;

    void IRAM_ATTR onEdge() {
        uint32_t now = micros();
        uint32_t delta = now - lastEdgeMicros;
        lastEdgeMicros = now;
        if (!capturing) return;
        if (pulseCount < SUBGHZ_MAX_PULSES) {
            pulseDurations[pulseCount++] = delta;
        }
    }

    // Recorded buffer for Module 5 replay (separate from the live capture
    // buffer above so a completed recording survives while we idle,
    // waiting for the operator to press Enter to transmit it).
    uint32_t recordedPulses[SUBGHZ_MAX_PULSES];
    size_t recordedCount = 0;

    void startDirectRx() {
        pulseCount = 0;
        lastEdgeMicros = micros();
        capturing = true;
        radio.receiveDirect();
        pinMode(SUBGHZ_GDO0_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN), onEdge, CHANGE);
    }

    void stopDirectRx() {
        capturing = false;
        detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
        radio.standby();
    }

    // Renders a very rough ASCII visualization of the last few pulses so
    // the operator can eyeball, e.g., "lots of similar-width pulses" (OOK
    // PWM-style, common in cheap remotes) vs. "two clearly distinct
    // widths repeating in pairs" (Manchester-coded), etc. This is a
    // teaching aid, not a real decoder - identifying the actual protocol
    // (fixed code vs rolling code, bit ordering, preamble) still requires
    // manual analysis or a reference like the RTL_433 protocol list.
    String pulsesToAscii(const volatile uint32_t* durations, size_t count, size_t maxChars) {
        String out;
        bool level = false; // we don't know absolute polarity, just alternate
        for (size_t i = 0; i < count && out.length() < maxChars; i++) {
            uint32_t d = durations[i];
            // Bucket duration into a rough "short/long" pulse classification;
            // real hardware requires per-protocol threshold tuning.
            char c = (d < 400) ? (level ? '-' : '_') : (level ? '=' : '.');
            out += c;
            level = !level;
        }
        return out;
    }

} // namespace

bool SubGhzAuditor::begin() {
    // begin(freq, bitrate, freqDev, rxBw, power, preambleLength)
    int state = radio.begin(SUBGHZ_FREQ_MHZ, 4.8f, 48.0f, 135.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    radio.setOOK(true);
    return true;
}

// --------------------------- Module 4: Sniffer ---------------------------

void SubGhzAuditor::sniffBegin() {
    UIManager::printLine("Capturing raw OOK/ASK...");
    UIManager::printLine("(edge timing, no protocol");
    UIManager::printLine(" assumed - eyeball the");
    UIManager::printLine(" pattern below)");
    startDirectRx();
}

void SubGhzAuditor::sniffLoop() {
    // Every so often, dump whatever has been captured so far and reset,
    // so long transmissions don't just fill the buffer once and go quiet.
    static uint32_t lastDump = 0;
    if (millis() - lastDump < 400) return;
    lastDump = millis();

    noInterrupts();
    size_t count = pulseCount;
    interrupts();

    if (count < 4) return; // nothing meaningful captured yet

    UIManager::clearLog();
    UIManager::printLine("Pulses: " + String(count));
    UIManager::printLine(pulsesToAscii(pulseDurations, count, 100));
    UIManager::printLine("First widths (us):");
    String widths;
    for (size_t i = 0; i < count && i < 8; i++) {
        widths += String(pulseDurations[i]) + " ";
    }
    UIManager::printLine(widths);

    noInterrupts();
    pulseCount = 0;
    interrupts();
}

void SubGhzAuditor::sniffEnd() {
    stopDirectRx();
}

// --------------------------- Module 5: Replay ---------------------------

void SubGhzAuditor::replayBegin() {
    recordedCount = 0;
    UIManager::printLine("Recording... press the");
    UIManager::printLine("remote button now.");
    UIManager::printLine("Then press ENTER here to");
    UIManager::printLine("replay the captured code.");
    UIManager::printLine("(Test your OWN receiver");
    UIManager::printLine(" only - replay attacks on");
    UIManager::printLine(" third-party gear are illegal.)");
    startDirectRx();
}

void SubGhzAuditor::replayLoop() {
    // Keep pulling the freshest capture into `recordedPulses` until the
    // operator is happy and hits Enter - this way the buffer always holds
    // the most recent complete button-press.
    noInterrupts();
    size_t count = pulseCount;
    interrupts();

    if (count >= 8) {
        noInterrupts();
        recordedCount = min(count, (size_t)SUBGHZ_MAX_PULSES);
        for (size_t i = 0; i < recordedCount; i++) recordedPulses[i] = pulseDurations[i];
        pulseCount = 0;
        interrupts();
        UIManager::printLine("Captured " + String(recordedCount) + " pulses, ready.");
    }

    if (UIManager::isEnter() && recordedCount > 0) {
        UIManager::printLine("Replaying " + String(recordedCount) + " pulses...");

        // Switch out of RX direct mode into TX direct mode, then bit-bang
        // GDO0 as a plain digital output following the recorded timing.
        // This faithfully reproduces whatever raw waveform we captured,
        // regardless of its (unknown) modulation/encoding - which is
        // exactly what a real replay attack does, and exactly what you
        // need to test whether a receiver requires a rolling code.
        detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
        radio.transmitDirect();
        pinMode(SUBGHZ_GDO0_PIN, OUTPUT);

        bool level = HIGH;
        for (size_t i = 0; i < recordedCount; i++) {
            digitalWrite(SUBGHZ_GDO0_PIN, level);
            delayMicroseconds(recordedPulses[i]);
            level = !level;
        }
        digitalWrite(SUBGHZ_GDO0_PIN, LOW);
        radio.standby();

        UIManager::printLine("Replay done.");

        // Resume listening in case the operator wants to re-record.
        startDirectRx();
    }
}

void SubGhzAuditor::replayEnd() {
    stopDirectRx();
    recordedCount = 0;
}
