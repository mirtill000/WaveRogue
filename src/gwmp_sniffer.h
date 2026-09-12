// =============================================================================
// WaveRogue - gwmp_sniffer.h
//
// Gateway Backhaul (GWMP) Metadata Extractor.
//
// Unlike every other module in this project, this one never touches the
// LoRa/Sub-GHz radios: it puts the ESP32-S3's own Wi-Fi radio into
// promiscuous (monitor) mode and looks for legacy, UNENCRYPTED UDP
// traffic between a LoRa gateway and its network server - the "Semtech
// UDP Packet Forwarder" protocol (aka GWMP), historically run in the
// clear on UDP port 1700. Its periodic PUSH_DATA "stat" messages include
// the gateway's own GPS coordinates and status in plain JSON.
//
// Scope/limitations (be honest about what this can and can't do):
//  - Only sees traffic actually reaching the Cardputer's Wi-Fi antenna on
//    a channel it's currently listening on - it cannot see a wired
//    Ethernet backhaul, and gives nothing on networks using WPA
//    encryption for the link (promiscuous capture still sees encrypted
//    802.11 payloads as opaque ciphertext - only an UNENCRYPTED/open Wi-Fi
//    backhaul, or one you hold the keys for and decrypt out-of-band, will
//    yield readable GWMP frames here).
//  - Only understands plain (non-QoS-Null, no HT-control) 802.11 Data
//    frames carrying a standard LLC/SNAP + IPv4 + UDP encapsulation.
//  - The standard Semtech "stat" JSON schema doesn't actually define an
///   "uptime" field - it has "time" (the gateway's own clock) and, on
//    some forwarders, a nonstandard "boot" field. We surface whichever of
//    these are present rather than inventing one.
// =============================================================================
#pragma once

namespace GwmpSniffer {
    bool begin();
    void loop();
    void end();
}
