// =============================================================================
// WaveRogue - subghz_audit.h
//
// Sub-GHz Audit: a single module replacing the earlier set of separate
// Sub-GHz tools. The operator picks one of four ISM-band presets (315,
// 433, 868, 915 MHz) before starting; the module then sweeps that band
// looking for activity and reports what it finds:
//
//   - A continuous carrier held above an RSSI threshold for several
//     seconds is flagged as a possible active transmitter/bug (as
//     opposed to a burst of data).
//   - A burst of OOK/ASK edges is captured in full (raw pulse timing,
//     direct-mode capture) and run through a generic PWM fixed-code
//     decoder: if the pulse train looks like a clean short/long-pulse
//     bitstream with a long sync gap (the pattern used by cheap
//     PT2262/EV1527-style remotes and countless clones), it's decoded
//     to a bit value; otherwise the raw capture is kept and reported as
//     unrecognized.
//   - Each capture on a given channel is compared against recent history
//     on that same channel: an exact repeat across separate button
//     presses flags a static/fixed code (100% replay-vulnerable); a
//     different payload each time suggests a rolling code instead.
//
// All findings are appended to an SD log (SUBGHZ_AUDIT_LOG_PATH).
//
// 315 MHz has no dedicated antenna-matching path on this hardware (the
// Cap CC1101's RF switch only exposes the 433 vs. 868/915 selection -
// see subghz_rf_switch.h) - it's tuned through the 433 MHz path instead,
// so expect reduced range/sensitivity there compared to the other bands.
//
// The overall approach (sweep to find activity, classify continuous
// carrier vs. burst, decode short/long PWM pulses, flag repeats) follows
// the same general design used by other Sub-GHz auditing tools for
// Cardputer-class hardware (e.g. the CC1101 tooling in
// github.com/7h30th3r0n3/Evil-M5Project) - independently implemented
// here rather than copied, since that project's repository carries no
// explicit open-source license.
// =============================================================================
#pragma once

namespace SubGhzAudit {
    // Keep in sync with kBands[] in subghz_audit.cpp and the menu labels
    // built from it in main.cpp.
    enum class Band {
        BAND_315 = 0,
        BAND_433,
        BAND_868,
        BAND_915,
        COUNT
    };

    // Returns a short label for a band, e.g. "433 MHz" - used to build the
    // band-select menu without duplicating the names in main.cpp.
    const char* bandLabel(Band band);

    bool begin(Band band);
    void loop();
    void end();
}
