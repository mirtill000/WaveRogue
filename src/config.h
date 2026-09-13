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
// LoRa module pins - matches M5Stack's official "Cap LoRa-1262" Cardputer
// Cap-Bus add-on (SX1262). IMPORTANT: this module does NOT share the
// Cardputer's internal display SPI bus - it brings out its own dedicated
// SCK/MISO/MOSI on the Cap-Bus header, which is why LORA_SPI_* below gets
// its own SPIClass instance in lora_auditor.cpp/lora_beacon_scanner.cpp
// rather than reusing the global `SPI` object. Using the wrong bus here
// is the classic cause of RadioLib's begin() returning -2
// (RADIOLIB_ERR_CHIP_NOT_FOUND) - the SPI transactions just never reach
// the chip. If you're using a different LoRa breakout wired by hand,
// update all of these (and WAVEROGUE_LORA_SX1262 above) to match.
// -----------------------------------------------------------------------
#define LORA_CS_PIN       5     // NSS / CS
#define LORA_RST_PIN      3     // RESET
#define LORA_DIO0_PIN     3     // SX1276 only (unused on this SX1262 module)
#define LORA_BUSY_PIN     6     // SX1262 BUSY line
#define LORA_DIO1_PIN     4     // SX1262 IRQ (DIO1) | SX1276: DIO1 (unused here)
#define LORA_SPI_SCK_PIN  40
#define LORA_SPI_MISO_PIN 39
#define LORA_SPI_MOSI_PIN 14

// -----------------------------------------------------------------------
// Cap LoRa-1262's RF antenna switch (FM8625H) is gated by P0 of an
// on-board PI4IOE5V6408 I2C GPIO expander, shared on the Cardputer's
// normal internal I2C bus (SDA=G8/SCL=G9 - the same `Wire` instance
// M5Cardputer.begin() already initializes, so we don't re-init it here).
// M5Stack's docs are explicit that P0 must be driven HIGH before the
// radio will actually RX/TX - LoraAuditor::begin() does this. Set to 0
// if your LoRa module/wiring has no such switch (e.g. a plain SX1262
// breakout wired directly, with no expander in the path).
// -----------------------------------------------------------------------
#define WAVEROGUE_LORA_HAS_ANT_SWITCH 1
#define LORA_ANT_SWITCH_I2C_ADDR 0x43   // PI4IOE5V6408 default address - verify with an I2C scan if RX/TX still fails
#define LORA_ANT_SWITCH_PIN_MASK 0x01   // P0

// -----------------------------------------------------------------------
// Sub-GHz (CC1101) module pins - matches M5Stack's official "Cap CC1101"
// Cardputer Cap-Bus add-on. It plugs into the SAME physical Cap-Bus slot
// (and therefore the same CS/SPI pins) as the "Cap LoRa-1262" module
// above - the two are mutually exclusive on real hardware, you swap
// whichever one you need. GDO0 is used as the RX "data available"
// interrupt pin (OOK/ASK raw pulse capture). This module has no GDO2
// broken out on the Cap-Bus header, so RADIOLIB_NC is passed for it in
// code.
//
// RF_SW0 selects the antenna path; RF_SW1 isn't broken out here (fixed
// in hardware). The threshold below was originally guessed at 700MHz
// (an arbitrary split between 433 and 868) on the assumption that
// RF_SW0=LOW meant "433MHz path" and HIGH meant "868/915MHz path".
// Cross-checking Evil-M5Project's from-scratch CC1101 driver for this
// exact same Cap CC1101 hardware (same CS/GDO0/RF_SW0 pin numbers,
// independently confirming the wiring) shows its actual working
// threshold is 350MHz, not 700MHz - i.e. RF_SW0=HIGH covers 433MHz
// *and* 868/915MHz on one shared wideband path, and LOW is reserved for
// genuinely sub-350MHz signals (315MHz's region) rather than being
// "the 433MHz path". Using 700MHz here would silently route 433MHz
// through the wrong antenna path (LOW, matched for well below 433MHz)
// - likely degrading 433MHz range/sensitivity rather than just leaving
// 315MHz without a dedicated match, which is the only gap this leaves:
// 315MHz still has no complete path (RF_SW1=LOW, needed to fully commit
// to the low-band match, isn't controllable from here), but LOW is at
// least the closer of the two available options for it now.
// -----------------------------------------------------------------------
#define SUBGHZ_CS_PIN      5
#define SUBGHZ_GDO0_PIN    15   // CC1101_G0
#define SUBGHZ_RF_SW0_PIN  13   // CC1101_RF_SW0 - antenna band select (see subghz_rf_switch.h)
#define SUBGHZ_SPI_SCK_PIN  LORA_SPI_SCK_PIN   // shared Cap-Bus SPI bus (G40)
#define SUBGHZ_SPI_MISO_PIN LORA_SPI_MISO_PIN  // (G39)
#define SUBGHZ_SPI_MOSI_PIN LORA_SPI_MOSI_PIN  // (G14)
// Frequencies at/above this are routed to the shared 433/868/915MHz
// antenna path; below it, to the (incomplete, no RF_SW1) low-band path.
#define SUBGHZ_RF_SW_THRESHOLD_MHZ 350.0f

// -----------------------------------------------------------------------
// NFC (ST25R3916) - the Cap CC1101 module doesn't just carry a CC1101:
// it also has an ST25R3916 NFC/RFID front-end on the SAME Cap-Bus slot,
// sharing the SAME SPI bus (SCK/MOSI/MISO) as the CC1101 above, on its
// own CS and IRQ (G4) lines. Driven via M5Stack's own M5UnitUnified +
// M5Unit-NFC stack (m5::unit::CapCC1101NFC), whose wiring::addSPI()
// helper already knows the shared bus pins. Passed explicitly to
// CapCC1101NFC's constructor in nfc_reader.cpp rather than relying on
// the library's own default, so this macro is the one place to change
// it if a future unit's wiring differs.
// -----------------------------------------------------------------------
#define NFC_CS_PIN 6
// POWER_EN for the ST25R3916, numerically matching LORA_RST_PIN above -
// not a conflict, since the LoRa and CC1101 caps are physically mutually
// exclusive on the same Cap-Bus connector.
#define NFC_POWER_EN_PIN 3
// Where per-tag dumps are saved (one file per UID, re-scanning updates it).
#define NFC_DUMP_DIR "/nfc"
// Optional extra MIFARE Classic keys, one per line (plain hex or
// ':'/'-'/space-separated), loaded from the SD card root on top of the
// built-in dictionary if present. A missing file is not an error.
#define NFC_WORDLIST_PATH "/nfc-wordlist.txt"
#define NFC_WORDLIST_MAX_KEYS 500

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
// US915, 923.0/920.9 for AS923, etc.
// -----------------------------------------------------------------------
#define LORA_FREQ_MHZ         868.1f
#define LORA_BANDWIDTH_KHZ    125.0f
#define LORA_SPREADING_FACTOR 7
#define LORA_CODING_RATE      5
#define LORA_SYNC_WORD        0x34   // Public LoRaWAN sync word
#define LORA_TX_POWER_DBM     2      // Only relevant for Module 3's optional TX

// Wardriving CSV log filename (Module 2)
#define WARDRIVE_LOG_PATH "/wardriving_log.csv"
// Rogue-gateway join-attempt log filename (Module 3)
#define ROGUE_GW_LOG_PATH "/join_attempts_log.csv"

// =============================================================================
// Sub-GHz Audit
// =============================================================================
// Hops across whichever ISM-band preset the operator picks (315/433/868/
// 915 MHz - see subghz_audit.h's kFreqs315/433/868/915 for the actual
// frequency lists), dwelling briefly on each one. Two things can trigger
// a finding on a frequency:
//   - Enough raw OOK/ASK edges during one dwell to look like an actual
//     burst transmission (as opposed to noise) - the module locks on and
//     captures the full pulse train.
//   - RSSI staying above threshold continuously for longer than a data
//     burst would - flagged as a possible continuous-carrier transmitter
//     (an active bug, rather than a remote/sensor).
// A captured burst is run through a generic short/long-pulse ("PWM
// fixed-code") decoder - the pattern used by cheap PT2262/EV1527-style
// remotes and countless clones - falling back to a raw pulse dump if it
// doesn't decode cleanly. Each capture is compared against recent
// history on the same channel to flag an exact repeat (static/fixed
// code, 100% replay-vulnerable) versus a different payload every time
// (possible rolling code).
#define SUBGHZ_AUDIT_DWELL_MS            200
// How long to wait after an explicit VCO calibration strobe (CC1101
// CMD_CAL) before the next state transition - TI's CC1101 datasheet
// gives ~721us typical calibration time; this rounds up for margin.
// Negligible next to SUBGHZ_AUDIT_DWELL_MS above, so it doesn't meaningfully
// slow down hopping between the frequencies in a band's list.
#define SUBGHZ_AUDIT_CAL_SETTLE_US       800
// Edges captured during one dwell window above this many means "this
// looks like a real transmission, not just noise" - lock onto it.
#define SUBGHZ_AUDIT_MIN_PULSES          6
// How long a locked channel has to stay quiet before the audit decides
// the transmission ended and resumes sweeping (from the next channel).
#define SUBGHZ_AUDIT_LOCK_QUIET_MS       800
// RSSI level, and how long it must be continuously exceeded, to flag a
// channel as a possible continuous-carrier transmitter rather than a
// data burst.
#define SUBGHZ_AUDIT_RSSI_THRESHOLD_DBM  -70.0f
#define SUBGHZ_AUDIT_CARRIER_MIN_DURATION_MS 2500
// How many recent distinct captures (per channel) to remember for
// repeat-detection, how much per-pulse timing jitter (microseconds) to
// tolerate when comparing two raw captures as "the same" code, and the
// per-capture buffer size used both for that comparison and for the PWM
// decoder.
#define SUBGHZ_AUDIT_HISTORY_SIZE        12
#define SUBGHZ_AUDIT_MATCH_TOLERANCE_US  150
#define SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE 256
// Findings (captures, repeats, continuous carriers) are appended here.
#define SUBGHZ_AUDIT_LOG_PATH "/subghz_audit_log.csv"

// Live ISR edge-capture buffer size (number of pulse edges) while a
// channel is locked - can hold a longer burst than what actually gets
// stored into history/decoded (SUBGHZ_AUDIT_MAX_PULSES_PER_CAPTURE
// above), since a button press often repeats the same code several
// times back-to-back before going quiet.
#define SUBGHZ_MAX_PULSES 1024

// =============================================================================
// LoRa: DevAddr inventory / NetID / plaintext-payload detector
// =============================================================================
// Max distinct DevAddrs tracked at once by the inventory table shared by the
// DevAddr Mapper, NetID Extractor and Plaintext Payload Detector modules.
#define DEVADDR_TABLE_SIZE 40

// FRMPayload entropy (bits/byte, max 8.0) below this is flagged as
// "possibly unencrypted / weak or default key" - properly AES-encrypted
// data looks statistically close to random (~7.9-8.0 bits/byte), while
// plaintext ASCII/JSON/binary-with-structure payloads read noticeably
// lower. This is a heuristic, not proof: short payloads (a handful of
// bytes) can score low entropy purely from having too little data to be
// conclusive - the tool flags this case separately.
#define PLAINTEXT_ENTROPY_THRESHOLD 6.0f
// If this fraction of payload bytes falls in the printable-ASCII range
// (0x20-0x7E), the payload is almost certainly cleartext regardless of
// its entropy score.
#define PLAINTEXT_ASCII_RATIO_THRESHOLD 0.85f
// Below this length an entropy estimate is too noisy to trust on its own.
#define PLAINTEXT_MIN_PAYLOAD_LEN 4

// =============================================================================
// LoRa: Class B Gateway Beacon Scanner
// =============================================================================
// Per the LoRaWAN Class B / Regional Parameters specs, EU868 beacons are
// broadcast on a fixed channel at SF9/BW125, once every BEACON_PERIOD_S
// seconds, as a fixed-length, unencrypted PHY payload (no LoRaWAN MAC
// header at all - it's not a MAC frame). Change LORA_BEACON_FREQ_MHZ/SF
// for your region if you're not in EU868.
#define LORA_BEACON_FREQ_MHZ   869.525f
#define LORA_BEACON_SF         9
#define LORA_BEACON_BW_KHZ     125.0f
#define LORA_BEACON_PERIOD_S   128
// RFU(2)+Time(4)+CRC(2)+GwSpecific(7)+CRC(2) = 17 bytes for the EU868
// beacon layout. Some vendors/regions vary this - treat as a starting
// point and verify against a real gateway if exact-length matching
// misses beacons that are otherwise clearly present (right periodicity).
#define LORA_BEACON_EXPECTED_LEN 17

// =============================================================================
// LoRa: Gateway Backhaul (GWMP / Semtech UDP Packet Forwarder) sniffer
// =============================================================================
// This module is different from the others: it doesn't touch the LoRa
// radio at all. It puts the ESP32's Wi-Fi in promiscuous mode and looks
// for legacy, UNENCRYPTED UDP traffic between a LoRa gateway and its
// network server (the "Semtech UDP Packet Forwarder" / GWMP protocol,
// historically run in the clear on port 1700). It only works if the
// Cardputer is within range of that traffic on a Wi-Fi channel it is
// currently listening on - it cannot see traffic on a wired/Ethernet
// backhaul, and it cannot see anything if the operator has since moved to
// an encrypted transport (e.g. Basic Station over TLS), which most modern
// deployments should be using.
#define GWMP_UDP_PORT 1700
// Fixed Wi-Fi channel to sniff. Set to 0 to enable slow round-robin
// channel hopping across 1-13 instead (slower to catch a specific
// gateway, but works without knowing its channel ahead of time).
#define GWMP_WIFI_CHANNEL 0
#define GWMP_CHANNEL_HOP_MS 500
