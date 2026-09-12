// =============================================================================
// WaveRogue - subghz_weather_decoder.h
//
// Weather/TPMS Telemetry Decoder (rtl_433-style dictionary approach).
//
// Captures raw OOK pulse timing exactly like Module 4, then tries to match
// it against a small dictionary of known PWM-encoded sensor protocols.
// Currently implements one fully-worked entry: the "Nexus"-style
// temperature/humidity sensor (see config.h for the many rebrands that
// share this same 36-bit frame). If the capture doesn't match anything in
// the dictionary (including any TPMS sensor - see config.h for why TPMS
// isn't covered), it falls back to a raw pulse dump so the module still
// shows you *something* rather than silently failing.
//
// This is intentionally NOT a full rtl_433 port (that's ~200 protocols
// and a much bigger undertaking) - it's a real, working example plus an
// obvious extension point for adding more dictionary entries.
// =============================================================================
#pragma once

namespace SubGhzWeatherDecoder {
    bool begin();
    void loop();
    void end();
}
