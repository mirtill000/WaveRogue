#include "rf_utils.h"
#include <math.h>

float RfUtils::shannonEntropy(const uint8_t* data, size_t len) {
    if (len == 0) return 0.0f;
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < len; i++) counts[data[i]]++;

    float entropy = 0.0f;
    for (int i = 0; i < 256; i++) {
        if (counts[i] == 0) continue;
        float p = (float)counts[i] / (float)len;
        entropy -= p * log2f(p);
    }
    return entropy;
}

float RfUtils::printableAsciiRatio(const uint8_t* data, size_t len) {
    if (len == 0) return 0.0f;
    size_t printable = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 0x20 && data[i] <= 0x7E) printable++;
    }
    return (float)printable / (float)len;
}

String RfUtils::bytesToHex(const uint8_t* data, size_t len) {
    String out;
    out.reserve(len * 2);
    char buf[3];
    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        out += buf;
    }
    return out;
}

uint16_t RfUtils::crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
