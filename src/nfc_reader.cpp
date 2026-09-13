#include "nfc_reader.h"
#include "config.h"
#include "ui_manager.h"
#include "rf_utils.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>

// M5Unified.h must come first: wiring/m5_unit_unified_wiring.hpp calls
// M5.getBoard()/M5.getPin() but doesn't include M5Unified.h itself - it
// expects the includer to have already done so.
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <M5Utility.h>
#include <wiring/m5_unit_unified_wiring.hpp>

using m5::nfc::a::PICC;
using m5::nfc::a::mifare::classic::Key;
using m5::nfc::a::mifare::classic::get_sector;
using m5::nfc::a::mifare::classic::get_sector_trailer_block_from_sector;

namespace {
    m5::unit::UnitUnified Units;
    m5::unit::CapCC1101NFC unit{NFC_CS_PIN};
    m5::nfc::NFCLayerA nfc_a{unit};

    bool sdReady = false;
    // The ST25R3916's full bring-up (chip-ID detection, CMD_SET_DEFAULT,
    // oscillator enable, RF field on) only tolerates running once per
    // boot - calling Units.begin() again on an already-initialized chip
    // with its field already on makes it fail. Re-entering the module
    // after a successful init just resumes polling instead.
    bool chipInitialized = false;

    // -------------------------------------------------------------------
    // A small, widely-published dictionary of MIFARE Classic default/
    // well-known keys - the same seed set shipped by common open-source
    // auditing tools (mfoc, libnfc's nfc-mfclassic). Not exhaustive: a
    // sector that resists all of these is NOT proven secure, only not
    // trivially default-keyed.
    // -------------------------------------------------------------------
    struct DictKey {
        uint8_t key[6];
        const char* label;
    };
    const DictKey kDefaultKeys[] = {
        {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, "factory default"},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, "all-zero"},
        {{0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}, "NXP MAD key A"},
        {{0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}, "NDEF/MAD key B"},
        {{0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5}, "common default"},
        {{0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD}, "common default"},
        {{0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A}, "common default"},
        {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}, "common default"},
        {{0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97}, "common default"},
        {{0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F}, "common default"},
        {{0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91}, "common default"},
        {{0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6}, "common default"},
        {{0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9}, "common default"},
    };
    constexpr size_t kNumDefaultKeys = sizeof(kDefaultKeys) / sizeof(kDefaultKeys[0]);

    Key toKey(const uint8_t* b) {
        Key k;
        memcpy(k.data(), b, 6);
        return k;
    }

    bool authenticate(uint8_t block, const Key& key, bool useKeyB) {
        return useKeyB ? nfc_a.mifareClassicAuthenticateB(block, key)
                        : nfc_a.mifareClassicAuthenticateA(block, key);
    }

    void logSdLine(File& f, const String& line) {
        if (f) {
            f.println(line);
            f.flush();
        }
    }

    // Sweeps every sector of a detected MIFARE Classic card against the
    // default-key dictionary, dumping cracked sectors to `f` and running
    // one write-access self-test along the way.
    void sweepMifareClassic(const PICC& picc, File& f) {
        int nSectors = (int)get_sector(picc.blocks - 1) + 1;
        UIManager::printLine("MIFARE Classic (" + String(nSectors) + " sectors)");
        logSdLine(f, "type,mifare_classic,sectors," + String(nSectors));

        int cracked = 0;
        bool wroteWriteTest = false;

        // NOTE: a full sweep runs to completion inside this one
        // NfcReader::loop() call, unlike every other module's loop(),
        // which returns quickly and lets main.cpp's per-tick
        // UIManager::pollInput() keep ESC responsive between calls.
        // Worst case (a fully-locked 4K card: 40 sectors x 2 key types x
        // the dictionary below) is on the order of tens of seconds, not
        // interruptible mid-sweep in this first version - a documented
        // simplification, not an oversight. The status bar still updates
        // per sector so it's clear the device hasn't frozen.
        for (int s = 0; s < nSectors; s++) {
            UIManager::setStatus("Sector " + String(s + 1) + "/" + String(nSectors) + " - trying default keys...");

            uint8_t trailer = (uint8_t)get_sector_trailer_block_from_sector((uint16_t)s);
            bool sectorCracked = false;

            for (int kt = 0; kt < 2 && !sectorCracked; kt++) {
                bool useKeyB = (kt == 1);
                for (size_t k = 0; k < kNumDefaultKeys && !sectorCracked; k++) {
                    Key key = toKey(kDefaultKeys[k].key);
                    if (!authenticate(trailer, key, useKeyB)) continue;

                    sectorCracked = true;
                    cracked++;
                    String keyHex = RfUtils::bytesToHex(kDefaultKeys[k].key, 6);
                    const char* ktName = useKeyB ? "B" : "A";
                    UIManager::printLine("Sector " + String(s) + ": key " + String(ktName) + "=" + keyHex);
                    logSdLine(f, "sector," + String(s) + ",cracked," + String(ktName) + "," + keyHex + "," +
                                     kDefaultKeys[k].label);

                    int firstBlk = (s < 32) ? s * 4 : 128 + (s - 32) * 16;
                    int nBlk = (s < 32) ? 4 : 16;
                    for (int b = 0; b < nBlk; b++) {
                        uint8_t blockNo = (uint8_t)(firstBlk + b);
                        uint8_t data[16];
                        if (!nfc_a.read16(data, blockNo)) continue;

                        logSdLine(f, "block," + String(blockNo) + "," + RfUtils::bytesToHex(data, 16));

                        // One-time write-access self-test: write the
                        // block's own bytes back unchanged, then read
                        // them again to confirm - proves the write path
                        // works without ever changing tag content. Skip
                        // the trailer (holds the keys/access bits) and
                        // block 0 of sector 0 (hardware-locked
                        // manufacturer block on genuine cards).
                        bool isManufacturerBlock = (s == 0 && b == 0);
                        if (!wroteWriteTest && blockNo != trailer && !isManufacturerBlock) {
                            wroteWriteTest = true;
                            uint8_t verify[16];
                            bool ok = nfc_a.write16(blockNo, data, 16) &&
                                      nfc_a.read16(verify, blockNo) &&
                                      memcmp(data, verify, 16) == 0;
                            UIManager::printLine(ok ? "Write-access test: OK" : "Write-access test: FAILED");
                            logSdLine(f, String("write_test,") + (ok ? "ok" : "failed"));
                        }
                    }
                }
            }

            if (!sectorCracked) {
                logSdLine(f, "sector," + String(s) + ",locked");
            }
        }

        UIManager::printLine(String(cracked) + "/" + String(nSectors) + " sectors cracked");
        logSdLine(f, "summary,cracked," + String(cracked) + ",total," + String(nSectors));
    }

    void handleTag(PICC& picc) {
        UIManager::clearLog();
        String uidHex = RfUtils::bytesToHex(picc.uid, picc.size);
        UIManager::printLine("UID: " + uidHex);
        char meta[40];
        snprintf(meta, sizeof(meta), "ATQA:%04X SAK:%02X", picc.atqa, picc.sak);
        UIManager::printLine(String(meta));
        UIManager::printLine("Type: " + String(picc.typeAsString().c_str()));

        File f;
        if (sdReady) {
            String path = String(NFC_DUMP_DIR) + "/" + uidHex + ".txt";
            if (SD.exists(path.c_str())) SD.remove(path.c_str()); // rewrite fresh on every re-scan of the same tag
            f = SD.open(path.c_str(), FILE_WRITE);
            if (f) {
                logSdLine(f, "uid," + uidHex);
                logSdLine(f, String("meta,") + meta);
                UIManager::printLine("Saving to " + path);
            } else {
                UIManager::printLine("[!] Could not open " + path);
            }
        } else {
            UIManager::printLine("[!] No SD - results not saved");
        }

        if (picc.isMifareClassic() && picc.blocks > 0) {
            sweepMifareClassic(picc, f);
        } else {
            UIManager::printLine("Not a recognized MIFARE");
            UIManager::printLine("Classic type - UID logged only.");
        }

        if (f) f.close();
    }
}

bool NfcReader::begin() {
    if (chipInitialized) {
        UIManager::printLine("ST25R3916 already initialized");
        UIManager::printLine("Present an NFC-A tag/badge");
        return true;
    }

    // POWER_EN: harmless to drive even though board bring-up (M5Cardputer
    // .begin(), already called once at boot) is expected to leave it
    // usable on its own - cheap insurance either way.
    pinMode(NFC_POWER_EN_PIN, OUTPUT);
    digitalWrite(NFC_POWER_EN_PIN, HIGH);

    // The Cap CC1101 board carries both the CC1101 and the ST25R3916 on
    // one SPI bus, on separate CS lines. M5UnitUnified's SPI adapter only
    // ever manages its own CS around a transaction - it never deselects
    // the CC1101 - so do that explicitly before touching the NFC chip.
    pinMode(SUBGHZ_CS_PIN, OUTPUT);
    digitalWrite(SUBGHZ_CS_PIN, HIGH);

    // addSPI() resolves the shared Cap-Bus SPI pins (SCK/MOSI/MISO)
    // itself via M5Unified's board profile; the unit's CS is passed
    // explicitly via its constructor above (NFC_CS_PIN) rather than
    // trusting the library's own default. Report what got resolved and
    // where init failed, since a bare "init failed" isn't actionable on
    // this shared-bus, multi-library-dependent hardware.
    auto spiPinInfo = m5::unit::wiring::spiPins();
    UIManager::printLine("Board: 0x" + String((unsigned)M5.getBoard(), HEX));
    UIManager::printLine("SPI: sck=" + String(spiPinInfo.sclk) + " miso=" + String(spiPinInfo.miso) +
                          " mosi=" + String(spiPinInfo.mosi));

    bool spiAdded = m5::unit::wiring::addSPI(Units, unit, 10000000, 1);
    UIManager::printLine(String("addSPI: ") + (spiAdded ? "ok" : "FAILED"));
    bool unitsBegan = spiAdded && Units.begin();
    if (spiAdded) {
        UIManager::printLine(String("Units.begin: ") + (unitsBegan ? "ok" : "FAILED"));
    }

    if (!unitsBegan) {
        UIManager::printLine("ST25R3916 init failed");
        UIManager::printLine("Check: Cap CC1101 seated");
        UIManager::printLine("firmly in the Cap-Bus slot?");
        return false;
    }

    sdReady = SD.begin(SD_CS_PIN);
    if (sdReady) {
        SD.mkdir(NFC_DUMP_DIR);
    } else {
        UIManager::printLine("[!] SD card init failed");
    }

    chipInitialized = true;
    UIManager::printLine("ST25R3916 ready");
    UIManager::printLine("Present an NFC-A tag/badge");
    UIManager::printLine("(MIFARE Classic: default-key");
    UIManager::printLine(" sweep runs automatically)");
    return true;
}

void NfcReader::loop() {
    Units.update();
    UIManager::setStatus("Waiting for a tag...");

    PICC picc{};
    if (!nfc_a.detect(picc, 100)) {
        return; // no tag this tick - let main.cpp poll the keyboard for ESC
    }
    if (!nfc_a.identify(picc) || !nfc_a.reactivate(picc)) {
        UIManager::printLine("[!] Lost tag before it could be read");
        nfc_a.deactivate();
        return;
    }

    UIManager::setStatus("Tag detected - processing...");
    handleTag(picc);
    nfc_a.deactivate();
}

void NfcReader::end() {
    nfc_a.deactivate();
}
