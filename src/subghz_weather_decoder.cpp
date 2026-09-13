#include "subghz_weather_decoder.h"
#include "config.h"
#include "ui_manager.h"
#include "subghz_rf_switch.h"
#include <RadioLib.h>
#include <Arduino.h>
#include <stdlib.h>
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

    // ------------------------- Nexus-style decoder -----------------------
    // Frame (36 bits, MSB first), each bit = fixed HIGH sync pulse then a
    // LOW gap whose length selects 0 or 1 (classic PWM/PPM encoding):
    //   id(8) battLow(1) still(1) channel(2) temp(12, signed /10 degC)
    //   const(4, usually 0xF) humidity(8, 0xFF if sensor has none)
    bool tryDecodeNexusAt(const uint16_t* p, size_t n, size_t start, uint64_t& outBits) {
        if (start + (size_t)NEXUS_FRAME_BITS * 2 > n) return false;
        uint64_t bits = 0;
        for (int b = 0; b < NEXUS_FRAME_BITS; b++) {
            int32_t high = p[start + b * 2];
            int32_t gap = p[start + b * 2 + 1];
            if (abs(high - NEXUS_BIT_HIGH_US) > NEXUS_GAP_TOLERANCE_US) return false;

            bool bitVal;
            if (abs(gap - NEXUS_GAP_ZERO_US) <= NEXUS_GAP_TOLERANCE_US) {
                bitVal = false;
            } else if (abs(gap - NEXUS_GAP_ONE_US) <= NEXUS_GAP_TOLERANCE_US) {
                bitVal = true;
            } else {
                return false;
            }
            bits = (bits << 1) | (bitVal ? 1ULL : 0ULL);
        }
        outBits = bits;
        return true;
    }

    struct NexusReading {
        uint8_t id;
        bool batteryLow;
        uint8_t channel;
        float tempC;
        bool hasHumidity;
        uint8_t humidity;
    };

    bool decodeNexusFields(uint64_t bits, NexusReading& out) {
        out.id = (bits >> 28) & 0xFF;
        out.batteryLow = (bits >> 27) & 0x1;
        out.channel = (bits >> 24) & 0x3;
        int16_t tempRaw = (bits >> 12) & 0xFFF;
        if (tempRaw & 0x800) tempRaw |= 0xF000; // sign-extend 12 -> 16 bits
        out.tempC = tempRaw / 10.0f;
        uint8_t constField = (bits >> 8) & 0xF;
        uint8_t hum = bits & 0xFF;
        out.hasHumidity = (constField == 0xF && hum <= 100);
        out.humidity = hum;

        // Sanity filter: reject anything wildly outside plausible sensor
        // ranges - cheap protection against a spurious/garbled decode
        // being reported as if it were real data (Nexus has no checksum).
        return out.tempC > -60.0f && out.tempC < 60.0f;
    }

    bool findAndDecodeNexus(const uint16_t* p, size_t n, NexusReading& out) {
        if (n < (size_t)NEXUS_FRAME_BITS * 2) return false;
        for (size_t start = 0; start + (size_t)NEXUS_FRAME_BITS * 2 <= n; start++) {
            uint64_t bits;
            if (tryDecodeNexusAt(p, n, start, bits) && decodeNexusFields(bits, out)) {
                return true;
            }
        }
        return false;
    }

    void redrawRawFallback(size_t count) {
        UIManager::printLine("No known protocol matched.");
        UIManager::printLine("(Nexus-style dictionary only -");
        UIManager::printLine(" TPMS needs FSK, not covered)");
        UIManager::printLine("Raw pulses: " + String(count));
        String widths;
        for (size_t i = 0; i < count && i < 6; i++) widths += String(pulses[i]) + " ";
        UIManager::printLine(widths);
    }
}

bool SubGhzWeatherDecoder::begin() {
    subghzSPI.begin(SUBGHZ_SPI_SCK_PIN, SUBGHZ_SPI_MISO_PIN, SUBGHZ_SPI_MOSI_PIN, SUBGHZ_CS_PIN);
    SubGhzRfSwitch::selectForFrequency(SUBGHZ_FREQ_MHZ);

    int state = radio.begin(SUBGHZ_FREQ_MHZ, 4.8f, 48.0f, 135.0f, 10, 16);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("CC1101 init failed, code " + String(state));
        return false;
    }
    radio.setOOK(true);
    UIManager::printLine("Listening for weather/temp");
    UIManager::printLine("sensors (Nexus-style dict).");
    startCapture();
    return true;
}

void SubGhzWeatherDecoder::loop() {
    static uint32_t lastDump = 0;
    if (millis() - lastDump < 500) return;
    lastDump = millis();

    noInterrupts();
    size_t count = pulseCount;
    interrupts();

    if (count < (size_t)NEXUS_FRAME_BITS * 2) return; // not enough to be a full frame yet

    UIManager::clearLog();
    NexusReading reading;
    if (findAndDecodeNexus((const uint16_t*)pulses, count, reading)) {
        UIManager::printLine("Nexus-style sensor found!");
        UIManager::printLine("ID:0x" + String(reading.id, HEX) + " Ch:" + String(reading.channel + 1));
        UIManager::printLine("Temp: " + String(reading.tempC, 1) + " C");
        if (reading.hasHumidity) {
            UIManager::printLine("Humidity: " + String(reading.humidity) + "%");
        }
        UIManager::printLine(reading.batteryLow ? "Battery: LOW" : "Battery: OK");
    } else {
        redrawRawFallback(count);
    }

    noInterrupts();
    pulseCount = 0;
    interrupts();
}

void SubGhzWeatherDecoder::end() {
    stopCapture();
}
