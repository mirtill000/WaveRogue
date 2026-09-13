#include "lora_auditor.h"
#include "config.h"
#include "ui_manager.h"
#include "gps_logger.h"
#include "lora_inventory.h"
#include "lora_antenna_switch.h"
#include <RadioLib.h>
#include <SD.h>
#include <SPI.h>

namespace {

    // The Cap-Bus LoRa module brings its own dedicated SCK/MISO/MOSI, NOT
    // the Cardputer's internal display SPI bus - using the default global
    // `SPI` object here (implicitly on the wrong pins) is why begin()
    // used to fail with RADIOLIB_ERR_CHIP_NOT_FOUND (-2). HSPI picks a
    // different SPI peripheral than the one M5GFX already uses.
    SPIClass loraSPI(HSPI);

#if defined(WAVEROGUE_LORA_SX1262)
    SX1262 radio = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, loraSPI);
#else
    SX1276 radio = new Module(LORA_CS_PIN, LORA_DIO0_PIN, LORA_RST_PIN, LORA_DIO1_PIN, loraSPI);
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

    // Parsed view of a LoRaWAN data-frame FHDR, shared by every module that
    // needs DevAddr/FCnt/FRMPayload (the sniffer, wardriver, DevAddr
    // Mapper, NetID Extractor and Plaintext Detector all go through this).
    struct DataFrameFields {
        uint32_t devAddr;
        uint16_t fcnt;
        const uint8_t* frmPayload; // may be nullptr if there is none
        size_t frmLen;
    };

    // FHDR = DevAddr(4) + FCtrl(1) + FCnt(2) [+ FOpts(0-15)], followed by an
    // optional 1-byte FPort and then FRMPayload, with a 4-byte MIC at the
    // very end. Returns false if `len` is too short to be a valid frame.
    bool parseDataFrame(const uint8_t* data, size_t len, DataFrameFields& out) {
        if (len < 8 + 4) return false; // FHDR(min 8) + MIC(4)
        out.devAddr = readLE32(&data[1]);
        uint8_t fctrl = data[5];
        out.fcnt = readLE16(&data[6]);
        uint8_t foptsLen = fctrl & 0x0F;

        size_t fhdrEnd = 8 + foptsLen;
        size_t micStart = len - 4;
        out.frmPayload = nullptr;
        out.frmLen = 0;
        if (fhdrEnd < micStart) {
            // There's at least an FPort byte; FRMPayload (if any) follows it.
            size_t payloadStart = fhdrEnd + 1;
            if (payloadStart <= micStart) {
                out.frmPayload = &data[payloadStart];
                out.frmLen = micStart - payloadStart;
            }
        }
        return true;
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
        DataFrameFields f;
        if (!parseDataFrame(data, len, f)) {
            UIManager::printLine("[!] Malformed data frame (len=" + String(len) + ")");
            return;
        }

        char devAddrStr[9];
        snprintf(devAddrStr, sizeof(devAddrStr), "%08lX", (unsigned long)f.devAddr);

        UIManager::printLine(String(mtype));
        UIManager::printLine("DevAddr:" + String(devAddrStr) + " FCnt:" + String(f.fcnt));
        UIManager::printLine("RSSI:" + String(rssi, 0) + " SNR:" + String(snr, 1));
        // NOTE: FRMPayload (the actual application data) is only ever fed
        // into LoraInventory's entropy heuristic below, never decoded: it
        // is AES-128-CTR encrypted with the session's AppSKey, which is
        // never transmitted over the air and cannot be recovered by
        // sniffing. A LOW entropy score is what tells us it probably
        // *isn't* properly encrypted in the first place (see Module:
        // Plaintext Payload Detector).
        LoraInventory::record(f.devAddr, rssi, snr, f.frmPayload, f.frmLen);

        if (wardriveFile) {
            wardriveFile.printf("%s,%.6f,%.6f,%08lX,%u,%.1f,%.1f,%s\n",
                                 GpsLogger::timeString().c_str(),
                                 GpsLogger::latitude(), GpsLogger::longitude(),
                                 (unsigned long)f.devAddr, f.fcnt, rssi, snr, mtype);
            wardriveFile.flush();
        }
    }

} // namespace

bool LoraAuditor::begin() {
    loraSPI.begin(LORA_SPI_SCK_PIN, LORA_SPI_MISO_PIN, LORA_SPI_MOSI_PIN, LORA_CS_PIN);
    LoraAntennaSwitch::enable();

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
    UIManager::setStatus("Scanning for LoRa frames...");

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

    // Independent of whether any LoRa traffic shows up - this used to
    // only update when a packet was also received, so it could sit on
    // "no fix yet" (or nothing at all) indefinitely while actually
    // acquiring satellites in the background.
    if (GpsLogger::hasFix()) {
        UIManager::setStatus("GPS fix OK (" + String(GpsLogger::satellites()) + " sats) - listening...");
    } else {
        int sats = GpsLogger::satellites();
        UIManager::setStatus(sats > 0 ? "Acquiring GPS fix (" + String(sats) + " sats seen)..."
                                       : "Acquiring GPS satellites...");
    }

    int state = radio.receive(frameBuf, kMaxFrameLen);
    if (state == RADIOLIB_ERR_NONE) {
        size_t len = radio.getPacketLength();
        handleFrame(frameBuf, len, radio.getRSSI(), radio.getSNR());
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
    UIManager::setStatus("Listening for Join-Requests...");

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

// ------------------- DevAddr Mapper / NetID / Plaintext Detector ---------
//
// All three modules share one radio-polling routine and the LoraInventory
// backend; they only differ in how they render the table.

namespace {
    uint32_t lastInventoryRedraw = 0;

    // Receives one frame (if any) and, if it's a data frame, records it
    // into the shared inventory. Returns true iff a frame was recorded.
    bool pollInventoryOnce() {
        int state = radio.receive(frameBuf, kMaxFrameLen);
        if (state != RADIOLIB_ERR_NONE) return false;
        size_t len = radio.getPacketLength();
        if (len < 1 || isJoinRequest(frameBuf[0])) return false;

        DataFrameFields f;
        if (!parseDataFrame(frameBuf, len, f)) return false;
        LoraInventory::record(f.devAddr, radio.getRSSI(), radio.getSNR(), f.frmPayload, f.frmLen);
        return true;
    }

    void redrawDevAddrTable() {
        UIManager::clearLog();
        int n = LoraInventory::count();
        UIManager::printLine("Devices seen: " + String(n));
        for (int i = 0; i < n && i < 7; i++) {
            const auto* e = LoraInventory::entryByRecency(i);
            char buf[32];
            snprintf(buf, sizeof(buf), "%08lX x%-3lu %.0fdBm",
                     (unsigned long)e->devAddr, (unsigned long)e->seenCount, e->lastRssi);
            UIManager::printLine(String(buf));
        }
    }

    void redrawNetIdTable() {
        UIManager::clearLog();
        int n = LoraInventory::count();
        if (n == 0) {
            UIManager::printLine("No devices heard yet...");
            return;
        }
        for (int i = 0; i < n && i < 4; i++) {
            const auto* e = LoraInventory::entryByRecency(i);
            LoraInventory::NetIdGuess guess = LoraInventory::identifyNetwork(e->devAddr);
            char addrBuf[10];
            snprintf(addrBuf, sizeof(addrBuf), "%08lX:", (unsigned long)e->devAddr);
            UIManager::printLine(String(addrBuf));
            UIManager::printLine(guess.name);
        }
    }

    void redrawPlaintextTable() {
        UIManager::clearLog();
        int n = LoraInventory::count();
        int shown = 0;
        for (int i = 0; i < n; i++) {
            const auto* e = LoraInventory::entryByRecency(i);
            if (!e->flaggedPlaintext) continue;
            char buf[40];
            snprintf(buf, sizeof(buf), "%08lX H=%.1f/8 !!", (unsigned long)e->devAddr, e->lastPayloadEntropy);
            UIManager::printLine(String(buf));
            shown++;
            if (shown >= 7) break;
        }
        if (shown == 0) {
            UIManager::printLine(n == 0 ? "No devices heard yet..." : "No low-entropy payloads");
            if (n > 0) UIManager::printLine("seen so far (good sign)");
        }
    }
} // namespace

void LoraAuditor::devAddrScanBegin() {
    LoraInventory::reset();
    lastInventoryRedraw = 0;
    UIManager::printLine("Building device inventory...");
}

void LoraAuditor::devAddrScanLoop() {
    UIManager::setStatus("Scanning for LoRa devices...");
    bool got = pollInventoryOnce();
    if (got || millis() - lastInventoryRedraw > 600) {
        lastInventoryRedraw = millis();
        redrawDevAddrTable();
    }
}

void LoraAuditor::devAddrScanEnd() {}

void LoraAuditor::netIdScanBegin() {
    LoraInventory::reset();
    lastInventoryRedraw = 0;
    UIManager::printLine("Identifying networks...");
}

void LoraAuditor::netIdScanLoop() {
    UIManager::setStatus("Scanning + identifying networks...");
    bool got = pollInventoryOnce();
    if (got || millis() - lastInventoryRedraw > 800) {
        lastInventoryRedraw = millis();
        redrawNetIdTable();
    }
}

void LoraAuditor::netIdScanEnd() {}

void LoraAuditor::plaintextScanBegin() {
    LoraInventory::reset();
    lastInventoryRedraw = 0;
    UIManager::printLine("Scanning payload entropy...");
}

void LoraAuditor::plaintextScanLoop() {
    UIManager::setStatus("Scanning payload entropy...");
    bool got = pollInventoryOnce();
    if (got || millis() - lastInventoryRedraw > 800) {
        lastInventoryRedraw = millis();
        redrawPlaintextTable();
    }
}

void LoraAuditor::plaintextScanEnd() {}
