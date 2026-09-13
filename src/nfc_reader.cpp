#include "nfc_reader.h"
#include "config.h"
#include "ui_manager.h"
#include "rf_utils.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <cctype>
#include <cstdlib>
#include <vector>
#include <array>

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
    // A dictionary of MIFARE Classic default/well-known/pattern keys -
    // the same kind of seed set shipped by common open-source auditing
    // tools (mfoc, libnfc's nfc-mfclassic). Not exhaustive: a sector that
    // resists all of these (and the SD wordlist below) is NOT proven
    // secure, only not trivially default-keyed. Entries labeled "pattern"
    // are trivially-guessable byte patterns rather than confirmed
    // real-world keys - included because some deployments really do use
    // them, not because they're independently documented defaults.
    // Entries labeled "extended dictionary" are additional candidate
    // keys supplied by a WaveRogue user, of unverified individual
    // provenance - included on the same basis as the pattern keys.
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
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, "pattern"},
        {{0x01, 0x01, 0x01, 0x01, 0x01, 0x01}, "pattern"},
        {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11}, "pattern"},
        {{0x22, 0x22, 0x22, 0x22, 0x22, 0x22}, "pattern"},
        {{0x33, 0x33, 0x33, 0x33, 0x33, 0x33}, "pattern"},
        {{0x44, 0x44, 0x44, 0x44, 0x44, 0x44}, "pattern"},
        {{0x55, 0x55, 0x55, 0x55, 0x55, 0x55}, "pattern"},
        {{0x66, 0x66, 0x66, 0x66, 0x66, 0x66}, "pattern"},
        {{0x77, 0x77, 0x77, 0x77, 0x77, 0x77}, "pattern"},
        {{0x88, 0x88, 0x88, 0x88, 0x88, 0x88}, "pattern"},
        {{0x99, 0x99, 0x99, 0x99, 0x99, 0x99}, "pattern"},
        {{0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA}, "pattern"},
        {{0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB}, "pattern"},
        {{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, "pattern"},
        {{0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD}, "pattern"},
        {{0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE}, "pattern"},
        {{0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, "pattern"},
        {{0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45}, "pattern"},
        {{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB}, "pattern"},
        {{0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54}, "pattern"},
        {{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}, "pattern"},
        {{0x06, 0x05, 0x04, 0x03, 0x02, 0x01}, "pattern"},
        {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, "pattern"},
        {{0x66, 0x55, 0x44, 0x33, 0x22, 0x11}, "pattern"},
        {{0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x22}, "pattern"},
        {{0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00}, "pattern"},
        {{0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF}, "pattern"},
        {{0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00}, "pattern"},
        {{0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF}, "pattern"},
        {{0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5}, "pattern"},
        {{0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A}, "pattern"},
        {{0x12, 0x34, 0x56, 0x78, 0x90, 0xAB}, "pattern"},
        {{0x99, 0x88, 0x77, 0x66, 0x55, 0x44}, "pattern"},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, "pattern"},
        {{0xFF, 0x00, 0x00, 0x00, 0x00, 0x00}, "pattern"},
        {{0x12, 0x31, 0x23, 0x12, 0x31, 0x23}, "pattern"},
        {{0xAB, 0xCA, 0xBC, 0xAB, 0xCA, 0xBC}, "pattern"},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x02}, "extended dictionary"},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x0A}, "extended dictionary"},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x0B}, "extended dictionary"},
        {{0x00, 0x00, 0x00, 0x00, 0x18, 0xDE}, "extended dictionary"},
        {{0x00, 0x00, 0x01, 0x4B, 0x5C, 0x31}, "extended dictionary"},
        {{0x00, 0x00, 0x0F, 0xFE, 0x24, 0x88}, "extended dictionary"},
        {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, "extended dictionary"},
        {{0x00, 0x30, 0x03, 0x00, 0x30, 0x03}, "extended dictionary"},
        {{0x00, 0x3C, 0xC4, 0x20, 0x00, 0x1A}, "extended dictionary"},
        {{0x01, 0x38, 0x89, 0x34, 0x38, 0x91}, "extended dictionary"},
        {{0x01, 0xFA, 0x3F, 0xC6, 0x83, 0x49}, "extended dictionary"},
        {{0x02, 0x12, 0x09, 0x19, 0x75, 0x91}, "extended dictionary"},
        {{0x02, 0x63, 0xDE, 0x12, 0x78, 0xF3}, "extended dictionary"},
        {{0x02, 0x97, 0x92, 0x7C, 0x0F, 0x77}, "extended dictionary"},
        {{0x06, 0x7D, 0xB4, 0x54, 0x54, 0xA9}, "extended dictionary"},
        {{0x10, 0x00, 0x00, 0x00, 0x00, 0x00}, "extended dictionary"},
        {{0x12, 0xF2, 0xEE, 0x34, 0x78, 0xC1}, "extended dictionary"},
        {{0x14, 0xD4, 0x46, 0xE3, 0x33, 0x63}, "extended dictionary"},
        {{0x15, 0xFC, 0x4C, 0x76, 0x13, 0xFE}, "extended dictionary"},
        {{0x16, 0xF2, 0x1A, 0x82, 0xEC, 0x84}, "extended dictionary"},
        {{0x16, 0xF3, 0xD5, 0xAB, 0x11, 0x39}, "extended dictionary"},
        {{0x17, 0x75, 0x88, 0x56, 0xB1, 0x82}, "extended dictionary"},
        {{0x19, 0x99, 0xA3, 0x55, 0x4A, 0x55}, "extended dictionary"},
        {{0x1F, 0xC2, 0x35, 0xAC, 0x13, 0x09}, "extended dictionary"},
        {{0x20, 0x00, 0x00, 0x00, 0x00, 0x00}, "extended dictionary"},
        {{0x22, 0xC1, 0xBA, 0xE1, 0xAA, 0xCD}, "extended dictionary"},
        {{0x24, 0x3F, 0x16, 0x09, 0x18, 0xD1}, "extended dictionary"},
        {{0x25, 0x09, 0x4D, 0xF6, 0xF1, 0x48}, "extended dictionary"},
        {{0x26, 0x94, 0x0B, 0x21, 0xFF, 0x5D}, "extended dictionary"},
        {{0x27, 0xDD, 0x91, 0xF1, 0xFC, 0xF1}, "extended dictionary"},
        {{0x2A, 0x3C, 0x34, 0x7A, 0x12, 0x00}, "extended dictionary"},
        {{0x2B, 0xA9, 0x62, 0x1E, 0x0A, 0x36}, "extended dictionary"},
        {{0x31, 0x4B, 0x49, 0x47, 0x49, 0x56}, "extended dictionary"},
        {{0x32, 0x4F, 0x5D, 0xF6, 0x53, 0x10}, "extended dictionary"},
        {{0x32, 0xAC, 0x3B, 0x90, 0xAC, 0x13}, "extended dictionary"},
        {{0x33, 0xF9, 0x74, 0xB4, 0x27, 0x69}, "extended dictionary"},
        {{0x34, 0xD1, 0xDF, 0x99, 0x34, 0xC5}, "extended dictionary"},
        {{0x35, 0xC3, 0xD2, 0xCA, 0xEE, 0x88}, "extended dictionary"},
        {{0x3A, 0x42, 0xF3, 0x3A, 0xF4, 0x29}, "extended dictionary"},
        {{0x3D, 0xF1, 0x4C, 0x80, 0x00, 0xA1}, "extended dictionary"},
        {{0x3E, 0x35, 0x54, 0xAF, 0x0E, 0x12}, "extended dictionary"},
        {{0x3E, 0x65, 0xE4, 0xFB, 0x65, 0xB3}, "extended dictionary"},
        {{0x43, 0x4F, 0x4D, 0x4D, 0x4F, 0x41}, "extended dictionary"},
        {{0x43, 0x4F, 0x4D, 0x4D, 0x4F, 0x42}, "extended dictionary"},
        {{0x43, 0xAB, 0x19, 0xEF, 0x5C, 0x31}, "extended dictionary"},
        {{0x44, 0xAB, 0x09, 0x01, 0x08, 0x45}, "extended dictionary"},
        {{0x45, 0x48, 0x41, 0x58, 0x54, 0x43}, "extended dictionary"},
        {{0x46, 0x07, 0x22, 0x12, 0x25, 0x10}, "extended dictionary"},
        {{0x47, 0x52, 0x4F, 0x55, 0x50, 0x41}, "extended dictionary"},
        {{0x47, 0x52, 0x4F, 0x55, 0x50, 0x42}, "extended dictionary"},
        {{0x48, 0xFF, 0xE7, 0x12, 0x94, 0xA0}, "extended dictionary"},
        {{0x49, 0x1C, 0xDC, 0xFB, 0x77, 0x52}, "extended dictionary"},
        {{0x4A, 0xD1, 0xE2, 0x73, 0xEA, 0xF1}, "extended dictionary"},
        {{0x4A, 0xF9, 0xD7, 0xAD, 0xEB, 0xE4}, "extended dictionary"},
        {{0x4B, 0x0B, 0x20, 0x10, 0x7C, 0xCB}, "extended dictionary"},
        {{0x4B, 0x79, 0x1B, 0xEA, 0x7B, 0xCC}, "extended dictionary"},
        {{0x50, 0x52, 0x49, 0x56, 0x41, 0x41}, "extended dictionary"},
        {{0x50, 0x52, 0x49, 0x56, 0x41, 0x42}, "extended dictionary"},
        {{0x50, 0x52, 0x49, 0x56, 0x54, 0x41}, "extended dictionary"},
        {{0x50, 0x52, 0x49, 0x56, 0x54, 0x42}, "extended dictionary"},
        {{0x51, 0x28, 0x4C, 0x36, 0x86, 0xA6}, "extended dictionary"},
        {{0x52, 0x8C, 0x9D, 0xFF, 0xE2, 0x8C}, "extended dictionary"},
        {{0x54, 0x72, 0x61, 0x76, 0x65, 0x6C}, "extended dictionary"},
        {{0x55, 0xF5, 0xA5, 0xDD, 0x38, 0xC9}, "extended dictionary"},
        {{0x56, 0x4C, 0x50, 0x5F, 0x4D, 0x41}, "extended dictionary"},
        {{0x56, 0x93, 0x69, 0xC5, 0xA0, 0xE5}, "extended dictionary"},
        {{0x5C, 0x59, 0x8C, 0x9C, 0x58, 0xB5}, "extended dictionary"},
        {{0x5E, 0xB8, 0xF8, 0x84, 0xC8, 0xD1}, "extended dictionary"},
        {{0x5F, 0x14, 0x67, 0x16, 0xE3, 0x73}, "extended dictionary"},
        {{0x63, 0x21, 0x93, 0xBE, 0x1C, 0x3C}, "extended dictionary"},
        {{0x63, 0x38, 0xA3, 0x71, 0xC0, 0xED}, "extended dictionary"},
        {{0x63, 0xF1, 0x7A, 0x44, 0x9A, 0xF0}, "extended dictionary"},
        {{0x64, 0x3F, 0xB6, 0xDE, 0x22, 0x17}, "extended dictionary"},
        {{0x64, 0x46, 0x72, 0xBD, 0x4A, 0xFE}, "extended dictionary"},
        {{0x64, 0xE3, 0xC1, 0x03, 0x94, 0xC2}, "extended dictionary"},
        {{0x68, 0x2D, 0x40, 0x1A, 0xBB, 0x09}, "extended dictionary"},
        {{0x68, 0xD3, 0x02, 0x88, 0x91, 0x0A}, "extended dictionary"},
        {{0x69, 0x31, 0x43, 0xF1, 0x03, 0x68}, "extended dictionary"},
        {{0x6A, 0x47, 0x0D, 0x54, 0x12, 0x7C}, "extended dictionary"},
        {{0x72, 0x2B, 0xFC, 0xC5, 0x37, 0x5F}, "extended dictionary"},
        {{0x74, 0x0E, 0x9A, 0x4F, 0x9A, 0xAF}, "extended dictionary"},
        {{0x75, 0xCC, 0xB5, 0x9C, 0x9B, 0xED}, "extended dictionary"},
        {{0x75, 0xD8, 0x69, 0x0F, 0x21, 0xB6}, "extended dictionary"},
        {{0x75, 0xED, 0xE6, 0xA8, 0x44, 0x60}, "extended dictionary"},
        {{0x77, 0x69, 0x74, 0x68, 0x75, 0x73}, "extended dictionary"},
        {{0x82, 0xF4, 0x35, 0xDE, 0xDF, 0x01}, "extended dictionary"},
        {{0x85, 0x67, 0x5B, 0x20, 0x00, 0x17}, "extended dictionary"},
        {{0x85, 0xFE, 0xD9, 0x80, 0xEA, 0x5A}, "extended dictionary"},
        {{0x87, 0x1B, 0x8C, 0x08, 0x59, 0x97}, "extended dictionary"},
        {{0x8F, 0xE6, 0x44, 0x03, 0x87, 0x90}, "extended dictionary"},
        {{0x93, 0x7A, 0x4F, 0xFF, 0x30, 0x11}, "extended dictionary"},
        {{0x97, 0x18, 0x4D, 0x13, 0x62, 0x33}, "extended dictionary"},
        {{0x97, 0xD1, 0x10, 0x1F, 0x18, 0xB0}, "extended dictionary"},
        {{0x99, 0xC6, 0x36, 0x33, 0x44, 0x33}, "extended dictionary"},
        {{0x9A, 0xFC, 0x42, 0x37, 0x2A, 0xF1}, "extended dictionary"},
        {{0x9D, 0xE8, 0x9E, 0x07, 0x02, 0x77}, "extended dictionary"},
        {{0xA0, 0x00, 0x00, 0x00, 0x00, 0x00}, "extended dictionary"},
        {{0xA0, 0x53, 0xA2, 0x92, 0xA4, 0xAF}, "extended dictionary"},
        {{0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0}, "extended dictionary"},
        {{0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1}, "extended dictionary"},
        {{0xA2, 0x7D, 0x38, 0x04, 0xC2, 0x59}, "extended dictionary"},
        {{0xA3, 0xF9, 0x74, 0x28, 0xDD, 0x01}, "extended dictionary"},
        {{0xA6, 0x45, 0x98, 0xA7, 0x74, 0x78}, "extended dictionary"},
        {{0xA8, 0x96, 0x6C, 0x7C, 0xC5, 0x4B}, "extended dictionary"},
        {{0xA9, 0x41, 0x33, 0x01, 0x34, 0x01}, "extended dictionary"},
        {{0xA9, 0xF9, 0x53, 0xDE, 0xF0, 0xA3}, "extended dictionary"},
        {{0xAA, 0xFB, 0x06, 0x04, 0x58, 0x77}, "extended dictionary"},
        {{0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56}, "extended dictionary"},
        {{0xAC, 0x0E, 0x24, 0xC7, 0x55, 0x27}, "extended dictionary"},
        {{0xAE, 0x3F, 0xF4, 0xEE, 0xA0, 0xDB}, "extended dictionary"},
        {{0xB0, 0x00, 0x00, 0x00, 0x00, 0x00}, "extended dictionary"},
        {{0xB0, 0xC9, 0xDD, 0x55, 0xDD, 0x4D}, "extended dictionary"},
        {{0xB1, 0x27, 0xC6, 0xF4, 0x14, 0x36}, "extended dictionary"},
        {{0xB5, 0xFF, 0x67, 0xCB, 0xA9, 0x51}, "extended dictionary"},
        {{0xB7, 0x36, 0x41, 0x26, 0x14, 0xAF}, "extended dictionary"},
        {{0xBD, 0x49, 0x3A, 0x39, 0x62, 0xB6}, "extended dictionary"},
        {{0xC4, 0x65, 0x2C, 0x54, 0x26, 0x1C}, "extended dictionary"},
        {{0xC6, 0xAD, 0x00, 0x25, 0x45, 0x62}, "extended dictionary"},
        {{0xC8, 0x2E, 0xC2, 0x9E, 0x32, 0x35}, "extended dictionary"},
        {{0xC9, 0x34, 0xFE, 0x34, 0xD9, 0x34}, "extended dictionary"},
        {{0xD3, 0x9B, 0xB8, 0x3F, 0x52, 0x97}, "extended dictionary"},
        {{0xD4, 0x9E, 0x28, 0x26, 0x66, 0x4F}, "extended dictionary"},
        {{0xDF, 0x27, 0xA8, 0xF1, 0xCB, 0x8E}, "extended dictionary"},
        {{0xE2, 0xC4, 0x25, 0x91, 0x36, 0x8A}, "extended dictionary"},
        {{0xE3, 0x42, 0x92, 0x81, 0xEF, 0xC1}, "extended dictionary"},
        {{0xE4, 0x44, 0xD5, 0x3D, 0x35, 0x9F}, "extended dictionary"},
        {{0xE4, 0xD2, 0x77, 0x0A, 0x89, 0xBE}, "extended dictionary"},
        {{0xEE, 0x00, 0x42, 0xF8, 0x88, 0x40}, "extended dictionary"},
        {{0xEF, 0xF6, 0x03, 0xE1, 0xEF, 0xE9}, "extended dictionary"},
        {{0xF1, 0x24, 0xC2, 0x57, 0x8A, 0xD0}, "extended dictionary"},
        {{0xF1, 0x4E, 0xE7, 0xCA, 0xE8, 0x63}, "extended dictionary"},
        {{0xF1, 0xA9, 0x73, 0x41, 0xA9, 0xFC}, "extended dictionary"},
        {{0xF1, 0xD8, 0x3F, 0x96, 0x43, 0x14}, "extended dictionary"},
        {{0xF4, 0xA9, 0xEF, 0x2A, 0xFC, 0x6D}, "extended dictionary"},
        {{0xF5, 0x9A, 0x36, 0xA2, 0x54, 0x6D}, "extended dictionary"},
        {{0xFC, 0x00, 0x01, 0x87, 0x78, 0xF7}, "extended dictionary"},
        {{0xFC, 0x00, 0x01, 0x87, 0x7B, 0xF7}, "extended dictionary"},
        {{0xFE, 0xE4, 0x70, 0xA4, 0xCB, 0x58}, "extended dictionary"},
    };
    constexpr size_t kNumDefaultKeys = sizeof(kDefaultKeys) / sizeof(kDefaultKeys[0]);

    // Extra keys loaded from an optional SD wordlist (see
    // loadWordlistFromSd() below) - tried after the built-in dictionary,
    // for every sector, on top of it.
    std::vector<std::array<uint8_t, 6>> wordlistKeys;

    Key toKey(const uint8_t* b) {
        Key k;
        memcpy(k.data(), b, 6);
        return k;
    }

    bool authenticate(uint8_t block, const Key& key, bool useKeyB) {
        return useKeyB ? nfc_a.mifareClassicAuthenticateB(block, key)
                        : nfc_a.mifareClassicAuthenticateA(block, key);
    }

    // Tries one key against `trailer`. On failure, reactivates the tag
    // (HLTA + WUPA + re-select) before returning, since a real MIFARE
    // Classic tag needs a fresh select cycle after a rejected 3-pass
    // auth before it will accept another Auth attempt at all - without
    // this, only the very first key/key-type tried per sector could ever
    // succeed, and every attempt after one wrong guess would silently
    // keep failing even if the right key came later in the dictionary.
    // Evil-M5Project's own from-scratch Crypto1 implementation for this
    // same ST25R3916 hardware does the equivalent (a full field/anti-
    // collision reset after every failed key), which is what pointed at
    // this as the likely cause of "only default-keyed sectors ever crack".
    // Returns false either way (wrong key, or the tag was lost/removed -
    // `tagLost` distinguishes the two so the caller can stop the sweep).
    bool tryKeyAndRecover(const PICC& picc, uint8_t trailer, const Key& key, bool useKeyB, bool& tagLost) {
        if (authenticate(trailer, key, useKeyB)) return true;
        if (!nfc_a.reactivate(picc)) tagLost = true;
        return false;
    }

    void logSdLine(File& f, const String& line) {
        if (f) {
            f.println(line);
            f.flush();
        }
    }

    // Parses one wordlist line into a 6-byte key. Accepts plain hex
    // ("FFFFFFFFFFFF") or hex separated by ':'/'-'/space
    // ("FF:FF:FF:FF:FF:FF"); blank lines and lines starting with '#' are
    // skipped. Returns false for anything else (malformed line, wrong
    // length) so the loader can just skip it rather than fail the load.
    bool parseHexKey(String line, uint8_t out[6]) {
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) return false;

        String hex;
        hex.reserve(12);
        for (size_t i = 0; i < line.length(); i++) {
            char c = line[i];
            if (isxdigit((unsigned char)c)) {
                hex += c;
            } else if (c != ':' && c != '-' && c != ' ') {
                return false; // unexpected character - malformed line
            }
        }
        if (hex.length() != 12) return false;

        for (int i = 0; i < 6; i++) {
            out[i] = (uint8_t)strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        }
        return true;
    }

    // Loads extra keys from NFC_WORDLIST_PATH on the SD card, if present,
    // into wordlistKeys - one call per boot, right after SD.begin()
    // succeeds. A missing file is not an error; it's the expected case
    // when someone hasn't dropped one on the card.
    void loadWordlistFromSd() {
        wordlistKeys.clear();
        if (!sdReady || !SD.exists(NFC_WORDLIST_PATH)) return;

        File f = SD.open(NFC_WORDLIST_PATH);
        if (!f) return;

        while (f.available() && wordlistKeys.size() < NFC_WORDLIST_MAX_KEYS) {
            String line = f.readStringUntil('\n');
            uint8_t key[6];
            if (parseHexKey(line, key)) {
                std::array<uint8_t, 6> k;
                memcpy(k.data(), key, 6);
                wordlistKeys.push_back(k);
            }
        }
        f.close();

        UIManager::printLine(String(wordlistKeys.size()) + " keys loaded from " + NFC_WORDLIST_PATH);
    }

    // Called once a sector's trailer has been authenticated with
    // `keyBytes`: logs the find, reads every block in the sector into
    // `f`, and (once per whole sweep) runs the write-access self-test on
    // the first ordinary block it can reach.
    void reportCracked(int s, uint8_t trailer, const char* ktName, const uint8_t* keyBytes, const char* label,
                        File& f, bool& wroteWriteTest) {
        String keyHex = RfUtils::bytesToHex(keyBytes, 6);
        UIManager::printLine("Sector " + String(s) + ": key " + String(ktName) + "=" + keyHex);
        logSdLine(f, "sector," + String(s) + ",cracked," + String(ktName) + "," + keyHex + "," + label);

        int firstBlk = (s < 32) ? s * 4 : 128 + (s - 32) * 16;
        int nBlk = (s < 32) ? 4 : 16;
        for (int b = 0; b < nBlk; b++) {
            uint8_t blockNo = (uint8_t)(firstBlk + b);
            uint8_t data[16];
            if (!nfc_a.read16(data, blockNo)) continue;

            logSdLine(f, "block," + String(blockNo) + "," + RfUtils::bytesToHex(data, 16));

            // One-time write-access self-test: write the block's own
            // bytes back unchanged, then read them again to confirm -
            // proves the write path works without ever changing tag
            // content. Skip the trailer (holds the keys/access bits) and
            // block 0 of sector 0 (hardware-locked manufacturer block on
            // genuine cards).
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

    // Sweeps every sector of a detected MIFARE Classic card against the
    // built-in dictionary plus any keys loaded from an SD wordlist,
    // dumping cracked sectors to `f` and running one write-access
    // self-test along the way.
    void sweepMifareClassic(const PICC& picc, File& f) {
        int nSectors = (int)get_sector(picc.blocks - 1) + 1;
        UIManager::printLine("MIFARE Classic (" + String(nSectors) + " sectors)");
        logSdLine(f, "type,mifare_classic,sectors," + String(nSectors));
        if (!wordlistKeys.empty()) {
            UIManager::printLine("+" + String(wordlistKeys.size()) + " keys from SD wordlist");
        }

        int cracked = 0;
        bool wroteWriteTest = false;
        bool tagLost = false;

        // NOTE: a full sweep runs to completion inside this one
        // NfcReader::loop() call, unlike every other module's loop(),
        // which returns quickly and lets main.cpp's per-tick
        // UIManager::pollInput() keep ESC responsive between calls.
        // Worst case (a fully-locked 4K card: 40 sectors x 2 key types x
        // the whole dictionary, plus any wordlist) can run well past a
        // minute and isn't interruptible mid-sweep in this first version
        // - a documented simplification, not an oversight. The status
        // bar still updates per sector so it's clear the device hasn't
        // frozen.
        for (int s = 0; s < nSectors && !tagLost; s++) {
            UIManager::setStatus("Sector " + String(s + 1) + "/" + String(nSectors) + " - trying default keys...");

            uint8_t trailer = (uint8_t)get_sector_trailer_block_from_sector((uint16_t)s);
            bool sectorCracked = false;

            for (int kt = 0; kt < 2 && !sectorCracked && !tagLost; kt++) {
                bool useKeyB = (kt == 1);
                const char* ktName = useKeyB ? "B" : "A";

                for (size_t k = 0; k < kNumDefaultKeys && !sectorCracked && !tagLost; k++) {
                    if (!tryKeyAndRecover(picc, trailer, toKey(kDefaultKeys[k].key), useKeyB, tagLost)) continue;
                    sectorCracked = true;
                    cracked++;
                    reportCracked(s, trailer, ktName, kDefaultKeys[k].key, kDefaultKeys[k].label, f, wroteWriteTest);
                }
                for (size_t k = 0; k < wordlistKeys.size() && !sectorCracked && !tagLost; k++) {
                    if (!tryKeyAndRecover(picc, trailer, toKey(wordlistKeys[k].data()), useKeyB, tagLost)) continue;
                    sectorCracked = true;
                    cracked++;
                    reportCracked(s, trailer, ktName, wordlistKeys[k].data(), "SD wordlist", f, wroteWriteTest);
                }
            }

            if (!sectorCracked && !tagLost) {
                logSdLine(f, "sector," + String(s) + ",locked");
            }
        }

        if (tagLost) {
            UIManager::printLine("[!] Lost tag mid-sweep");
            logSdLine(f, "error,lost_tag_mid_sweep");
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
        loadWordlistFromSd();
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
