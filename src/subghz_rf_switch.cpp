#include "subghz_rf_switch.h"
#include "config.h"
#include <Arduino.h>

void SubGhzRfSwitch::selectForFrequency(float freqMHz) {
    pinMode(SUBGHZ_RF_SW0_PIN, OUTPUT);
    digitalWrite(SUBGHZ_RF_SW0_PIN, freqMHz >= SUBGHZ_RF_SW_THRESHOLD_MHZ ? HIGH : LOW);
}
