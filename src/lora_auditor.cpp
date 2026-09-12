#include "lora_auditor.h"
#include "config.h"
#include "ui_manager.h"
#include "gps_logger.h"
#include <RadioLib.h>
#include <SD.h>
#include <SPI.h>

namespace {

#if defined(WAVEROGUE_LORA_SX1262)
    SX1262 radio = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
#else
    SX1276 radio = new Module(LORA_CS_PIN, LORA_DIO0_PIN, LORA_RST_PIN, LORA_DIO1_PIN);
#endif

    constexpr size_t kMaxFrameLen = 255;
    uint8_t frameBuf[kMaxFrameLen];

    File wardriveFile;
    File rogueGwFile;

    // -------------------------------------------------------------------
    // LoRaWAN MHDR MType field (bits 7-5 of the first byte). See LoRaWAN
    // L2 1.0.x specification, section 4.2. This is the ONLY part of the
    // frame we need for classification - everything else in FRMPayload
    // stays opaque ciphertext to us.
    // -------------------------------------------------------------------
    const char* mtypeToString(uint8_t mhdr) {
        switch ((mhdr >> 5) & 0x07) {
            case 0b000: return "Join Request";
            case 0b001: return "Join Accept";
            case 0b010: return "Unconfirmed Data Up";
            case 0b011: return "Unconfirmed Data Down";
            case 0b100: return "Confirmed Data Up";
            case 0b101: return "Confirmed Data Down";
            case 0b110: return "RFU";
            default:    return "Proprietary";
        }
    }

    bool isJoinRequest(uint8_t mhdr) {
        return ((mhdr >> 5) & 0x07) == 0b000;
    }

    uint32_t readLE32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    uint16_t readLE16(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }

    String eui64ToHex(const uint8_t* p) {
        // EUI-64 fields are transmitted little-endian on air; print
        // most-significant-byte-first for readability, as vendors do.
        char buf[24];
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0]);
        return String(buf);
    }

    // Decodes one raw LoRa PHY frame as a LoRaWAN frame and prints/logs it.
    // `logGeo` / `logRogue` select which (if any) CSV sink also receives it.
    void handleFrame(const uint8_t* data, size_t len, float rssi, float snr) {
        if (len < 1) return;
        uint8_t mhdr = data[0];
        const char* mtype = mtypeToString(mhdr);

        if (isJoinRequest(mhdr)) {
            // MHDR(1) + AppEUI(8) + DevEUI(8) + DevNonce(2) + MIC(4) = 23 bytes
            if (len < 23) {
                UIManager::printLine("[!] Malformed Join-Request (len=" + String(len) + ")");
                return;
            }
            String appEui = eui64ToHex(&data[1]);
            String devEui = eui64ToHex(&data[9]);
            uint16_t devNonce = readLE16(&data[17]);

            UIManager::printLine(String(mtype));
            UIManager::printLine("AppEUI:" + appEui);
            UIManager::printLine("DevEUI:" + devEui);
            UIManager::printLine("Nonce:" + String(devNonce) + " RSSI:" + String(rssi, 0));

            if (rogueGwFile) {
                rogueGwFile.printf("%lu,%s,%s,%u,%.1f,%.1f\n",
                                   (unsigned long)millis(), appEui.c_str(), devEui.c_str(),
                                   devNonce, rssi, snr);
                rogueGwFile.flush();
            }
            return;
        }

        // Data frame (uplink/downlink): FHDR starts right after MHDR.
        // FHDR = DevAddr(4) + FCtrl(1) + FCnt(2) [+ FOpts(0-15)]
        if (len < 8) {
            UIManager::printLine("[!] Malformed data frame (len=" + String(len) + ")");
            return;
        }
        uint32_t devAddr = readLE32(&data[1]);
        uint8_t fctrl = data[5];
        uint16_t fcnt = readLE16(&data[6]);
        uint8_t foptsLen = fctrl & 0x0F;
        (void)foptsLen; // parsed but not displayed - present for further extension

        char devAddrStr[9];
        snprintf(devAddrStr, sizeof(devAddrStr), "%08lX", (unsigned long)devAddr);

        UIManager::printLine(String(mtype));
        UIManager::printLine("DevAddr:" + String(devAddrStr) + " FCnt:" + String(fcnt));
        UIManager::printLine("RSSI:" + String(rssi, 0) + " SNR:" + String(snr, 1));
        // NOTE: FRMPayload (the actual application data) begins after FHDR
        // (+FPort). We deliberately do NOT attempt to touch it: it is
        // AES-128-CTR encrypted with the session's AppSKey, which is never
        // transmitted over the air and cannot be recovered by sniffing.

        if (wardriveFile) {
            wardriveFile.printf("%s,%.6f,%.6f,%08lX,%u,%.1f,%.1f,%s\n",
                                 GpsLogger::timeString().c_str(),
                                 GpsLogger::latitude(), GpsLogger::longitude(),
                                 (unsigned long)devAddr, fcnt, rssi, snr, mtype);
            wardriveFile.flush();
        }
    }

} // namespace

bool LoraAuditor::begin() {
    int state;
#if defined(WAVEROGUE_LORA_SX1262)
    state = radio.begin(LORA_FREQ_MHZ, LORA_BANDWIDTH_KHZ, LORA_SPREADING_FACTOR,
                         LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM);
#else
    state = radio.begin(LORA_FREQ_MHZ, LORA_BANDWIDTH_KHZ, LORA_SPREADING_FACTOR,
                         LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM);
#endif
    if (state != RADIOLIB_ERR_NONE) {
        UIManager::printLine("Radio init failed, code " + String(state));
        return false;
    }
    radio.setCRC(true);
    return true;
}

void LoraAuditor::sniffLoop() {
    // Blocking receive with a short timeout so the caller's key-poll loop
    // (checking for ESC/back) still gets serviced regularly.
    int state = radio.receive(frameBuf, kMaxFrameLen);
    if (state == RADIOLIB_ERR_NONE) {
        size_t len = radio.getPacketLength();
        float rssi = radio.getRSSI();
        float snr = radio.getSNR();
        handleFrame(frameBuf, len, rssi, snr);
    }
    // RADIOLIB_ERR_RX_TIMEOUT is expected when nothing was heard; ignore it.
}

void LoraAuditor::wardriveBegin() {
    GpsLogger::begin();
    if (!SD.begin(SD_CS_PIN)) {
        UIManager::printLine("[!] SD card init failed");
        return;
    }
    bool isNew = !SD.exists(WARDRIVE_LOG_PATH);
    wardriveFile = SD.open(WARDRIVE_LOG_PATH, FILE_APPEND);
    if (wardriveFile && isNew) {
        wardriveFile.println("utc_time,lat,lon,dev_addr,fcnt,rssi,snr,mtype");
    }
    UIManager::printLine(wardriveFile ? "Logging to SD..." : "[!] Could not open log file");
}

void LoraAuditor::wardriveLoop() {
    GpsLogger::update();
    int state = radio.receive(frameBuf, kMaxFrameLen);
    if (state == RADIOLIB_ERR_NONE) {
        size_t len = radio.getPacketLength();
        handleFrame(frameBuf, len, radio.getRSSI(), radio.getSNR());
        UIManager::printLine(GpsLogger::hasFix() ? "GPS: fix OK" : "GPS: no fix yet");
    }
}

void LoraAuditor::wardriveEnd() {
    if (wardriveFile) wardriveFile.close();
}

void LoraAuditor::rogueGatewayBegin() {
    bool sdOk = SD.begin(SD_CS_PIN);
    if (sdOk) {
        bool isNew = !SD.exists(ROGUE_GW_LOG_PATH);
        rogueGwFile = SD.open(ROGUE_GW_LOG_PATH, FILE_APPEND);
        if (rogueGwFile && isNew) {
            rogueGwFile.println("millis,app_eui,dev_eui,dev_nonce,rssi,snr");
        }
    }
    UIManager::printLine("Listening for Join-Requests...");
    UIManager::printLine("(Educational PoC: no real");
    UIManager::printLine(" Join-Accept is sent - we");
    UIManager::printLine(" don't have the AppKey.)");
}

void LoraAuditor::rogueGatewayLoop() {
    int state = radio.receive(frameBuf, kMaxFrameLen);
    if (state == RADIOLIB_ERR_NONE) {
        size_t len = radio.getPacketLength();
        if (len >= 1 && isJoinRequest(frameBuf[0])) {
            handleFrame(frameBuf, len, radio.getRSSI(), radio.getSNR());

            // -----------------------------------------------------------
            // Deliberately NOT implemented: transmitting a spoofed
            // Join-Accept. A real Join-Accept's payload (AppNonce,
            // NetID, DevAddr, DLSettings, RxDelay, CFList) is encrypted
            // AND integrity-protected with the device's real AppKey,
            // which a passive listener never has. Broadcasting a bogus,
            // improperly-encrypted frame would not be accepted by any
            // spec-compliant end node - it would, at best, let you
            // observe the node's join-retry/backoff behavior, which is
            // itself a valid (and legal, on your own hardware) thing to
            // audit. If you want to experiment with that, do it only
            // against a device you own, and be aware most stacks
            // (e.g. LoRaMAC-node) will simply discard an unverifiable
            // Join-Accept and retry with backoff.
            // -----------------------------------------------------------
        }
    }
}

void LoraAuditor::rogueGatewayEnd() {
    if (rogueGwFile) rogueGwFile.close();
}
