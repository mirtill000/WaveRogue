#include "subghz_wmbus_scanner.h"
#include "config.h"
#include "ui_manager.h"
#include "subghz_rf_switch.h"
#include <RadioLib.h>
#include <Arduino.h>
#include <SPI.h>

namespace {
    SPIClass subghzSPI(HSPI);
    CC1101 radio = new Module(SUBGHZ_CS_PIN, SUBGHZ_GDO0_PIN, RADIOLIB_NC, RADIOLIB_NC, subghzSPI);

    volatile uint16_t pulses[SUBGHZ_MAX_PULSES];
    volatile size_t pulseCount = 0;
    volatile uint32_t lastEdgeMicros = 0;
    volatile bool capturing = false;

    void IRAM_ATTR onEdge() {
        uint32_t now = micros();
        uint32_t delta = now - lastEdgeMicros;
        lastEdgeMicros = now;
        if (!capturing) return;
        if (pulseCount < SUBGHZ_MAX_PULSES) {
            pulses[pulseCount++] = (delta > 0xFFFF) ? 0xFFFF : (uint16_t)delta;
        }
    }

    void startCapture() {
        pulseCount = 0;
        lastEdgeMicros = micros();
        capturing = true;
        radio.receiveDirect(); // async serial mode: raw demodulated bit on GDO0, works for FSK too
        pinMode(SUBGHZ_GDO0_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN), onEdge, CHANGE);
    }

    void stopCapture() {
        capturing = false;
        detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
        radio.standby();
    }

    constexpr size_t kMaxBits = 512;
    constexpr uint16_t kSync = 0x543D;
    constexpr uint16_t kSyncInv = (uint16_t)(~kSync) & 0xFFFF;

    // Manchester-decodes raw edge timing into unpacked bits (one uint8_t
    // 0/1 per bit) using the standard "2 short pulses OR 1 long pulse per
    // bit" grouping. We keep bits unpacked (not bytes) so the sync search
    // below can align on ANY bit offset, not just byte boundaries - we
    // have no a priori guarantee our capture started byte-aligned with
    // the transmitter's framing.
    size_t manchesterExtractBits(const uint16_t* p, size_t n, uint8_t* outBits, size_t maxBits) {
        size_t bitCount = 0;
        bool level = false;
        const uint16_t shortMax = WMBUS_MANCHESTER_UNIT_US + WMBUS_MANCHESTER_TOLERANCE_US;
        const uint16_t longMin = 2 * WMBUS_MANCHESTER_UNIT_US - WMBUS_MANCHESTER_TOLERANCE_US;
        const uint16_t longMax = 2 * WMBUS_MANCHESTER_UNIT_US + WMBUS_MANCHESTER_TOLERANCE_US;

        size_t i = 0;
        while (i < n && bitCount < maxBits) {
            uint16_t d = p[i];
            if (d <= shortMax) {
                if (i + 1 < n && p[i + 1] <= shortMax) {
                    outBits[bitCount++] = level ? 1 : 0;
                    level = !level;
                    i += 2;
                } else {
                    break;
                }
            } else if (d >= longMin && d <= longMax) {
                outBits[bitCount++] = level ? 1 : 0;
                level = !level;
                i += 1;
            } else {
                break; // pulse doesn't fit either bucket - stop here
            }
        }
        return bitCount;
    }

    // Finds the 16-bit sync word (0x543D) at any bit offset. Since we
    // don't know the capture's absolute polarity, we also accept its
    // bitwise complement and remember to invert everything after it.
    bool findSync(const uint8_t* bits, size_t bitCount, size_t& outStartBit, bool& outInvert) {
        for (size_t i = 0; i + 16 <= bitCount; i++) {
            uint16_t word = 0;
            for (int b = 0; b < 16; b++) word = (word << 1) | bits[i + b];
            if (word == kSync) { outStartBit = i + 16; outInvert = false; return true; }
            if (word == kSyncInv) { outStartBit = i + 16; outInvert = true; return true; }
        }
        return false;
    }

    size_t packBytes(const uint8_t* bits, size_t bitCount, size_t startBit, bool invert,
                      uint8_t* out, size_t maxBytes) {
        size_t avail = (bitCount > startBit) ? (bitCount - startBit) / 8 : 0;
        size_t n = min(avail, maxBytes);
        for (size_t byteIdx = 0; byteIdx < n; byteIdx++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) {
                uint8_t bit = bits[startBit + byteIdx * 8 + b];
                if (invert) bit ^= 1;
                v = (uint8_t)((v << 1) | bit);
            }
            out[byteIdx] = v;
        }
        return n;
    }

    // EN 13757-3 manufacturer code: 3 letters packed 5 bits each into a
    // 16-bit field, value 1-26 => 'A'-'Z'.
    String decodeManufacturer(uint16_t m, bool& plausible) {
        char c1 = ((m >> 10) & 0x1F) + 'A' - 1;
        char c2 = ((m >> 5) & 0x1F) + 'A' - 1;
        char c3 = (m & 0x1F) + 'A' - 1;
        plausible = c1 >= 'A' && c1 <= 'Z' && c2 >= 'A' && c2 <= 'Z' && c3 >= 'A' && c3 <= 'Z';
        char buf[4] = {c1, c2, c3, 0};
        return String(buf);
    }

    const char* deviceTypeName(uint8_t type) {
        switch (type) {
            case 0x02: return "Electricity";
            case 0x03: return "Gas";
            case 0x04: return "Heat";
            case 0x06: return "Warm water";
            case 0x07: return "Water";
            case 0x08: return "Heat cost allocator";
            case 0x0C: return "Heat/cooling load meter";
            case 0x15: return "Cold water";
            case 0x16: return "Dual water";
            default:   return "Unknown/other";
        }
    }

    // Best-effort encryption-mode read from the CI-field's "short header"
    // config word. See the .h file's honesty note - this is not a
    // spec-verified CRC-checked parse, just a plausibility-gated one.
    String describeEncryption(uint8_t ci, const uint8_t* rest, size_t restLen) {
        char ciBuf[8];
        snprintf(ciBuf, sizeof(ciBuf), "0x%02X", ci);
        if (ci == 0x78) {
            return String("CI ") + ciBuf + ": plain APL, NO ENCRYPTION";
        }
        if ((ci == 0x7A || ci == 0x8A) && restLen >= 4) {
            uint8_t configHi = rest[3];
            uint8_t mode = configHi & 0x1F;
            if (mode == 0) return String("CI ") + ciBuf + ": mode 0 - NOT ENCRYPTED";
            char buf[40];
            snprintf(buf, sizeof(buf), "CI %s: AES mode %u (encrypted)", ciBuf, mode);
            return String(buf);
        }
        return String("CI ") + ciBuf + " (format not in lookup)";
    }
}

bool SubGhzWmbusScanner::begin() {
    // wM-Bus S-mode is 2-FSK at a 65.536 kbps chip rate (2x the 32.768
    // kbps Manchester-encoded bit rate), ~50 kHz deviation. We leave OOK
    // disabled (default FSK) - receiveDirect() gives us the raw
    // demodulated bitstream on GDO0 either way.
    subghzSPI.begin(SUBGHZ_SPI_SCK_PIN, SUBGHZ_SPI_MISO_PIN, SUBGHZ_SPI_MOSI_PIN, SUBGHZ_CS_PIN);
    SubGhzRfSwitch::selectForFrequency(WMBUS_SMODE_FREQ_MHZ);

    int state = radio.begin(WMBUS_SMODE_FREQ_MHZ, 65.536f, 50.0f, 200.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    UIManager::printLine("Scanning for wM-Bus S-mode");
    UIManager::printLine(String(WMBUS_SMODE_FREQ_MHZ, 2) + "MHz meters...");
    UIManager::printLine("(T-mode meters not covered)");
    startCapture();
    return true;
}

void SubGhzWmbusScanner::loop() {
    UIManager::setStatus("Scanning " + String(WMBUS_SMODE_FREQ_MHZ, 2) + "MHz for meters...");

    static uint32_t lastDump = 0;
    if (millis() - lastDump < 500) return;
    lastDump = millis();

    noInterrupts();
    size_t count = pulseCount;
    interrupts();
    if (count < 64) return; // not enough edges for a full header yet

    static uint8_t bitBuf[kMaxBits];
    size_t bitCount = manchesterExtractBits((const uint16_t*)pulses, count, bitBuf, kMaxBits);

    UIManager::clearLog();
    size_t startBit;
    bool invert;
    if (bitCount >= 32 && findSync(bitBuf, bitCount, startBit, invert)) {
        uint8_t frame[24];
        size_t n = packBytes(bitBuf, bitCount, startBit, invert, frame, sizeof(frame));

        if (n >= 10) {
            uint8_t lField = frame[0];
            uint16_t mField = frame[2] | ((uint16_t)frame[3] << 8);
            bool mfrPlausible;
            String mfr = decodeManufacturer(mField, mfrPlausible);

            if (mfrPlausible && lField >= WMBUS_MIN_L_FIELD && lField <= WMBUS_MAX_L_FIELD) {
                uint8_t devType = frame[9];
                uint8_t ci = (n >= 11) ? frame[10] : 0;
                UIManager::printLine("wM-Bus meter found!");
                UIManager::printLine("Mfr:" + mfr + " Type:" + String(deviceTypeName(devType)));
                char serial[16];
                snprintf(serial, sizeof(serial), "SN:%02X%02X%02X%02X", frame[7], frame[6], frame[5], frame[4]);
                // NOTE: A-field serial (frame[4..7], little-endian) is BCD,
                // printed here as raw hex nibbles - convert to decimal BCD
                // yourself if needed.
                UIManager::printLine(String(serial));
                UIManager::printLine("L:" + String(lField));
                if (n >= 15) {
                    UIManager::printLine(describeEncryption(ci, &frame[11], n - 11));
                } else {
                    UIManager::printLine("CI:0x" + String(ci, HEX) + " (short capture)");
                }
                noInterrupts();
                pulseCount = 0;
                interrupts();
                return;
            }
        }
    }

    UIManager::printLine("No wM-Bus sync found yet.");
    UIManager::printLine("Raw edges: " + String(count));
    noInterrupts();
    pulseCount = 0;
    interrupts();
}

void SubGhzWmbusScanner::end() {
    stopCapture();
}
