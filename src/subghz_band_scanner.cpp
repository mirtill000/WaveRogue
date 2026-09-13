#include "subghz_band_scanner.h"
#include "config.h"
#include "ui_manager.h"
#include "subghz_rf_switch.h"
#include <RadioLib.h>
#include <Arduino.h>
#include <SPI.h>

namespace {
    SPIClass bandSPI(HSPI);
    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, RADIOLIB_NC, bandSPI);

    // Channel list is computed from the start/end/step range in config.h
    // rather than stored as an array - freqForChannel() derives it.
    const int kChannelCount =
        (int)((SUBGHZ_BANDSCAN_END_MHZ - SUBGHZ_BANDSCAN_START_MHZ) / SUBGHZ_BANDSCAN_STEP_MHZ + 0.5f) + 1;

    float freqForChannel(int ch) {
        return SUBGHZ_BANDSCAN_START_MHZ + ch * SUBGHZ_BANDSCAN_STEP_MHZ;
    }

    // -------------------------------------------------------------------
    // Same raw edge-timing capture as the Raw Sniffer (subghz_auditor.cpp)
    // - the interrupt is attached once in begin() and stays attached for
    // the module's whole lifetime; hopping only changes the CC1101's
    // tuned frequency, not the GPIO/ISR wiring.
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

    String pulsesToAscii(const volatile uint32_t* durations, size_t count, size_t maxChars) {
        String out;
        bool level = false;
        for (size_t i = 0; i < count && out.length() < maxChars; i++) {
            uint32_t d = durations[i];
            char c = (d < 400) ? (level ? '-' : '_') : (level ? '=' : '.');
            out += c;
            level = !level;
        }
        return out;
    }

    enum class ScanState { SWEEPING, LOCKED };
    ScanState state = ScanState::SWEEPING;
    int currentChannel = 0;
    int lockedChannel = 0;
    uint32_t channelEnteredAt = 0;
    uint32_t lastDumpMillis = 0;

    void tuneToChannel(int ch) {
        float freq = freqForChannel(ch);
        SubGhzRfSwitch::selectForFrequency(freq);
        radio.standby();
        radio.setFrequency(freq);
        noInterrupts();
        pulseCount = 0;
        interrupts();
        lastEdgeMicros = micros();
        radio.receiveDirect();
        channelEnteredAt = millis();
    }
}

bool SubGhzBandScanner::begin() {
    bandSPI.begin(SUBGHZ_SPI_SCK_PIN, SUBGHZ_SPI_MISO_PIN, SUBGHZ_SPI_MOSI_PIN, SUBGHZ_CS_PIN);

    int rState = radio.begin(SUBGHZ_BANDSCAN_START_MHZ, 4.8f, 48.0f, 135.0f, 10, 16);
    if (rState != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(rState));
        return false;
    }
    radio.setOOK(true);

    capturing = true;
    pinMode(SUBGHZ_GDO0_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN), onEdge, CHANGE);

    UIManager::printLine("Sweeping " + String(SUBGHZ_BANDSCAN_START_MHZ, 2) + "-" +
                          String(SUBGHZ_BANDSCAN_END_MHZ, 2) + "MHz");
    UIManager::printLine(String(kChannelCount) + " channels, " + String(SUBGHZ_BANDSCAN_DWELL_MS) + "ms dwell");
    UIManager::printLine("Locks on any real signal...");

    state = ScanState::SWEEPING;
    currentChannel = 0;
    tuneToChannel(0);
    return true;
}

void SubGhzBandScanner::loop() {
    uint32_t now = millis();

    if (state == ScanState::SWEEPING) {
        UIManager::setStatus("Sweeping ch " + String(currentChannel + 1) + "/" + String(kChannelCount) + " (" +
                              String(freqForChannel(currentChannel), 3) + "MHz)...");

        noInterrupts();
        size_t count = pulseCount;
        interrupts();

        if (count >= (size_t)SUBGHZ_BANDSCAN_MIN_PULSES) {
            state = ScanState::LOCKED;
            lockedChannel = currentChannel;
            UIManager::clearLog();
            UIManager::printLine("Signal on " + String(freqForChannel(lockedChannel), 3) + "MHz!");
            lastDumpMillis = 0; // force an immediate dump below
            return;
        }

        if (now - channelEnteredAt >= (uint32_t)SUBGHZ_BANDSCAN_DWELL_MS) {
            currentChannel = (currentChannel + 1) % kChannelCount;
            tuneToChannel(currentChannel);
        }
        return;
    }

    // LOCKED: behave like the Raw Sniffer at this one frequency, but
    // watch for a quiet period to know the transmission has ended.
    UIManager::setStatus("Locked " + String(freqForChannel(lockedChannel), 3) + "MHz - capturing...");

    noInterrupts();
    size_t count = pulseCount;
    uint32_t idleUs = micros() - lastEdgeMicros;
    interrupts();

    if (now - lastDumpMillis >= 400 && count >= 4) {
        lastDumpMillis = now;
        UIManager::clearLog();
        UIManager::printLine(String(freqForChannel(lockedChannel), 3) + "MHz - pulses: " + String(count));
        UIManager::printLine(pulsesToAscii(pulseDurations, count, 100));
        String widths;
        for (size_t i = 0; i < count && i < 8; i++) widths += String(pulseDurations[i]) + " ";
        UIManager::printLine(widths);

        noInterrupts();
        pulseCount = 0;
        interrupts();
    }

    if (idleUs > (uint32_t)SUBGHZ_BANDSCAN_LOCK_QUIET_MS * 1000UL) {
        UIManager::printLine("(quiet - resuming sweep)");
        state = ScanState::SWEEPING;
        // Resume from the NEXT channel, not the same one, so lingering
        // interference on one frequency can't stall the whole sweep.
        currentChannel = (lockedChannel + 1) % kChannelCount;
        tuneToChannel(currentChannel);
    }
}

void SubGhzBandScanner::end() {
    capturing = false;
    detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
    radio.standby();
}
