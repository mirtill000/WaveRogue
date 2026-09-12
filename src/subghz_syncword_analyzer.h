// =============================================================================
// WaveRogue - subghz_syncword_analyzer.h
//
// Preamble / Sync-Word Analyzer.
//
// Captures raw OOK/ASK pulse timing like Module 4, auto-detects the
// shortest "chip" duration as its timing unit, reconstructs a raw
// bitstream from it (rounding each pulse to the nearest whole number of
// unit periods - the same NRZ run-length technique used by the POCSAG
// module), and shows the first 32 bits as a candidate sync word/preamble
// fingerprint - in both normal and bit-reversed form, since we have no
// way to know the transmitter's intended bit order from timing alone.
//
// This is matched against a small, DELIBERATELY SHORT table of
// well-known reference values so you can start grouping captures by
// probable vendor/hardware. IMPORTANT CAVEAT: some commonly-quoted sync
// words (e.g. 0x2DD4, often cited for IEEE 802.15.4/Zigbee) belong to
// radios that normally run at 2.4 GHz, which is physically outside the
// CC1101's tuning range - such entries are listed for reference/education
// only, not because this hardware can receive them. Extend the table in
// subghz_syncword_analyzer.cpp with values relevant to your own targets.
// =============================================================================
#pragma once

namespace SubGhzSyncwordAnalyzer {
    bool begin();
    void loop();
    void end();
}
