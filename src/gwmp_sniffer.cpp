#include "gwmp_sniffer.h"
#include "config.h"
#include "ui_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <string.h>

namespace {
    // Single-slot handoff from the Wi-Fi driver's callback context to the
    // main loop(): the callback only ever copies raw bytes (cheap, no
    // allocation), and all JSON parsing/UI drawing happens in loop().
    constexpr int kMaxSemtechLen = 220;
    volatile bool pendingCapture = false;
    uint8_t capturedBuf[kMaxSemtechLen];
    volatile int capturedLen = 0;

    uint8_t currentChannel = 1;
    uint32_t lastHopMillis = 0;
    uint32_t packetsMatched = 0;

    void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
        if (type != WIFI_PKT_DATA) return;
        if (pendingCapture) return; // previous capture not yet drained

        auto* pkt = reinterpret_cast<wifi_promiscuous_pkt_t*>(buf);
        const uint8_t* p = pkt->payload;
        int len = pkt->rx_ctrl.sig_len;
        if (len < 24 + 8 + 20 + 8 + 12) return; // too short to possibly matter

        uint8_t frameType = (p[0] >> 2) & 0x03;
        uint8_t frameSubtype = (p[0] >> 4) & 0x0F;
        if (frameType != 2) return; // only interested in 802.11 Data frames

        int hdrLen = 24;
        if (frameSubtype & 0x08) hdrLen += 2; // QoS control field present
        if (len < hdrLen + 8) return;

        // LLC/SNAP header: AA AA 03 00:00:00 <EtherType 2 bytes>.
        const uint8_t* llc = p + hdrLen;
        if (!(llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03)) return;
        uint16_t etherType = ((uint16_t)llc[6] << 8) | llc[7];
        if (etherType != 0x0800) return; // not IPv4

        const uint8_t* ip = llc + 8;
        if (((ip[0] >> 4) & 0x0F) != 4) return; // not IPv4
        uint8_t ihl = (ip[0] & 0x0F) * 4;
        uint8_t proto = ip[9];
        if (proto != 17) return; // not UDP

        const uint8_t* udp = ip + ihl;
        if (udp + 8 > p + len) return;
        uint16_t dstPort = ((uint16_t)udp[2] << 8) | udp[3];
        if (dstPort != GWMP_UDP_PORT) return;

        const uint8_t* semtech = udp + 8;
        int semtechLen = len - (int)(semtech - p);
        // Semtech header: version(1) + token(2) + identifier(1) + gwEUI(8).
        // identifier 0x00 = PUSH_DATA, the message that carries "stat" JSON.
        if (semtechLen < 12 || semtech[3] != 0x00) return;

        int copyLen = (semtechLen < kMaxSemtechLen) ? semtechLen : kMaxSemtechLen;
        memcpy(capturedBuf, semtech, copyLen);
        capturedLen = copyLen;
        pendingCapture = true;
    }
}

bool GwmpSniffer::begin() {
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&promiscuousCallback);

    currentChannel = (GWMP_WIFI_CHANNEL != 0) ? GWMP_WIFI_CHANNEL : 1;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    lastHopMillis = millis();
    pendingCapture = false;
    packetsMatched = 0;

    UIManager::printLine("Wi-Fi promiscuous capture on");
    UIManager::printLine(GWMP_WIFI_CHANNEL == 0
                              ? ("Hopping ch 1-13...")
                              : ("Fixed channel " + String(GWMP_WIFI_CHANNEL)));
    UIManager::printLine("Watching for cleartext GWMP");
    UIManager::printLine("(UDP/" + String(GWMP_UDP_PORT) + ") stat packets.");
    UIManager::printLine("Encrypted Wi-Fi backhaul or");
    UIManager::printLine("Basic Station/TLS: nothing here.");
    return true;
}

void GwmpSniffer::loop() {
    if (GWMP_WIFI_CHANNEL == 0 && millis() - lastHopMillis > GWMP_CHANNEL_HOP_MS) {
        lastHopMillis = millis();
        currentChannel = (currentChannel % 13) + 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }

    if (!pendingCapture) return;

    uint8_t buf[kMaxSemtechLen];
    int len = capturedLen;
    memcpy(buf, capturedBuf, len);
    pendingCapture = false;
    packetsMatched++;

    UIManager::clearLog();
    char eui[24];
    snprintf(eui, sizeof(eui), "%02X%02X%02X%02X%02X%02X%02X%02X",
             buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11]);
    UIManager::printLine("GW EUI: " + String(eui));

    if (len <= 12) {
        UIManager::printLine("(PUSH_DATA had no JSON body)");
        return;
    }

    JsonDocument doc; // ArduinoJson v7: dynamically-sized, no capacity template needed
    DeserializationError err = deserializeJson(doc, buf + 12, len - 12);
    if (err) {
        UIManager::printLine("[!] JSON parse failed: " + String(err.c_str()));
        return;
    }

    JsonObject stat = doc["stat"];
    if (stat.isNull()) {
        UIManager::printLine("(no 'stat' object in this packet)");
        return;
    }

    if (stat.containsKey("time")) {
        UIManager::printLine("Time: " + String((const char*)stat["time"]));
    }
    if (stat.containsKey("lati") && stat.containsKey("long")) {
        UIManager::printLine("GPS: " + String((float)stat["lati"], 5) + "," +
                              String((float)stat["long"], 5));
    }
    if (stat.containsKey("alti")) {
        UIManager::printLine("Alt: " + String((float)stat["alti"], 0) + "m");
    }
    if (stat.containsKey("boot")) { // nonstandard, some forwarders add it
        UIManager::printLine("Boot: " + String((const char*)stat["boot"]));
    }
    UIManager::printLine("[!] Backhaul is CLEARTEXT -");
    UIManager::printLine("    GW location/status leaked.");
}

void GwmpSniffer::end() {
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
}
