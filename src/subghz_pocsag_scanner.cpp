#include "subghz_pocsag_scanner.h"
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
        radio.receiveDirect();
        pinMode(SUBGHZ_GDO0_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN), onEdge, CHANGE);
    }

    void stopCapture() {
        capturing = false;
        detachInterrupt(digitalPinToInterrupt(SUBGHZ_GDO0_PIN));
        radio.standby();
    }

    constexpr size_t kMaxBits = 2048;
    constexpr uint32_t kIdleCodeword = 0x7A89C197UL;

    // NRZ bit recovery: since POCSAG has no guaranteed per-bit transition
    // (unlike Manchester), a long run of identical bits shows up as ONE
    // long gap with no edge. We recover it by rounding that gap's
    // duration to the nearest whole number of bit periods and emitting
    // that many repeated bits - the standard technique for edge-timed
    // NRZ recovery.
    size_t nrzExtractBits(const uint16_t* p, size_t n, uint8_t* outBits, size_t maxBits, uint32_t bitPeriodUs) {
        size_t bitCount = 0;
        bool level = false;
        for (size_t i = 0; i < n && bitCount < maxBits; i++) {
            uint32_t d = p[i];
            int nBits = (int)((d + bitPeriodUs / 2) / bitPeriodUs);
            if (nBits < 1) nBits = 1;
            if (nBits > 32) nBits = 32; // clamp against a stray huge gap (e.g. start of capture)
            for (int b = 0; b < nBits && bitCount < maxBits; b++) outBits[bitCount++] = level ? 1 : 0;
            level = !level;
        }
        return bitCount;
    }

    bool findSync32(const uint8_t* bits, size_t bitCount, size_t& outStartBit, bool& outInvert) {
        for (size_t i = 0; i + 32 <= bitCount; i++) {
            uint32_t word = 0;
            for (int b = 0; b < 32; b++) word = (word << 1) | bits[i + b];
            if (word == POCSAG_FRAME_SYNC_CODEWORD) { outStartBit = i + 32; outInvert = false; return true; }
            if (word == (~POCSAG_FRAME_SYNC_CODEWORD)) { outStartBit = i + 32; outInvert = true; return true; }
        }
        return false;
    }

    uint32_t readCodeword(const uint8_t* bits, size_t start, bool invert) {
        uint32_t word = 0;
        for (int b = 0; b < 32; b++) {
            uint8_t bit = bits[start + b];
            if (invert) bit ^= 1;
            word = (word << 1) | bit;
        }
        return word;
    }

    // POCSAG numeric alphabet (matches the common multimon-ng convention;
    // digits 0-9 are spec-guaranteed, the punctuation codes are
    // conventional rather than independently spec-verified here).
    char numericDigit(uint8_t nibble) {
        static const char table[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                        '8', '9', ' ', 'U', '-', ']', '[', ' '};
        return table[nibble & 0x0F];
    }

    void decodeAndPrint(const uint8_t* bits, size_t bitCount, size_t startBit, bool invert) {
        UIManager::clearLog();
        UIManager::printLine("POCSAG sync found!");

        String message;
        int shownLines = 0;
        size_t pos = startBit;
        while (pos + 32 <= bitCount && shownLines < 6) {
            uint32_t cw = readCodeword(bits, pos, invert);
            pos += 32;

            if (cw == kIdleCodeword) continue;

            bool flag = (cw >> 31) & 1;
            uint32_t data20 = (cw >> 11) & 0xFFFFF;

            if (!flag) {
                // Address codeword. NOTE: the full 21-bit RIC also needs
                // the 3-bit batch frame-position OR'd into the low bits -
                // not tracked here, so this is a partial address only.
                uint32_t addrPartial = data20 >> 2;
                uint8_t func = data20 & 0x3;
                if (!message.isEmpty()) {
                    UIManager::printLine("Msg: " + message);
                    shownLines++;
                    message = "";
                }
                UIManager::printLine("Addr(partial)=0x" + String(addrPartial, HEX) + " f=" + String(func));
                shownLines++;
            } else {
                for (int n = 4; n >= 0; n--) {
                    message += numericDigit((data20 >> (n * 4)) & 0xF);
                }
            }
        }
        if (!message.isEmpty()) {
            UIManager::printLine("Msg: " + message);
        }
    }
}

bool SubGhzPocsagScanner::begin() {
    // Standard POCSAG deviation is +/-4.5 kHz regardless of baud rate.
    subghzSPI.begin(SUBGHZ_SPI_SCK_PIN, SUBGHZ_SPI_MISO_PIN, SUBGHZ_SPI_MOSI_PIN, SUBGHZ_CS_PIN);
    SubGhzRfSwitch::selectForFrequency(POCSAG_FREQ_MHZ);

    int state = radio.begin(POCSAG_FREQ_MHZ, (float)POCSAG_BAUD / 1000.0f, 4.5f, 50.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    UIManager::printLine("Listening for POCSAG on");
    UIManager::printLine(String(POCSAG_FREQ_MHZ, 3) + "MHz @ " + String(POCSAG_BAUD) + "bps");
    UIManager::printLine("(FLEX not decoded - freq");
    UIManager::printLine(" must be authorized to test)");
    startCapture();
    return true;
}

void SubGhzPocsagScanner::loop() {
    static uint32_t lastDump = 0;
    if (millis() - lastDump < 600) return;
    lastDump = millis();

    noInterrupts();
    size_t count = pulseCount;
    interrupts();
    if (count < 64) return;

    uint32_t bitPeriodUs = 1000000UL / (uint32_t)POCSAG_BAUD;
    static uint8_t bitBuf[kMaxBits];
    size_t bitCount = nrzExtractBits((const uint16_t*)pulses, count, bitBuf, kMaxBits, bitPeriodUs);

    size_t startBit;
    bool invert;
    if (bitCount >= 32 && findSync32(bitBuf, bitCount, startBit, invert)) {
        decodeAndPrint(bitBuf, bitCount, startBit, invert);
    } else {
        UIManager::clearLog();
        UIManager::printLine("No POCSAG sync yet.");
        UIManager::printLine("Raw edges: " + String(count));
    }

    noInterrupts();
    pulseCount = 0;
    interrupts();
}

void SubGhzPocsagScanner::end() {
    stopCapture();
}
