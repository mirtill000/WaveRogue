// =============================================================================
// WaveRogue - subghz_wmbus_scanner.h
//
// Wireless M-Bus (EN 13757-4) Smart Meter Scanner, S-mode (868.95 MHz,
// Manchester @ 32.768 kbps - the common mode for EU water/gas/heat
// meters).
//
// Decodes the fixed "block 1" header that every wM-Bus telegram starts
// with: L-field (length), C-field (control), M-field (manufacturer,
// EN 13757-3 3-letter code), A-field (serial number/version/device type),
// and CI-field (tells us the application layer format, including whether
// the payload that follows is AES-128 encrypted or sent in the clear).
//
// HONESTY NOTE: wM-Bus's exact CRC framing (which bytes each CRC-16
// covers, block boundaries beyond block 1) is intricate enough that we do
// NOT attempt to reproduce/verify it here without hardware to test
// against. Instead, a decode is accepted only when the L-field and
// manufacturer code land in plausible ranges - good enough to be useful,
// but treat results as best-effort, not spec-certified.
// =============================================================================
#pragma once

namespace SubGhzWmbusScanner {
    bool begin();
    void loop();
    void end();
}
