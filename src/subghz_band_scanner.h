// =============================================================================
// WaveRogue - subghz_band_scanner.h
//
// Multi-Frequency Band Scanner ("sniffer di banda").
//
// The Raw Sniffer (Module 4) and Analog Bug Detector both work at one
// frequency at a time - this module sweeps a whole range (default: the
// EU 433 MHz SRD sub-band) so you don't have to already know which exact
// channel a device transmits on. It hops with a short dwell per channel,
// watching raw edge timing (like the Raw Sniffer); as soon as a channel
// shows enough edges to look like a real modulated transmission (not
// just noise), it locks onto that frequency and displays the captured
// pulses live, the same way the Raw Sniffer does. Once the channel goes
// quiet for a bit, it resumes sweeping from the next channel.
//
// This is deliberately edge-count-based, not RSSI-based: a continuous,
// unmodulated carrier (the thing the Analog Bug Detector looks for)
// produces few or no edges and won't trigger a lock here - the two
// modules are complementary, not overlapping.
// =============================================================================
#pragma once

namespace SubGhzBandScanner {
    bool begin();
    void loop();
    void end();
}
