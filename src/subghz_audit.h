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
//     decoder: every sync-gap-delimited frame in the capture (a button
//     press usually repeats the same code several times back-to-back) is
//     decoded independently and cross-checked against its neighbor - two
//     consecutive frames agreeing is reported as "repeat-confirmed",
//     a much stronger signal than a single decode. Each frame is tried
//     against both common public short:long pulse ratios (~1:3, the
//     Princeton/PT2262/EV1527 shape, and ~1:2, the Holtek HT12x/CAME-
//     style shape) and whichever decodes the most bits wins; the result
//     is then labeled against the matching bit-count range (24-bit
//     PT2262/EV1527-family, 12-bit Holtek/CAME-style, 32+-bit long
//     PT2262-style) as a coarse hint, not a full protocol fingerprint.
//     A pulse train that doesn't fit either ratio at all is kept and
//     reported as unrecognized raw data rather than forced into a decode.
//   - Each capture on a given channel is compared against recent history
//     on that same channel: an exact repeat across separate button
//     presses flags a static/fixed code (100% replay-vulnerable); a
//     different payload each time suggests a rolling code instead.
//
// All findings are appended to an SD log (SUBGHZ_AUDIT_LOG_PATH).
//
// 315 MHz has no complete antenna-matching path on this hardware - the
// low-band path it needs requires RF_SW1, which isn't controllable from
// the Cap-Bus header (see subghz_rf_switch.h) - so expect reduced
// range/sensitivity there compared to 433/868/915, which all share one
// fully-selectable wideband path.
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
