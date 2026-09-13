#include "subghz_audit.h"
#include "config.h"
#include "ui_manager.h"
#include "subghz_rf_switch.h"
#include <RadioLib.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace {
    SPIClass subghzSPI(HSPI);
    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, RADIOLIB_NC, subghzSPI);

    // Band edges match the generic ISM presets used by other Cardputer-class
    // Sub-GHz tools (e.g. Evil-M5Project's spectrum-analyzer band list) -
    // these are standard ISM allocation boundaries, not anyone's expression.
    struct BandDef {
        const char* label;
        float startMhz;
        float endMhz;
    };
    constexpr BandDef kBands[] = {
        {"315 MHz", 310.0f, 320.0f},
        {"433 MHz", 425.0f, 445.0f},
        {"868 MHz", 860.0f, 875.0f},
        {"915 MHz", 905.0f, 925.0f},
    };
    constexpr int kBandCount = sizeof(kBands) / sizeof(kBands[0]);
    static_assert(kBandCount == (int)SubGhzAudit::Band::COUNT, "kBands must match SubGhzAudit::Band");

    SubGhzAudit::Band currentBand = SubGhzAudit::Band::BAND_433;

    int channelCountFor(SubGhzAudit::Band band) {
        const BandDef& b = kBands[(int)band];
        return (int)((b.endMhz - b.startMhz) / SUBGHZ_AUDIT_STEP_MHZ + 0.5f) + 1;
    }

    float freqForChannel(SubGhzAudit::Band band, int ch) {
        return kBands[(int)band].startMhz + ch * SUBGHZ_AUDIT_STEP_MHZ;
    }

    // -------------------------------------------------------------------
    // Raw edge-timing capture: an ISR records the microsecond delta since
    // the previous GPIO transition into a ring-style buffer while the
    // CC1101 is in direct mode (radio.receiveDirect()). Same pattern used
    // by every earlier Sub-GHz module in this codebase.
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

    String pulsesToAscii(const uint16_t* durations, size_t count, size_t maxChars) {
        String out;
        bool level = false;
        for (size_t i = 0; i < count && out.length() < maxChars; i++) {
            char c = (durations[i] < 400) ? (level ? '-' : '_') : (level ? '=' : '.');
            out += c;
            level = !level;
        }
        return out;
    }

    // -------------------------------------------------------------------
    // Generic "PWM fixed-code" decoder.
    //
    // Cheap OOK remotes built around PT2262/EV1527-family encoders (and
    // the countless compatible clones used in garage remotes, doorbells,
    // driveway alarms, etc.) all share the same publicly-documented shape:
    // a long "sync" gap marks the start of a frame, followed by a fixed
    // number of bit-pairs where a short pulse next to a long pulse (or
    // vice-versa) encodes one bit. This is independently implemented from
    // that public timing knowledge - it doesn't try to identify a named
    // protocol or match a per-protocol table, just this one common shape.
    // -------------------------------------------------------------------
    struct DecodedCode {
        bool ok = false;
        uint32_t value = 0;
        uint8_t bitCount = 0;
    };

    DecodedCode tryDecodePwmFixedCode(const uint16_t* pulses, size_t count) {
        DecodedCode result;
        if (count < 16) return result; // too short to be a meaningful frame

        // The sync gap is, by design, much longer than any data pulse -
        // take the single longest pulse in the capture as its location.
        size_t syncIdx = 0;
        uint16_t syncWidth = 0;
        for (size_t i = 0; i < count; i++) {
            if (pulses[i] > syncWidth) {
                syncWidth = pulses[i];
                syncIdx = i;
            }
        }
        if (syncIdx + 17 > count) return result; // no room for a frame after it

        const uint16_t* frame = pulses + syncIdx + 1;
        size_t frameLen = count - syncIdx - 1;

        // Reference "short" pulse width: median of the frame's pulses that
        // are clearly shorter than the sync gap (a real bit-pair pulse is
        // at most ~3x the short pulse, so a third of the sync gap is a
        // generous cutoff that still excludes stray long pulses).
        static uint16_t samples[SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE];
        size_t sampleCount = 0;
        for (size_t i = 0; i < frameLen && sampleCount < SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE; i++) {
            if (frame[i] < syncWidth / 3) samples[sampleCount++] = frame[i];
        }
        if (sampleCount < 8) return result;

        for (size_t i = 1; i < sampleCount; i++) {
            uint16_t key = samples[i];
            size_t j = i;
            while (j > 0 && samples[j - 1] > key) {
                samples[j] = samples[j - 1];
                j--;
            }
            samples[j] = key;
        }
        uint16_t teShort = samples[sampleCount / 2];
        if (teShort < 50) return result; // implausibly short for a real pulse

        int32_t tol = teShort / 2 + SUBGHZ_AUDIT_MATCH_TOLERANCE_US / 10;
        auto classify = [&](uint16_t w) -> int {
            if (abs((int32_t)w - (int32_t)teShort) <= tol) return 0;             // "short"
            if (abs((int32_t)w - (int32_t)teShort * 3) <= tol * 2) return 1;      // "long" (~3x short)
            return -1; // doesn't fit this encoding - not our decoder's job
        };

        uint32_t value = 0;
        uint8_t bits = 0;
        for (size_t i = 0; i + 1 < frameLen && bits < 32; i += 2) {
            int a = classify(frame[i]);
            int b = classify(frame[i + 1]);
            if (a < 0 || b < 0 || a == b) break; // frame ended or doesn't fit - stop cleanly
            value = (value << 1) | (uint32_t)(a == 0 ? 0 : 1); // short,long=0 ; long,short=1
            bits++;
        }

        if (bits < 8) return result; // too few clean bits to call this "decoded"
        result.ok = true;
        result.value = value;
        result.bitCount = bits;
        return result;
    }

    // -------------------------------------------------------------------
    // Per-channel capture history, for static-vs-rolling-code detection:
    // an exact repeat of the same payload on the same channel means a
    // fixed code (100% replay-vulnerable); a different payload each time
    // suggests a rolling code instead.
    // -------------------------------------------------------------------
    struct CaptureRecord {
        int channel = -1;
        uint16_t pulses[SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE];
        size_t count = 0;
        DecodedCode decoded;
        int repeatCount = 1;
    };
    CaptureRecord history[SUBGHZ_AUDIT_HISTORY_SIZE];
    int historyCount = 0;

    bool rawPulsesMatch(const uint16_t* a, size_t countA, const uint16_t* b, size_t countB) {
        size_t n = min(countA, countB);
        if (n < 8) return false;
        size_t lenDiff = (countA > countB) ? (countA - countB) : (countB - countA);
        if (lenDiff > n / 8 + 4) return false;

        size_t mismatches = 0;
        for (size_t i = 0; i < n; i++) {
            int32_t diff = (int32_t)a[i] - (int32_t)b[i];
            if (diff < 0) diff = -diff;
            if (diff > SUBGHZ_AUDIT_MATCH_TOLERANCE_US) mismatches++;
        }
        return mismatches <= n / 10; // allow up to 10% noisy pulses
    }

    bool recordsMatch(const CaptureRecord& existing, const uint16_t* pulses, size_t count,
                       const DecodedCode& decoded) {
        // Prefer comparing decoded values when both captures decoded
        // cleanly - immune to jitter that a raw comparison would still
        // tolerate, but only meaningful if the shape actually decoded.
        if (existing.decoded.ok && decoded.ok) {
            return existing.decoded.value == decoded.value && existing.decoded.bitCount == decoded.bitCount;
        }
        return rawPulsesMatch(existing.pulses, existing.count, pulses, count);
    }

    // -------------------------------------------------------------------
    // SD logging
    // -------------------------------------------------------------------
    File logFile;

    void openLog() {
        if (!SD.begin(SD_CS_PIN)) {
            UIManager::printLine("[!] SD card init failed");
            return;
        }
        bool isNew = !SD.exists(SUBGHZ_AUDIT_LOG_PATH);
        logFile = SD.open(SUBGHZ_AUDIT_LOG_PATH, FILE_APPEND);
        if (logFile && isNew) {
            logFile.println("utc_millis,band,freq_mhz,event,detail");
        }
    }

    void logEvent(float freqMhz, const String& event, const String& detail) {
        if (!logFile) return;
        logFile.println(String(millis()) + "," + kBands[(int)currentBand].label + "," + String(freqMhz, 3) + "," +
                         event + "," + detail);
        logFile.flush();
    }

    // -------------------------------------------------------------------
    // Sweep / lock / carrier-detect state machine.
    //
    // Merges what used to be two separate tools: hopping across the band
    // looking for OOK/ASK edge bursts (lock on and capture in full), while
    // also watching RSSI continuously for a carrier held far longer than
    // any data burst would be (flagged as a possible active bug/Tx).
    // -------------------------------------------------------------------
    enum class ScanState { SWEEPING, LOCKED };
    ScanState state = ScanState::SWEEPING;
    int currentChannel = 0;
    int lockedChannel = 0;
    uint32_t channelEnteredAt = 0;
    uint32_t lastDumpMillis = 0;
    uint32_t aboveThresholdSince = 0; // 0 = not currently above threshold
    bool carrierFlaggedThisChannel = false;

    void tuneToChannel(int ch) {
        float freq = freqForChannel(currentBand, ch);
        SubGhzRfSwitch::selectForFrequency(freq);
        radio.standby();
        radio.setFrequency(freq);
        noInterrupts();
        pulseCount = 0;
        interrupts();
        lastEdgeMicros = micros();
        radio.receiveDirect();
        channelEnteredAt = millis();
        aboveThresholdSince = 0;
        carrierFlaggedThisChannel = false;
    }

    void redrawHistorySummary() {
        UIManager::clearLog();
        UIManager::printLine(String(kBands[(int)currentBand].label) + " audit - " + String(historyCount) +
                              " capture(s)");
        int staticCount = 0;
        for (int i = 0; i < historyCount; i++) {
            if (history[i].repeatCount >= 2) staticCount++;
        }
        if (staticCount > 0) {
            UIManager::printLine("[!] " + String(staticCount) + " STATIC code(s)!");
            UIManager::printLine("    100% replay-vulnerable.");
        }
        int shown = 0;
        for (int i = historyCount - 1; i >= 0 && shown < 4; i--, shown++) {
            char buf[56];
            const CaptureRecord& r = history[i];
            if (r.decoded.ok) {
                snprintf(buf, sizeof(buf), "%.2fMHz: 0x%lX (%ub) x%d%s", freqForChannel(currentBand, r.channel),
                         (unsigned long)r.decoded.value, r.decoded.bitCount, r.repeatCount,
                         r.repeatCount >= 2 ? " STATIC" : "");
            } else {
                snprintf(buf, sizeof(buf), "%.2fMHz: raw %up x%d%s", freqForChannel(currentBand, r.channel),
                         (unsigned)r.count, r.repeatCount, r.repeatCount >= 2 ? " STATIC" : "");
            }
            UIManager::printLine(String(buf));
        }
    }

    // Finalizes whatever is in the live buffer as one completed capture on
    // the given channel: decodes it, compares it against history on that
    // same channel, and logs the outcome.
    void finalizeCapture(int channel) {
        noInterrupts();
        size_t count = pulseCount;
        static uint16_t snapshot[SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE];
        size_t copyCount = min(count, (size_t)SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE);
        for (size_t i = 0; i < copyCount; i++) {
            uint32_t d = pulseDurations[i];
            snapshot[i] = (d > 0xFFFF) ? 0xFFFF : (uint16_t)d;
        }
        pulseCount = 0;
        interrupts();

        if (copyCount < (size_t)SUBGHZ_AUDIT_MIN_PULSES) return;

        DecodedCode decoded = tryDecodePwmFixedCode(snapshot, copyCount);
        float freq = freqForChannel(currentBand, channel);

        for (int i = 0; i < historyCount; i++) {
            if (history[i].channel != channel) continue;
            if (recordsMatch(history[i], snapshot, copyCount, decoded)) {
                history[i].repeatCount++;
                String detail = decoded.ok ? ("0x" + String(decoded.value, HEX) + " repeat")
                                            : (String(copyCount) + "p repeat");
                logEvent(freq, "REPEAT", detail);
                redrawHistorySummary();
                return;
            }
        }

        int idx;
        if (historyCount < SUBGHZ_AUDIT_HISTORY_SIZE) {
            idx = historyCount++;
        } else {
            for (int i = 0; i < SUBGHZ_AUDIT_HISTORY_SIZE - 1; i++) history[i] = history[i + 1];
            idx = SUBGHZ_AUDIT_HISTORY_SIZE - 1;
        }
        history[idx].channel = channel;
        history[idx].count = copyCount;
        history[idx].decoded = decoded;
        history[idx].repeatCount = 1;
        for (size_t i = 0; i < copyCount; i++) history[idx].pulses[i] = snapshot[i];

        String detail = decoded.ok ? ("0x" + String(decoded.value, HEX) + " (" + String(decoded.bitCount) + "b)")
                                    : (String(copyCount) + " pulses, unrecognized shape");
        logEvent(freq, "CAPTURE", detail);
        redrawHistorySummary();
    }
}

const char* SubGhzAudit::bandLabel(Band band) {
    return kBands[(int)band].label;
}

bool SubGhzAudit::begin(Band band) {
    currentBand = band;
    subghzSPI.begin(SUBGHZ_SPI_SCK_PIN, SUBGHZ_SPI_MISO_PIN, SUBGHZ_SPI_MOSI_PIN, SUBGHZ_CS_PIN);

    int rState = radio.begin(kBands[(int)band].startMhz, 4.8f, 48.0f, 135.0f, 10, 16);
    if (rState != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(rState));
        return false;
    }
    radio.setOOK(true);

    openLog();

    historyCount = 0;
    capturing = true;
    pinMode(SUBGHZ_GDO0_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN), onEdge, CHANGE);

    UIManager::printLine(String("Auditing ") + kBands[(int)band].label);
    UIManager::printLine(String(kBands[(int)band].startMhz, 1) + "-" + String(kBands[(int)band].endMhz, 1) + "MHz, " +
                          String(channelCountFor(band)) + " channels");
    UIManager::printLine("Locks on bursts; also");
    UIManager::printLine("watches for a carrier...");

    state = ScanState::SWEEPING;
    currentChannel = 0;
    tuneToChannel(0);
    return true;
}

void SubGhzAudit::loop() {
    uint32_t now = millis();
    int channelCount = channelCountFor(currentBand);

    // Carrier watch runs continuously regardless of sweep/lock state - a
    // continuous transmitter looks nothing like a data burst (RSSI just
    // stays pinned above threshold far longer than any packet takes).
    float rssi = radio.getRSSI();
    bool above = rssi > SUBGHZ_AUDIT_RSSI_THRESHOLD_DBM;
    if (above) {
        if (aboveThresholdSince == 0) aboveThresholdSince = now;
        uint32_t dur = now - aboveThresholdSince;
        if (dur >= (uint32_t)SUBGHZ_AUDIT_CARRIER_MIN_DURATION_MS && !carrierFlaggedThisChannel) {
            carrierFlaggedThisChannel = true;
            int ch = (state == ScanState::LOCKED) ? lockedChannel : currentChannel;
            float freq = freqForChannel(currentBand, ch);
            UIManager::clearLog();
            UIManager::printLine("[!] CONTINUOUS CARRIER!");
            UIManager::printLine(String(freq, 3) + "MHz for " + String(dur / 1000.0f, 1) + "s");
            UIManager::printLine("RSSI: " + String(rssi, 0) + "dBm - possible");
            UIManager::printLine("active bug/transmitter.");
            logEvent(freq, "CARRIER", String(dur) + "ms @ " + String(rssi, 0) + "dBm");
        }
    } else {
        aboveThresholdSince = 0;
    }

    if (state == ScanState::SWEEPING) {
        UIManager::setStatus("Sweeping ch " + String(currentChannel + 1) + "/" + String(channelCount) + " (" +
                              String(freqForChannel(currentBand, currentChannel), 2) + "MHz)...");

        noInterrupts();
        size_t count = pulseCount;
        interrupts();

        if (count >= (size_t)SUBGHZ_AUDIT_MIN_PULSES) {
            state = ScanState::LOCKED;
            lockedChannel = currentChannel;
            UIManager::clearLog();
            UIManager::printLine("Burst on " + String(freqForChannel(currentBand, lockedChannel), 3) + "MHz!");
            lastDumpMillis = 0; // force an immediate dump below
            return;
        }

        if (now - channelEnteredAt >= (uint32_t)SUBGHZ_AUDIT_DWELL_MS) {
            currentChannel = (currentChannel + 1) % channelCount;
            tuneToChannel(currentChannel);
        }
        return;
    }

    // LOCKED: keep capturing on this one channel, watch for a quiet period
    // that means the transmission ended.
    UIManager::setStatus("Locked " + String(freqForChannel(currentBand, lockedChannel), 3) + "MHz - capturing...");

    noInterrupts();
    size_t count = pulseCount;
    uint32_t idleUs = micros() - lastEdgeMicros;
    interrupts();

    if (now - lastDumpMillis >= 400 && count >= 4) {
        lastDumpMillis = now;
        UIManager::clearLog();
        UIManager::printLine(String(freqForChannel(currentBand, lockedChannel), 3) + "MHz - pulses: " + String(count));
        static uint16_t peek[64];
        size_t peekCount = min(count, (size_t)64);
        for (size_t i = 0; i < peekCount; i++) {
            uint32_t d = pulseDurations[i];
            peek[i] = (d > 0xFFFF) ? 0xFFFF : (uint16_t)d;
        }
        UIManager::printLine(pulsesToAscii(peek, peekCount, 100));
        // Only peeking at the buffer for the display above - NOT resetting
        // pulseCount here, unlike the old Band Scanner did. Resetting mid-
        // capture would fragment one continuous burst into several partial
        // ones; the buffer is only cleared at finalizeCapture()/tuneToChannel().
    }

    if (idleUs > (uint32_t)SUBGHZ_AUDIT_LOCK_QUIET_MS * 1000UL) {
        finalizeCapture(lockedChannel);
        state = ScanState::SWEEPING;
        // Resume from the NEXT channel, not the same one, so lingering
        // interference on one frequency can't stall the whole sweep.
        currentChannel = (lockedChannel + 1) % channelCount;
        tuneToChannel(currentChannel);
    }
}

void SubGhzAudit::end() {
    capturing = false;
    detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
    radio.standby();
    if (logFile) logFile.close();
}
