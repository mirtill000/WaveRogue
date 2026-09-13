// =============================================================================
// WaveRogue - nfc_reader.h
//
// NFC Reader/Writer + Mifare Classic default-key auditor.
//
// Uses the ST25R3916 NFC front-end that ships on the Cap CC1101 module
// (same Cap-Bus slot/SPI bus as the CC1101, separate CS/IRQ - see
// config.h). Rather than reimplementing ISO14443A anticollision and the
// MIFARE Classic Crypto1 cipher from scratch, this module is built on
// the ESP32-validated "ST25R3916 + NFC-RFAL" Arduino library (see
// platformio.ini for the dependency and README.md if it doesn't
// auto-resolve).
//
// Flow, on every tag presented:
//   1. Poll for an NFC-A tag; report UID/ATQA/SAK.
//   2. If the SAK matches a known MIFARE Classic variant (Mini/1K/4K),
//      sweep every sector against a small dictionary of widely-published
//      default/well-known keys (Key A and Key B) - the same kind of seed
//      dictionary shipped by common open-source MIFARE auditing tools
//      (e.g. mfoc, libnfc's nfc-mfclassic). Flags each sector as cracked
//      (with which key) or still locked.
//   3. For each cracked sector, reads its data blocks and appends them to
//      a per-UID dump file on the SD card (/nfc/<UID>.txt) - the
//      "reader" half.
//   4. On the first cracked ordinary (non-trailer) data block, performs a
//      one-time write-access self-test: writes the block's own bytes
//      back unchanged and reads them again to confirm the write path
//      actually works - the "writer" half, done safely (no data is ever
//      changed) rather than writing arbitrary content.
//
// A tag that isn't a recognized MIFARE Classic variant is still logged
// (UID/ATQA/SAK) - this module is a generic NFC-A reader first, and a
// MIFARE Classic auditor second.
// =============================================================================
#pragma once

namespace NfcReader {
    bool begin();
    void loop();
    void end();
}
