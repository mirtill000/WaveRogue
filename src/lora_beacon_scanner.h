// =============================================================================
// WaveRogue - lora_beacon_scanner.h
//
// Class B Gateway Beacon Scanner: confirms a Class-B-capable gateway is
// present and operating by listening on the fixed beacon channel/SF and
// looking for periodic (~128s), fixed-length, unencrypted beacon frames.
//
// The presence + correct periodicity of a beacon-shaped frame at the
// right frequency/SF is itself strong evidence of a live Class B gateway,
// independent of whether we get every byte of its payload layout exactly
// right - regional beacon formats have minor variations, so treat the
// decoded time/coordinates as best-effort and the "a beacon exists here"
// finding as the reliable part.
// =============================================================================
#pragma once

namespace LoraBeaconScanner {
    bool begin();
    void loop();
    void end();
}
