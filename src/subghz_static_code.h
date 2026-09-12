// =============================================================================
// WaveRogue - subghz_static_code.h
//
// Static-Code Legacy System Discovery.
//
// Captures each button-press as a separate "session" of raw OOK/ASK pulse
// timing (segmented by a long quiet gap, i.e. the user releasing the
// button - NOT by the short gaps between the several back-to-back repeats
// a remote sends within one press, which are always identical even for a
// ROLLING-code remote and would trivially "match" for the wrong reason).
// It then compares each new session against previously captured ones: if
// two separate button presses produce the same binary timing pattern
// (within jitter tolerance), the remote is using a fixed/static code and
// is, by definition, 100% vulnerable to a replay attack (see Module:
// Replay Vulnerability Tester to actually verify that against your own
// receiver).
// =============================================================================
#pragma once

namespace SubGhzStaticCode {
    bool begin();
    void loop();
    void end();
}
