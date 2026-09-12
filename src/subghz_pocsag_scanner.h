// =============================================================================
// WaveRogue - subghz_pocsag_scanner.h
//
// POCSAG Pager Scanner.
//
// Paging frequencies are licensed and vary hugely by country and even by
// site (a hospital or factory's own on-site paging transmitter is
// commonly in the 148/154/173/453-470 MHz range depending on region and
// era) - there is no universal default. SET POCSAG_FREQ_MHZ in config.h
// to a frequency you are actually authorized to audit before using this.
//
// Implements: NRZ-FSK bit recovery from raw edge timing, POCSAG frame
// sync (0x7CD215D8) detection, and codeword classification (address vs
// message vs idle), with a best-effort NUMERIC message decode (the
// standard 4-bit BCD-ish digit table). Alphanumeric decode is NOT
// implemented (POCSAG's cross-codeword 7-bit character packing is a
// meaningfully bigger undertaking) - unrecognized/alpha codewords are
// still shown as raw hex so you can see that traffic exists even without
// a full decode.
//
// FLEX is a different, more complex protocol (4-level FSK, interleaving)
// and is explicitly NOT decoded here - only POCSAG.
// =============================================================================
#pragma once

namespace SubGhzPocsagScanner {
    bool begin();
    void loop();
    void end();
}
