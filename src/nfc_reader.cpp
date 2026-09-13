#include "nfc_reader.h"
#include "config.h"
#include "ui_manager.h"
#include "rf_utils.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>

#include <rfal_mf1.h>
#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st25r3916_config.h>
#include <st25r3916_com.h>
#include <st_errno.h>

namespace {
    SPIClass nfcSPI(HSPI);
    RfalRfST25R3916Class nfcHwReader(&nfcSPI, NFC_CS_PIN, NFC_IRQ_PIN);
    RfalNfcClass nfc(&nfcHwReader);
    RfalMf1Class mf1(&nfcHwReader);

    volatile bool tagActivated = false;
    bool sdReady = false;

    // Human-readable ReturnCode names, so a failure prints something
    // actionable ("ERR_IO (7)") instead of just "init failed" - matters
    // a lot for a chip this fiddly to get talking over a shared SPI bus.
    const char* returnCodeToString(ReturnCode code) {
        switch (code) {
            case ERR_NONE: return "ERR_NONE";
            case ERR_NOMEM: return "ERR_NOMEM";
            case ERR_BUSY: return "ERR_BUSY";
            case ERR_IO: return "ERR_IO";
            case ERR_TIMEOUT: return "ERR_TIMEOUT";
            case ERR_REQUEST: return "ERR_REQUEST";
            case ERR_NOMSG: return "ERR_NOMSG";
            case ERR_PARAM: return "ERR_PARAM";
            case ERR_SYSTEM: return "ERR_SYSTEM";
            case ERR_FRAMING: return "ERR_FRAMING";
            case ERR_OVERRUN: return "ERR_OVERRUN";
            case ERR_PROTO: return "ERR_PROTO";
            case ERR_INTERNAL: return "ERR_INTERNAL";
            case ERR_AGAIN: return "ERR_AGAIN";
            case ERR_MEM_CORRUPT: return "ERR_MEM_CORRUPT";
            case ERR_NOT_IMPLEMENTED: return "ERR_NOT_IMPLEMENTED";
            case ERR_PC_CORRUPT: return "ERR_PC_CORRUPT";
            case ERR_SEND: return "ERR_SEND";
            case ERR_IGNORE: return "ERR_IGNORE";
            case ERR_SEMANTIC: return "ERR_SEMANTIC";
            case ERR_SYNTAX: return "ERR_SYNTAX";
            case ERR_CRC: return "ERR_CRC";
            case ERR_NOTFOUND: return "ERR_NOTFOUND";
            case ERR_NOTUNIQUE: return "ERR_NOTUNIQUE";
            case ERR_NOTSUPP: return "ERR_NOTSUPP";
            case ERR_WRITE: return "ERR_WRITE";
            case ERR_FIFO: return "ERR_FIFO";
            case ERR_PAR: return "ERR_PAR";
            case ERR_DONE: return "ERR_DONE";
            case ERR_RF_COLLISION: return "ERR_RF_COLLISION";
            case ERR_HW_OVERRUN: return "ERR_HW_OVERRUN";
            case ERR_RELEASE_REQ: return "ERR_RELEASE_REQ";
            case ERR_SLEEP_REQ: return "ERR_SLEEP_REQ";
            case ERR_WRONG_STATE: return "ERR_WRONG_STATE";
            case ERR_MAX_RERUNS: return "ERR_MAX_RERUNS";
            case ERR_DISABLED: return "ERR_DISABLED";
            case ERR_HW_MISMATCH: return "ERR_HW_MISMATCH";
            case ERR_LINK_LOSS: return "ERR_LINK_LOSS";
            case ERR_INCOMPLETE_BYTE: return "ERR_INCOMPLETE_BYTE";
            default: return "ERR_UNKNOWN";
        }
    }

    void onStateChange(rfalNfcState state) {
        if (state == RFAL_NFC_STATE_ACTIVATED) {
            tagActivated = true;
        }
    }

    bool startDiscovery() {
        // A local, stack-scoped struct is safe here: rfalNfcDiscover()
        // copies its contents rather than retaining the pointer (the
        // upstream example itself passes a local variable from setup(),
        // which returns immediately after).
        rfalNfcDiscoverParam params = {};
        params.compMode = RFAL_COMPLIANCE_MODE_NFC;
        params.devLimit = 1U;
        params.nfcfBR = RFAL_BR_212;
        params.ap2pBR = RFAL_BR_424;
        params.notifyCb = onStateChange;
        params.totalDuration = NFC_DISCOVER_DURATION_MS;
        params.techs2Find = (uint16_t)RFAL_NFC_POLL_TECH_A;
        return nfc.rfalNfcDiscover(&params) == ERR_NONE;
    }

    // -------------------------------------------------------------------
    // A small, widely-published dictionary of MIFARE Classic default/
    // well-known keys - the same seed set shipped by common open-source
    // auditing tools (mfoc, libnfc's nfc-mfclassic). Not exhaustive: a
    // sector that resists all of these is NOT proven secure, only not
    // trivially default-keyed.
    // -------------------------------------------------------------------
    struct DictKey {
        uint8_t key[RFAL_MF1_KEY_LEN];
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
    const uint8_t kKeyTypes[2] = {RFAL_MF1_AUTH_KEY_A, RFAL_MF1_AUTH_KEY_B};
    const char* keyTypeName(uint8_t t) { return (t == RFAL_MF1_AUTH_KEY_A) ? "A" : "B"; }

    // -------------------------------------------------------------------
    // MIFARE Classic sector/block layout. Sectors 0-31 are always 4
    // blocks; on a 4K card, sectors 32-39 are 16 blocks each, starting
    // at block 128.
    // -------------------------------------------------------------------
    int sectorCountForSak(uint8_t sak) {
        switch (sak) {
            case 0x09: return 5;   // MIFARE Mini (320B)
            case 0x08: case 0x28: return 16;  // MIFARE Classic 1K (0x28: some JCOP/clones)
            case 0x18: case 0x38: return 40;  // MIFARE Classic 4K (0x38: some JCOP/clones)
            default: return 0;     // not a recognized Classic SAK
        }
    }
    uint8_t sectorFirstBlock(int sector) {
        return (sector < 32) ? (uint8_t)(sector * 4) : (uint8_t)(128 + (sector - 32) * 16);
    }
    int sectorBlockCountForSector(int sector) { return (sector < 32) ? 4 : 16; }
    uint8_t sectorTrailerBlock(int sector) {
        return sectorFirstBlock(sector) + (uint8_t)(sectorBlockCountForSector(sector) - 1);
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
    void sweepMifareClassic(rfalNfcDevice* device, uint8_t sak, File& f) {
        int nSectors = sectorCountForSak(sak);
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

            uint8_t trailer = sectorTrailerBlock(s);
            bool sectorCracked = false;

            for (int kt = 0; kt < 2 && !sectorCracked; kt++) {
                for (size_t k = 0; k < kNumDefaultKeys && !sectorCracked; k++) {
                    rfalMf1CryptoState crypto = {};
                    uint32_t nonce = 0;
                    ReturnCode err = mf1.authenticate(&crypto, device, trailer, kDefaultKeys[k].key, kKeyTypes[kt], &nonce);
                    if (err != ERR_NONE) continue;

                    sectorCracked = true;
                    cracked++;
                    String keyHex = RfUtils::bytesToHex(kDefaultKeys[k].key, RFAL_MF1_KEY_LEN);
                    UIManager::printLine("Sector " + String(s) + ": key " + String(keyTypeName(kKeyTypes[kt])) +
                                          "=" + keyHex);
                    logSdLine(f, "sector," + String(s) + ",cracked," + String(keyTypeName(kKeyTypes[kt])) + "," +
                                     keyHex + "," + String(kDefaultKeys[k].label));

                    int firstBlk = sectorFirstBlock(s);
                    int nBlk = sectorBlockCountForSector(s);
                    for (int b = 0; b < nBlk; b++) {
                        uint8_t blockNo = (uint8_t)(firstBlk + b);
                        uint8_t data[RFAL_MF1_BLOCK_LEN];
                        if (mf1.readBlock(&crypto, blockNo, data) != ERR_NONE) continue;

                        logSdLine(f, "block," + String(blockNo) + "," + RfUtils::bytesToHex(data, RFAL_MF1_BLOCK_LEN));

                        // One-time write-access self-test: write the
                        // block's own bytes back unchanged, then read
                        // them again to confirm - proves the write path
                        // works without ever changing tag content.
                        if (!wroteWriteTest && blockNo != trailer) {
                            wroteWriteTest = true;
                            uint8_t verify[RFAL_MF1_BLOCK_LEN];
                            bool ok = (mf1.writeBlock(&crypto, blockNo, data) == ERR_NONE) &&
                                      (mf1.readBlock(&crypto, blockNo, verify) == ERR_NONE) &&
                                      (memcmp(data, verify, RFAL_MF1_BLOCK_LEN) == 0);
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

    void handleTag(rfalNfcDevice* device) {
        UIManager::clearLog();
        String uidHex = RfUtils::bytesToHex(device->nfcid, device->nfcidLen);
        uint8_t atqa0 = device->dev.nfca.sensRes.anticollisionInfo;
        uint8_t atqa1 = device->dev.nfca.sensRes.platformInfo;
        uint8_t sak = device->dev.nfca.selRes.sak;

        UIManager::printLine("UID: " + uidHex);
        char meta[24];
        snprintf(meta, sizeof(meta), "ATQA:%02X%02X SAK:%02X", atqa0, atqa1, sak);
        UIManager::printLine(String(meta));

        File f;
        if (sdReady) {
            String path = String(NFC_DUMP_DIR) + "/" + uidHex + ".txt";
            if (SD.exists(path.c_str())) SD.remove(path.c_str()); // rewrite fresh on every re-scan of the same tag
            f = SD.open(path.c_str(), FILE_WRITE);
            if (f) {
                logSdLine(f, "uid," + uidHex);
                logSdLine(f, String("atqa,") + meta);
                UIManager::printLine("Saving to " + path);
            } else {
                UIManager::printLine("[!] Could not open " + path);
            }
        } else {
            UIManager::printLine("[!] No SD - results not saved");
        }

        if (RfalMf1Class::isNfcaDevice(device) && sectorCountForSak(sak) > 0) {
            sweepMifareClassic(device, sak, f);
        } else {
            UIManager::printLine("Not a recognized MIFARE");
            UIManager::printLine("Classic SAK - UID logged only.");
        }

        if (f) f.close();
    }
}

bool NfcReader::begin() {
    pinMode(NFC_CS_PIN, OUTPUT);
    digitalWrite(NFC_CS_PIN, HIGH);
    pinMode(NFC_IRQ_PIN, INPUT); // belt-and-suspenders: the library should
                                 // do this itself, but costs nothing here
    nfcSPI.begin(NFC_SPI_SCK_PIN, NFC_SPI_MISO_PIN, NFC_SPI_MOSI_PIN, NFC_CS_PIN);
    delay(50); // let the chip's power/SPI lines settle before probing it

    ReturnCode initErr = nfc.rfalNfcInitialize();
    if (initErr != ERR_NONE) {
        UIManager::printLine("ST25R3916 init failed:");
        UIManager::printLine(String(returnCodeToString(initErr)) + " (" + String(initErr) + ")");

        if (initErr == ERR_HW_MISMATCH) {
            // rfalNfcInitialize() bailed because reading register 0x3F
            // (IC_IDENTITY) didn't match the known ST25R3916/3916B type
            // bits. That's ambiguous by itself: it fires identically
            // whether a different chip is really there, OR the CS/IRQ
            // wiring for THIS chip is bad and the register read just
            // came back as noise (0x00/0xFF are the classic "nothing
            // answered" values). Read it again directly and print the
            // raw byte so which case this is stops being a guess.
            uint8_t rawId = 0;
            nfcHwReader.st25r3916ReadRegister(ST25R3916_REG_IC_IDENTITY, &rawId);
            UIManager::printLine("Raw reg 0x3F = 0x" + String(rawId, HEX));
            if (rawId == 0x00 || rawId == 0xFF) {
                UIManager::printLine("-> looks like nothing");
                UIManager::printLine("   answered (wiring/CS/IRQ)");
            } else {
                UIManager::printLine("-> chip responded, but");
                UIManager::printLine("   with an unexpected ID");
            }
        }

        UIManager::printLine("Check: Cap CC1101 seated");
        UIManager::printLine("firmly? NFC_CS/IRQ correct");
        UIManager::printLine("in config.h (G6/G4)?");
        return false;
    }

    sdReady = SD.begin(SD_CS_PIN);
    if (sdReady) {
        SD.mkdir(NFC_DUMP_DIR);
    } else {
        UIManager::printLine("[!] SD card init failed");
    }

    tagActivated = false;
    UIManager::printLine("Present an NFC-A tag/badge");
    UIManager::printLine("(MIFARE Classic: default-key");
    UIManager::printLine(" sweep runs automatically)");
    startDiscovery();
    return true;
}

void NfcReader::loop() {
    nfc.rfalNfcWorker();
    UIManager::setStatus(tagActivated ? "Tag detected - processing..." : "Waiting for a tag...");

    if (!tagActivated) return;
    tagActivated = false;

    rfalNfcDevice* device = nullptr;
    if (nfc.rfalNfcGetActiveDevice(&device) == ERR_NONE && device != nullptr) {
        handleTag(device);
    } else {
        UIManager::printLine("[!] Lost tag before it could be read");
    }

    nfc.rfalNfcDeactivate(false);
    startDiscovery(); // resume listening for the next tag
}

void NfcReader::end() {
    nfc.rfalNfcDeactivate(false);
}
