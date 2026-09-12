// =============================================================================
// WaveRogue - rf_utils.h
//
// Small stateless helpers shared by several modules (entropy/plaintext
// heuristics, hex formatting, CRC) so each module file doesn't reinvent
// them.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace RfUtils {
    // Shannon entropy of `data[0..len)`, in bits/byte (0.0 - 8.0). Properly
    // encrypted or compressed data reads close to 8.0; plaintext text/JSON
    // or fixed/repetitive binary structures read noticeably lower.
    float shannonEntropy(const uint8_t* data, size_t len);

    // Fraction (0.0 - 1.0) of bytes that fall in the printable ASCII range
    // (0x20-0x7E). High values are a strong plaintext signal independent
    // of entropy (e.g. a short ASCII token can have so little data that
    // entropy alone is inconclusive).
    float printableAsciiRatio(const uint8_t* data, size_t len);

    String bytesToHex(const uint8_t* data, size_t len);

    // CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect). Used as a
    // best-effort integrity check where an exact vendor CRC variant isn't
    // confirmed (e.g. the Class B beacon parser) - treat a mismatch there
    // as "unconfirmed", not proof the frame is bad.
    uint16_t crc16Ccitt(const uint8_t* data, size_t len);
}
