// =============================================================================
// WaveRogue - lora_auditor.h
//
// Implements Modules 1-3, all of which revolve around passively receiving
// raw LoRa PHY frames and parsing the (unencrypted) LoRaWAN MAC header.
//
//   Module 1 - Sniffer & Meta-Analyzer: print MType/DevAddr/FCnt to screen.
//   Module 2 - Wardriving: same, but geotag+timestamp each frame and log
//              it as CSV to the SD card.
//   Module 3 - Rogue Gateway / Honeypot: watch specifically for Join-Request
//              frames and log the handshake attempt (DevEUI/AppEUI/DevNonce).
//
// IMPORTANT: we can only ever read the LoRaWAN MAC HEADER and FHDR fields.
// The actual application payload (FRMPayload) is encrypted with AES-128
// under the session's AppSKey, which we do not have and cannot derive by
// eavesdropping alone. Nothing in this file decrypts application data.
// =============================================================================
#pragma once

namespace LoraAuditor {
    // Shared radio bring-up, called once before entering any of the three
    // module loops below.
    bool begin();

    // Module 1: passive sniff + on-screen decode of LoRaWAN headers.
    void sniffLoop();

    // Module 2: sniff + GPS/SD-logged wardriving.
    void wardriveBegin();   // starts GPS + opens/creates the CSV log
    void wardriveLoop();
    void wardriveEnd();     // flush/close the log file

    // Module 3: Join-Request honeypot.
    void rogueGatewayBegin();
    void rogueGatewayLoop();
    void rogueGatewayEnd();
}
