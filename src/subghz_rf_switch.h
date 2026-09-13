// =============================================================================
// WaveRogue - subghz_rf_switch.h
//
// M5Stack's "Cap CC1101" Cardputer add-on routes the CC1101's antenna
// through a band-select RF switch, controlled by the RF_SW0/RF_SW1
// lines. Only RF_SW0 is broken out on the Cap-Bus header (RF_SW1 is
// fixed in hardware). The actual truth table (cross-checked against
// Evil-M5Project's from-scratch CC1101 driver for this identical
// hardware - same CS/GDO0/RF_SW0 pins, independently confirming the
// wiring) is:
//
//   RF_SW0 = HIGH -> shared 433/868/915 MHz path (freq >= 350 MHz)
//   RF_SW0 = LOW  -> low-band path, incomplete without RF_SW1
//   (315 MHz would need RF_SW1 = LOW too, which isn't controllable here,
//   so it gets the closer of the two available options - LOW - rather
//   than a fully-matched dedicated path)
//
// An earlier version of this file had RF_SW0=LOW meaning "the 433 MHz
// path" (with an arbitrary 700 MHz split point) - that was wrong and
// would have routed 433 MHz through the low-band match instead of the
// wideband one it actually needs. See SUBGHZ_RF_SW_THRESHOLD_MHZ in
// config.h for how the split point is set.
//
// Every Sub-GHz module must call SubGhzRfSwitch::selectForFrequency()
// with whatever frequency it's about to use, or the CC1101 will be
// transmitting/receiving into the wrong antenna path.
// =============================================================================
#pragma once

namespace SubGhzRfSwitch {
    void selectForFrequency(float freqMHz);
}
