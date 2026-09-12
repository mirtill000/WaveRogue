// =============================================================================
// WaveRogue - subghz_bug_detector.h
//
// Analog Bug / Continuous-Carrier Detector.
//
// Sweeps a configurable list of sub-GHz frequencies (BUG_SCAN_FREQ_LIST_MHZ
// in config.h) and watches the CC1101's RSSI reading on each. A signal
// that stays above threshold continuously for longer than a normal
// bursty data transmission would (BUG_CARRIER_MIN_DURATION_MS) is flagged
// as a possible active analog transmitter (a "bug") - most digital OOK/
// FSK devices (remotes, sensors) transmit in short bursts, not a
// continuous carrier.
//
// See config.h for the important hardware caveat: the CC1101 cannot tune
// below ~300 MHz, so classic 49 MHz/FM-broadcast/VHF analog bugs are
// physically out of reach of this module - it only covers what a CC1101
// can actually hear.
// =============================================================================
#pragma once

namespace SubGhzBugDetector {
    bool begin();
    void loop();
    void end();
}
