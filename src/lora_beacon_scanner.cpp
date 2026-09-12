#include "lora_beacon_scanner.h"
#include "config.h"
#include "ui_manager.h"
#include <RadioLib.h>

namespace {
    // Own radio handle (same physical chip/pins as lora_auditor.cpp's, but
    // only one module is ever active at a time so a second RadioLib driver
    // instance targeting the same SPI/CS is safe here).
#if defined(WAVEROGUE_LORA_SX1262)
    SX1262 beaconRadio = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
#else
    SX1276 beaconRadio = new Module(LORA_CS_PIN, LORA_DIO0_PIN, LORA_RST_PIN, LORA_DIO1_PIN);
#endif

    constexpr size_t kMaxBeaconLen = 32;
    uint8_t beaconBuf[kMaxBeaconLen];
    uint32_t lastBeaconMillis = 0;
    int beaconsSeen = 0;

    uint32_t readLE32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    int32_t read24Signed(const uint8_t* p) {
        int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000) v |= 0xFF000000; // sign-extend 24 -> 32 bits
        return v;
    }
}

bool LoraBeaconScanner::begin() {
    int state = beaconRadio.begin(LORA_BEACON_FREQ_MHZ, LORA_BEACON_BW_KHZ, LORA_BEACON_SF,
                                   LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM);
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("Beacon radio init failed: " + String(state));
        return false;
    }
    beaconRadio.setCRC(true);
    beaconsSeen = 0;
    lastBeaconMillis = 0;

    UIManager::printLine("Listening for Class-B beacons");
    UIManager::printLine(String(LORA_BEACON_FREQ_MHZ, 3) + "MHz SF" + String(LORA_BEACON_SF));
    UIManager::printLine("Expect ~1 every " + String(LORA_BEACON_PERIOD_S) + "s if a");
    UIManager::printLine("Class-B gateway is nearby.");
    return true;
}

void LoraBeaconScanner::loop() {
    int state = beaconRadio.receive(beaconBuf, kMaxBeaconLen);
    if (state != RADIOLIB_ERR_NONE) return; // timeout: nothing heard, keep waiting

    size_t len = beaconRadio.getPacketLength();
    beaconsSeen++;
    uint32_t now = millis();
    uint32_t sinceLast = lastBeaconMillis ? (now - lastBeaconMillis) : 0;
    lastBeaconMillis = now;

    UIManager::clearLog();
    UIManager::printLine("Beacon #" + String(beaconsSeen) + " (len=" + String(len) + ")");
    if (sinceLast > 0) {
        UIManager::printLine("Interval: " + String(sinceLast / 1000.0f, 1) + "s (want ~" +
                              String(LORA_BEACON_PERIOD_S) + "s)");
    }
    UIManager::printLine("RSSI:" + String(beaconRadio.getRSSI(), 0) +
                          " SNR:" + String(beaconRadio.getSNR(), 1));

    // Best-effort payload decode - see the header comment: getting a
    // beacon-shaped frame at the right cadence already proves the gateway
    // is there, regardless of whether this exact layout matches.
    if (len >= LORA_BEACON_EXPECTED_LEN) {
        uint32_t epochTime = readLE32(&beaconBuf[2]);
        uint8_t infoDesc = beaconBuf[8];
        UIManager::printLine("GW time (epoch): " + String(epochTime));
        if (infoDesc == 0 || infoDesc == 1) {
            // InfoDesc 0/1 conventionally mean "next 6 bytes are GW lat/lon".
            float lat = read24Signed(&beaconBuf[9]) / 8388608.0f * 90.0f;   // /2^23 * 90 deg
            float lon = read24Signed(&beaconBuf[12]) / 8388608.0f * 180.0f; // /2^23 * 180 deg
            UIManager::printLine("GW coords: " + String(lat, 4) + "," + String(lon, 4));
        } else {
            UIManager::printLine("InfoDesc=" + String(infoDesc) + " (no GPS in beacon)");
        }
    } else {
        UIManager::printLine("Unexpected length - GW present,");
        UIManager::printLine("but beacon layout differs from");
        UIManager::printLine("the EU868 default assumed here.");
    }
}

void LoraBeaconScanner::end() {
    beaconRadio.standby();
}
