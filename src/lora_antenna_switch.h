// =============================================================================
// WaveRogue - lora_antenna_switch.h
//
// M5Stack's "Cap LoRa-1262" Cardputer add-on gates its RF antenna switch
// (an FM8625H) through P0 of an on-board PI4IOE5V6408 I2C GPIO expander,
// on the Cardputer's normal internal I2C bus. Per M5Stack's own module
// docs, P0 must be driven HIGH before the SX1262 can actually receive or
// transmit anything - this is separate from (and in addition to) getting
// the SPI wiring/pins right.
//
// Gated behind WAVEROGUE_LORA_HAS_ANT_SWITCH in config.h so this is a
// no-op on hardware without this expander (e.g. a bare SX1262 breakout).
// =============================================================================
#pragma once

namespace LoraAntennaSwitch {
    // Assumes Wire has already been initialized (M5Cardputer.begin() does
    // this on the Cardputer's internal I2C pins) - does NOT call
    // Wire.begin() itself, to avoid re-configuring a bus shared with other
    // on-board peripherals (RTC, power management, etc.).
    void enable();
}
