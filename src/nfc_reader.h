// =============================================================================
// WaveRogue - nfc_reader.h
//
// NFC Reader/Writer + Mifare Classic default-key auditor.
//
// Uses the ST25R3916 NFC front-end that ships on the Cap CC1101 module
// (same Cap-Bus slot/SPI bus as the CC1101, separate CS/IRQ - see
// config.h), driven through M5Stack's own official M5UnitUnified +
// M5Unit-NFC stack (m5::unit::CapCC1101NFC + m5::nfc::NFCLayerA) rather
// than a standalone ST25R3916 Arduino library - see platformio.ini and
// README.md's "NFC Tools" section for why.
//
// Flow, on every tag presented:
//   1. Poll for an NFC-A tag; report UID/ATQA/SAK/type.
//   2. If it's a recognized MIFARE Classic variant (Mini/1K/4K), sweep
//      every sector against a small dictionary of widely-published
//      default/well-known keys (Key A and Key B) - the same kind of seed
//      dictionary shipped by common open-source MIFARE auditing tools
//      (e.g. mfoc, libnfc's nfc-mfclassic). Flags each sector as cracked
//      (with which key) or still locked.
//   3. For each cracked sector, reads its data blocks and appends them to
//      a per-UID dump file on the SD card (/nfc/<UID>.txt) - the
//      "reader" half.
//   4. On the first cracked ordinary (non-trailer, non-manufacturer-block)
//      data block, performs a one-time write-access self-test: writes
//      the block's own bytes back unchanged and reads them again to
//      confirm the write path actually works - the "writer" half, done
//      safely (no data is ever changed) rather than writing arbitrary
//      content.
//
// A tag that isn't a recognized MIFARE Classic variant is still logged
// (UID/ATQA/SAK/type) - this module is a generic NFC-A reader first, and
// a MIFARE Classic auditor second.
// =============================================================================
#pragma once

namespace NfcReader {
    bool begin();
    void loop();
    void end();
}
