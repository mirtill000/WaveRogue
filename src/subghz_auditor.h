// =============================================================================
// WaveRogue - subghz_auditor.h
//
// Modules 4-5: raw OOK/ASK sub-GHz work via a CC1101 transceiver put into
// RadioLib's "direct mode", where the chip just outputs/accepts a raw
// digital signal on GDO0 instead of framing packets itself. We time the
// HIGH/LOW edges ourselves with a GPIO interrupt, which is how tools like
// this typically capture arbitrary/unknown fixed-code remotes (garage
// doors, doorbells, simple sensors, etc).
//
//   Module 4 - Sniffer: capture pulse widths, print the raw
//              high/low durations and a binary approximation so you can
//              eyeball whether it looks like Manchester, PWM/PPM, etc.
//   Module 5 - Replay tester: record one button-press worth of pulses,
//              then, on demand, play them back to check whether the
//              receiver accepts the exact same code again (i.e. it has no
//              rolling code) - the classic replay-attack vulnerability
//              test, only ever to be run against your own equipment.
// =============================================================================
#pragma once

namespace SubGhzAuditor {
    bool begin();

    // Module 4: live sniff + print of raw pulse timing.
    void sniffBegin();
    void sniffLoop();
    void sniffEnd();

    // Module 5: record-then-replay workflow.
    void replayBegin();
    void replayLoop();     // call every tick; records automatically,
                            // and replays when UIManager::enterPressed()
    void replayEnd();
}
