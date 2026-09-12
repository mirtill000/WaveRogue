// =============================================================================
// WaveRogue - lora_inventory.h
//
// Shared backend for three menu modules that all revolve around the same
// underlying data: a table of DevAddrs seen on the air.
//
//   - DevAddr Mapper: shows the raw inventory (address, hit count, RSSI).
//   - NetID Extractor: shows, per device, a best-effort guess at which
//     network/operator it belongs to.
//   - Plaintext Payload Detector: shows only devices whose FRMPayload
//     looks suspiciously non-random (low entropy / high ASCII ratio).
//
// NetID identification here works from the DevAddr's NwkID prefix bits in
// ordinary DATA frames - NOT from Join-Accept. A real Join-Accept's NetID
// field is encrypted (technically AES-decrypted, per the LoRaWAN spec's
// quirky convention) under the joining device's AppKey, which a passive
// listener never has, so it cannot be read over the air. See the
// Join-Request handling in lora_auditor.cpp for the equivalent note on
// Module 3.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace LoraInventory {
    struct DevAddrEntry {
        uint32_t devAddr = 0;
        uint32_t seenCount = 0;
        float lastRssi = 0.0f;
        float lastSnr = 0.0f;
        uint32_t lastSeenMillis = 0;
        float lastPayloadEntropy = -1.0f;  // -1 = not enough payload bytes yet
        bool flaggedPlaintext = false;
    };

    void reset();

    // Called for every decoded LoRaWAN data frame. `frmPayload`/`frmLen`
    // may be null/0 if the frame carries no application payload (e.g. a
    // pure MAC-command/empty uplink) - pass what's available.
    void record(uint32_t devAddr, float rssi, float snr, const uint8_t* frmPayload, size_t frmLen);

    int count();
    // rank 0 = most recently seen. Returns nullptr if rank is out of range.
    const DevAddrEntry* entryByRecency(int rank);

    struct NetIdGuess {
        uint8_t netType;
        uint32_t nwkId;
        String name;
    };
    NetIdGuess identifyNetwork(uint32_t devAddr);
}
