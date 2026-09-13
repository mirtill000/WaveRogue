#include "lora_antenna_switch.h"
#include "config.h"
#include <Wire.h>
#include <Arduino.h>

namespace {
    // PI4IOE5V6408 register map (I/O direction: 1=output; Output state: 1=high).
    constexpr uint8_t kRegIoDirection = 0x03;
    constexpr uint8_t kRegOutputState = 0x05;

    bool readReg(uint8_t reg, uint8_t& value) {
        Wire.beginTransmission(LORA_ANT_SWITCH_I2C_ADDR);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false; // repeated start, keep bus held
        if (Wire.requestFrom((int)LORA_ANT_SWITCH_I2C_ADDR, 1) != 1) return false;
        value = Wire.read();
        return true;
    }

    bool writeReg(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(LORA_ANT_SWITCH_I2C_ADDR);
        Wire.write(reg);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }
}

void LoraAntennaSwitch::enable() {
#if WAVEROGUE_LORA_HAS_ANT_SWITCH
    // Read-modify-write so we don't clobber any other pin this expander
    // might also be driving on your particular board.
    uint8_t dir = 0;
    if (readReg(kRegIoDirection, dir)) {
        writeReg(kRegIoDirection, dir | LORA_ANT_SWITCH_PIN_MASK);
    } else {
        // Expander not responding (wrong address, or none present) -
        // fall back to a blind write rather than blocking the radio
        // entirely; verify LORA_ANT_SWITCH_I2C_ADDR with an I2C scan if
        // RX/TX still doesn't work.
        writeReg(kRegIoDirection, LORA_ANT_SWITCH_PIN_MASK);
    }

    uint8_t out = 0;
    if (readReg(kRegOutputState, out)) {
        writeReg(kRegOutputState, out | LORA_ANT_SWITCH_PIN_MASK);
    } else {
        writeReg(kRegOutputState, LORA_ANT_SWITCH_PIN_MASK);
    }
#endif
}
