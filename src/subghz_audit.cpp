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
    //
    // A real remote sends the same frame several times back-to-back per
    // button press (each repeat separated by another sync gap), so one
    // capture usually holds multiple copies of the same code. Rather than
    // decoding only the first frame found, every sync-delimited frame in
    // the capture is decoded independently and cross-checked against its
    // neighbor: two consecutive frames agreeing is a much stronger signal
    // than a single decode, since it rules out a one-off timing glitch
    // being misread as a bit. `repeatConfirmed` reports which happened.
    // -------------------------------------------------------------------
    struct DecodedCode {
        bool ok = false;
        bool repeatConfirmed = false;
        uint32_t value = 0;
        uint8_t bitCount = 0;
        const char* family = "generic PWM";
    };

    constexpr size_t kMaxSyncCandidates = 17;

    // PWM fixed-code remotes don't all use the same short:long pulse
    // ratio: Princeton/PT2262/EV1527-style encoders sit around 1:3, while
    // Holtek HT12A/HT12E and the similarly-shaped 12-bit encoders used by
    // many CAME/Nice-style gate/garage remotes sit closer to 1:2. Both
    // are well-documented, generic PWM facts (not any one vendor's
    // expression), so each frame is tried against both ratios and
    // whichever cleanly decodes the most bits wins.
    constexpr int kCandidateRatios[] = {3, 2};
    constexpr size_t kCandidateRatioCount = sizeof(kCandidateRatios) / sizeof(kCandidateRatios[0]);

    // Classifies a decode against the shape families that ratio/bit-count
    // combination is most consistent with. This is a coarse heuristic
    // over two generic, publicly-documented PWM shapes - not a maintained
    // per-vendor protocol fingerprint database - so anything that doesn't
    // land in one of these ranges is reported as an unnamed instance of
    // the same generic short/long-pulse shape rather than guessed at.
    const char* classifyFamily(uint8_t bitCount, int ratio) {
        if (ratio == 3 && bitCount >= 23 && bitCount <= 25) return "PT2262/EV1527-family";
        if (ratio == 2 && bitCount >= 11 && bitCount <= 13) return "Holtek/CAME-style 12b family";
        if (ratio == 3 && bitCount >= 32) return "long PT2262-style (32+b)";
        return "generic PWM";
    }

    // Decodes one sync-delimited frame in isolation: estimates that
    // frame's own short-pulse width (a capture spanning several repeats
    // of possibly different transmissions shouldn't assume one timing for
    // all of them), then, for each candidate short:long ratio, reads off
    // bit-pairs until the shape breaks down or the frame ends - keeping
    // whichever ratio decoded the most bits cleanly.
    bool decodeOneFrame(const uint16_t* frame, size_t frameLen, uint32_t& outValue, uint8_t& outBits, int& outRatio) {
        if (frameLen < 16) return false;

        uint16_t frameMax = 0;
        for (size_t i = 0; i < frameLen; i++) frameMax = max(frameMax, frame[i]);

        // Cutoff wide enough to keep genuine short pulses for either
        // candidate ratio: at 1:3 the short pulse is ~33% of the frame's
        // longest pulse, at 1:2 it's ~50% - both fall under this 2/3 mark.
        static uint16_t samples[SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE];
        size_t sampleCount = 0;
        for (size_t i = 0; i < frameLen && sampleCount < SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE; i++) {
            if (frame[i] < (uint16_t)(frameMax * 2 / 3)) samples[sampleCount++] = frame[i];
        }
        if (sampleCount < 8) return false;

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
        if (teShort < 50) return false; // implausibly short for a real pulse

        bool haveBest = false;
        uint32_t bestValue = 0;
        uint8_t bestBits = 0;
        int bestRatio = 0;

        // A tolerance of ~1/3 of the short pulse keeps the "short" and
        // "long" acceptance bands from overlapping even at the tightest
        // candidate ratio (1:2) - e.g. short=[0.67,1.33]xteShort and
        // long=[1.67,2.33]xteShort leave a clear gap between them. A
        // wider tolerance (like the previous fixed ~1/2) would blur that
        // gap shut for the 1:2 case.
        int32_t tol = teShort / 3 + SUBGHZ_AUDIT_MATCH_TOLERANCE_US / 10;

        for (size_t r = 0; r < kCandidateRatioCount; r++) {
            int ratioNum = kCandidateRatios[r];
            auto classify = [&](uint16_t w) -> int {
                if (abs((int32_t)w - (int32_t)teShort) <= tol) return 0; // "short"
                if (abs((int32_t)w - (int32_t)teShort * ratioNum) <= tol) return 1; // "long"
                return -1; // doesn't fit this ratio - not this candidate's job
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

            if (bits > bestBits) {
                haveBest = true;
                bestValue = value;
                bestBits = bits;
                bestRatio = ratioNum;
            }
        }

        if (!haveBest || bestBits < 8) return false; // too few clean bits to call this "decoded"
        outValue = bestValue;
        outBits = bestBits;
        outRatio = bestRatio;
        return true;
    }

    DecodedCode tryDecodePwmFixedCode(const uint16_t* pulses, size_t count) {
        DecodedCode result;
        if (count < 16) return result; // too short to be a meaningful frame

        // A rough "typical pulse" estimate from the whole capture, just to
        // set a threshold for what counts as a sync gap - a sync is, by
        // design, several times longer than any data pulse.
        static uint16_t rough[SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE];
        size_t roughCount = min(count, (size_t)SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE);
        for (size_t i = 0; i < roughCount; i++) rough[i] = pulses[i];
        for (size_t i = 1; i < roughCount; i++) {
            uint16_t key = rough[i];
            size_t j = i;
            while (j > 0 && rough[j - 1] > key) {
                rough[j] = rough[j - 1];
                j--;
            }
            rough[j] = key;
        }
        uint16_t roughMedian = rough[roughCount / 2];
        if (roughMedian < 20) return result;

        // Every pulse at least 5x that typical width is a candidate sync
        // gap - a real transmission's repeats give us several of these,
        // one per frame boundary.
        size_t syncIdx[kMaxSyncCandidates];
        size_t syncCount = 0;
        for (size_t i = 0; i < count && syncCount < kMaxSyncCandidates; i++) {
            if (pulses[i] > (uint16_t)(roughMedian * 5)) syncIdx[syncCount++] = i;
        }
        if (syncCount == 0) return result; // no plausible frame boundary at all

        // Decode every sync-delimited frame in turn, stopping as soon as
        // two consecutive frames agree (repeat-confirmed) - otherwise keep
        // the first frame that decoded cleanly as a best-effort result.
        bool havePrev = false;
        uint32_t prevValue = 0;
        uint8_t prevBits = 0;
        bool haveBest = false;
        uint32_t bestValue = 0;
        uint8_t bestBits = 0;
        int bestFrameRatio = 0;

        // syncCount==1 still gives one frame: everything after that sync.
        size_t frameStartCount = (syncCount > 1) ? syncCount - 1 : 1;
        for (size_t f = 0; f < frameStartCount; f++) {
            size_t frameBegin = syncIdx[f] + 1;
            size_t frameEnd = (syncCount > 1) ? syncIdx[f + 1] : count;
            if (frameBegin >= frameEnd) continue;

            uint32_t value = 0;
            uint8_t bits = 0;
            int ratio = 0;
            if (!decodeOneFrame(pulses + frameBegin, frameEnd - frameBegin, value, bits, ratio)) continue;

            if (havePrev && prevValue == value && prevBits == bits) {
                result.ok = true;
                result.repeatConfirmed = true;
                result.value = value;
                result.bitCount = bits;
                result.family = classifyFamily(bits, ratio);
                return result;
            }
            if (!haveBest) {
                haveBest = true;
                bestValue = value;
                bestBits = bits;
                bestFrameRatio = ratio;
            }
            prevValue = value;
            prevBits = bits;
            havePrev = true;
        }

        if (!haveBest) return result; // nothing in any frame decoded cleanly
        result.ok = true;
        result.repeatConfirmed = false;
        result.value = bestValue;
        result.bitCount = bestBits;
        result.family = classifyFamily(bestBits, bestFrameRatio);
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

    // One-letter tag for the screen's tight width: P=PT2262/EV1527-family,
    // H=Holtek/CAME-style 12b family, L=long PT2262-style (32+b),
    // G=unclassified generic PWM shape.
    char familyTag(const char* family) {
        if (family[0] == 'P') return 'P';
        if (family[0] == 'H') return 'H';
        if (family[0] == 'l') return 'L';
        return 'G';
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
                snprintf(buf, sizeof(buf), "%.2fMHz [%c]: 0x%lX (%ub%s) x%d%s",
                         freqForChannel(currentBand, r.channel), familyTag(r.decoded.family),
                         (unsigned long)r.decoded.value, r.decoded.bitCount, r.decoded.repeatConfirmed ? "*" : "",
                         r.repeatCount, r.repeatCount >= 2 ? " STATIC" : "");
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
                String detail = decoded.ok ? ("0x" + String(decoded.value, HEX) + " " + decoded.family + " repeat")
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

        String detail = decoded.ok ? ("0x" + String(decoded.value, HEX) + " (" + String(decoded.bitCount) + "b, " +
                                       decoded.family + (decoded.repeatConfirmed ? ", repeat-confirmed)" : ")"))
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
