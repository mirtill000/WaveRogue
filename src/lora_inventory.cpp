#include "lora_inventory.h"
#include "config.h"
#include "rf_utils.h"

namespace {
    LoraInventory::DevAddrEntry table[DEVADDR_TABLE_SIZE];
    int tableCount = 0;

    int findByAddr(uint32_t devAddr) {
        for (int i = 0; i < tableCount; i++) {
            if (table[i].devAddr == devAddr) return i;
        }
        return -1;
    }
}

void LoraInventory::reset() {
    tableCount = 0;
}

void LoraInventory::record(uint32_t devAddr, float rssi, float snr, const uint8_t* frmPayload, size_t frmLen) {
    int idx = findByAddr(devAddr);
    if (idx < 0) {
        if (tableCount < DEVADDR_TABLE_SIZE) {
            idx = tableCount++;
        } else {
            // Table full: evict the least-recently-seen entry.
            idx = 0;
            for (int i = 1; i < tableCount; i++) {
                if (table[i].lastSeenMillis < table[idx].lastSeenMillis) idx = i;
            }
        }
        table[idx] = DevAddrEntry{};
        table[idx].devAddr = devAddr;
    }

    DevAddrEntry& e = table[idx];
    e.seenCount++;
    e.lastRssi = rssi;
    e.lastSnr = snr;
    e.lastSeenMillis = millis();

    if (frmPayload != nullptr && frmLen >= PLAINTEXT_MIN_PAYLOAD_LEN) {
        float entropy = RfUtils::shannonEntropy(frmPayload, frmLen);
        float asciiRatio = RfUtils::printableAsciiRatio(frmPayload, frmLen);
        e.lastPayloadEntropy = entropy;
        e.flaggedPlaintext = (asciiRatio >= PLAINTEXT_ASCII_RATIO_THRESHOLD) ||
                              (entropy < PLAINTEXT_ENTROPY_THRESHOLD);
    }
    // else: leave entropy/flag as previously computed (or -1/false if this
    // is the first time we've seen this device with too little payload).
}

int LoraInventory::count() {
    return tableCount;
}

const LoraInventory::DevAddrEntry* LoraInventory::entryByRecency(int rank) {
    if (rank < 0 || rank >= tableCount) return nullptr;

    // Small N (<= DEVADDR_TABLE_SIZE), so a simple selection scan per call
    // is plenty fast for a UI that redraws a couple of times a second.
    static int order[DEVADDR_TABLE_SIZE];
    bool used[DEVADDR_TABLE_SIZE] = {false};
    for (int pick = 0; pick <= rank; pick++) {
        int best = -1;
        for (int i = 0; i < tableCount; i++) {
            if (used[i]) continue;
            if (best < 0 || table[i].lastSeenMillis > table[best].lastSeenMillis) best = i;
        }
        used[best] = true;
        order[pick] = best;
    }
    return &table[order[rank]];
}

LoraInventory::NetIdGuess LoraInventory::identifyNetwork(uint32_t devAddr) {
    // NetType = number of leading '1' bits in DevAddr (0-7), per the
    // LoRaWAN DevAddr prefix allocation table (L2 spec, "NetID/DevAddr
    // assignment procedure").
    uint8_t netType = 0;
    for (int i = 0; i < 7; i++) {
        int bitPos = 31 - i;
        if (devAddr & (1UL << bitPos)) {
            netType++;
        } else {
            break;
        }
    }

    static const uint8_t kPrefixBits[8] = {1, 2, 3, 4, 5, 6, 7, 7};
    static const uint8_t kNwkIdBits[8]  = {6, 6, 9, 11, 12, 13, 15, 17};
    uint8_t prefixBits = kPrefixBits[netType];
    uint8_t nwkIdBits = kNwkIdBits[netType];
    uint32_t nwkId = (devAddr >> (32 - prefixBits - nwkIdBits)) & ((1UL << nwkIdBits) - 1);

    // Deliberately small, best-effort table of publicly documented NetIDs.
    // NetID assignment for most commercial/private operators is regional
    // (see the LoRa Alliance NetID registry) and not something safe to
    // hardcode from memory beyond well-established public examples - add
    // your own verified entries here for networks you're authorized to
    // audit rather than trusting this list to be exhaustive or current.
    struct KnownNet { uint8_t netType; uint32_t nwkId; const char* name; };
    static const KnownNet kKnown[] = {
        {0, 0x13, "The Things Network (legacy community NetID 0x13)"},
    };
    for (const auto& k : kKnown) {
        if (k.netType == netType && k.nwkId == nwkId) {
            return NetIdGuess{netType, nwkId, String(k.name)};
        }
    }

    char buf[56];
    snprintf(buf, sizeof(buf), "Unknown/private (type %u, NwkID 0x%lX)", netType, (unsigned long)nwkId);
    return NetIdGuess{netType, nwkId, String(buf)};
}
