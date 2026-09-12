// =============================================================================
// WaveRogue - config.h
//
// Central place for every pin definition and radio parameter. The Cardputer
// exposes its extra GPIOs on the Grove port (G1/G2) and on the internal
// header used by M5Stack "Unit"/"HAT" add-ons. Because wiring varies a lot
// depending on which LoRa/Sub-GHz/GNSS module you actually soldered on,
// EVERYTHING hardware-specific lives here so you only have to edit one file.
//
// LEGAL / ETHICAL NOTE
// ---------------------------------------------------------------------------
// This firmware is built strictly for authorized security auditing and
// education (e.g. testing IoT devices and networks that you own, or that you
// have explicit written permission to assess). Radio transmission on
// licensed/ISM bands is regulated in most countries: verify local law
// (ETSI EN 300 220 in the EU, FCC Part 15 in the US, etc.) before enabling
// any TX-capable module. Do not use this tool against infrastructure or
// devices you do not have permission to test.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------
// Select which LoRa transceiver you have wired up. Only define ONE of
// these. RadioLib exposes a (mostly) unified API, so switching chips is a
// one-line change in lora_auditor.cpp (the class name used to construct
// the `radio` object), not a rewrite.
// -----------------------------------------------------------------------
#define WAVEROGUE_LORA_SX1262   1
// #define WAVEROGUE_LORA_SX1276 1

// -----------------------------------------------------------------------
// LoRa module pins (SPI shared with the Cardputer's default HSPI/FSPI bus).
// SX1262 needs BUSY; SX1276 needs DIO1 as a second IRQ line for some
// features but works fine with just DIO0 for RX-done. Adjust to your
// wiring (e.g. a M5Stack LoRa868 Unit / Ra-01/Ra-02 breakout on Grove+GPIO).
// -----------------------------------------------------------------------
#define LORA_CS_PIN     1     // NSS / CS
#define LORA_RST_PIN    2     // RESET
#define LORA_DIO0_PIN   3     // SX1276: DIO0 (RxDone/TxDone) | SX1262: IRQ
#define LORA_BUSY_PIN   4     // SX1262 only: BUSY line (tie unused on SX1276 builds)
#define LORA_DIO1_PIN   5     // SX1262: DIO1 (optional 2nd IRQ) | SX1276: DIO1 (unused here)

// -----------------------------------------------------------------------
// Sub-GHz (CC1101) module pins - separate SPI CS, shares SCK/MOSI/MISO.
// GDO0 is used both as the RX "data available" interrupt pin (OOK/ASK raw
// pulse capture) and, in TX mode, as the bit-banged output pin for replay.
// -----------------------------------------------------------------------
#define SUBGHZ_CS_PIN     6
#define SUBGHZ_GDO0_PIN   7
#define SUBGHZ_GDO2_PIN   8   // optional, not required for basic OOK RX/TX

// -----------------------------------------------------------------------
// GNSS (GPS) module - plain UART, e.g. on the Grove port (G1/G2).
// -----------------------------------------------------------------------
#define GPS_RX_PIN   13   // Cardputer RX  <-  GPS TX
#define GPS_TX_PIN   15   // Cardputer TX  ->  GPS RX
#define GPS_BAUD     9600
#define GPS_UART_NUM 1

// -----------------------------------------------------------------------
// microSD card slot (built into the Cardputer).
// -----------------------------------------------------------------------
#define SD_CS_PIN   12

// -----------------------------------------------------------------------
// RF parameters. LoRaWAN region defaults to EU868; change to 915.0 for
// US915, 923.0/920.9 for AS923, etc. Sub-GHz default targets the common
// 433.92 MHz OOK/ASK ISM band used by garage doors, weather stations, etc.
// -----------------------------------------------------------------------
#define LORA_FREQ_MHZ         868.1f
#define LORA_BANDWIDTH_KHZ    125.0f
#define LORA_SPREADING_FACTOR 7
#define LORA_CODING_RATE      5
#define LORA_SYNC_WORD        0x34   // Public LoRaWAN sync word
#define LORA_TX_POWER_DBM     2      // Only relevant for Module 3's optional TX

#define SUBGHZ_FREQ_MHZ       433.92f

// Wardriving CSV log filename (Module 2)
#define WARDRIVE_LOG_PATH "/wardriving_log.csv"
// Rogue-gateway join-attempt log filename (Module 3)
#define ROGUE_GW_LOG_PATH "/join_attempts_log.csv"

// Sub-GHz raw capture buffer size (number of pulse edges), Module 4/5.
#define SUBGHZ_MAX_PULSES 1024
