// =============================================================================
// WaveRogue - lora_auditor.h
//
// Implements the LoRa PHY-sniffing modules, all of which revolve around
// passively receiving raw LoRa frames and parsing the (unencrypted)
// LoRaWAN MAC header - never the encrypted application payload itself.
//
//   Sniffer & Meta-Analyzer: print MType/DevAddr/FCnt to screen.
//   Wardriving: same, but geotag+timestamp each frame and log it as CSV.
//   Rogue Gateway / Honeypot: watch for Join-Request frames and log the
//     handshake attempt (DevEUI/AppEUI/DevNonce).
//   DevAddr Mapper / NetID Extractor / Plaintext Payload Detector: build
//     a shared device inventory (see lora_inventory.h) from the same
//     frames, rendered three different ways.
//
// IMPORTANT: we can only ever read the LoRaWAN MAC HEADER and FHDR fields.
// The actual application payload (FRMPayload) is encrypted with AES-128
// under the session's AppSKey, which we do not have and cannot derive by
// eavesdropping alone. The Plaintext Payload Detector flags payloads that
// LOOK unencrypted (low entropy/high-ASCII) - it never decrypts anything.
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

    // DevAddr Mapper: passive device inventory (see lora_inventory.h).
    void devAddrScanBegin();
    void devAddrScanLoop();
    void devAddrScanEnd();

    // NetID Extractor: same inventory, rendered as network/operator guesses.
    void netIdScanBegin();
    void netIdScanLoop();
    void netIdScanEnd();

    // Plaintext Payload Detector: same inventory, filtered to devices whose
    // FRMPayload looks like it might not actually be encrypted.
    void plaintextScanBegin();
    void plaintextScanLoop();
    void plaintextScanEnd();
}
