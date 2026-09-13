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
// whichever one you need. GDO0 is used both as the RX "data available"
// interrupt pin (OOK/ASK raw pulse capture) and, in TX mode, as the
// bit-banged output pin for replay. This module has no GDO2 broken out
// on the Cap-Bus header, so RADIOLIB_NC is passed for it in code.
//
// RF_SW0 selects the antenna path per M5Stack's published truth table;
// RF_SW1 isn't broken out here (fixed in hardware), so only two of the
// three documented bands are actually reachable: RF_SW0=LOW -> 433MHz
// path, RF_SW0=HIGH -> 868/915MHz path. 315MHz (which needs RF_SW1=LOW)
// is NOT selectable on this module - see subghz_rf_switch.cpp.
// -----------------------------------------------------------------------
#define SUBGHZ_CS_PIN      5    // shared with LORA_CS_PIN - same physical Cap-Bus pin
#define SUBGHZ_GDO0_PIN    15   // CC1101_G0
#define SUBGHZ_RF_SW0_PIN  13   // CC1101_RF_SW0 - antenna band select (see subghz_rf_switch.h)
#define SUBGHZ_SPI_SCK_PIN  LORA_SPI_SCK_PIN   // shared Cap-Bus SPI bus (G40)
#define SUBGHZ_SPI_MISO_PIN LORA_SPI_MISO_PIN  // (G39)
#define SUBGHZ_SPI_MOSI_PIN LORA_SPI_MOSI_PIN  // (G14)
// Frequencies at/above this are routed to the 868/915MHz antenna path;
// below it, to the 433MHz path (see the RF Switch Control Truth Table).
#define SUBGHZ_RF_SW_THRESHOLD_MHZ 700.0f

// -----------------------------------------------------------------------
// NFC (ST25R3916) - the Cap CC1101 module doesn't just carry a CC1101:
// it also has an ST25R3916 NFC/RFID front-end on the SAME Cap-Bus slot,
// sharing the SAME SPI bus (SCK/MOSI/MISO) as the CC1101 above, on its
// own CS/IRQ lines. NFC_CS/NFC_IRQ happen to numerically match
// LORA_BUSY_PIN/LORA_DIO1_PIN - that's not a conflict: the LoRa and
// CC1101 caps are physically mutually exclusive on the same Cap-Bus
// connector, so whichever cap is actually plugged in "owns" those pins.
// -----------------------------------------------------------------------
#define NFC_CS_PIN       6    // ST25R3916 CS
#define NFC_IRQ_PIN      4    // ST25R3916 IRQ
#define NFC_SPI_SCK_PIN  SUBGHZ_SPI_SCK_PIN   // shared Cap-Bus SPI bus (G40)
#define NFC_SPI_MISO_PIN SUBGHZ_SPI_MISO_PIN  // (G39)
#define NFC_SPI_MOSI_PIN SUBGHZ_SPI_MOSI_PIN  // (G14)
// How long (ms) each discovery cycle polls before restarting - keep short
// so the module stays responsive to ESC while idle between tags.
#define NFC_DISCOVER_DURATION_MS 1000
// Where per-tag dumps are saved (one file per UID, re-scanning updates it).
#define NFC_DUMP_DIR "/nfc"

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

// =============================================================================
// Sub-GHz: Weather/TPMS Telemetry Decoder
// =============================================================================
// This is a dictionary-based decoder in the spirit of rtl_433, but with a
// SINGLE fully-implemented entry rather than rtl_433's ~200 protocols:
// the very common "Nexus"-style temperature/humidity sensor protocol
// (sold under many rebrands - Bresser, Digitech XC0348, Optex 990045,
// Number8, Weather Star, and others - all sharing the same 36-bit
// PWM-encoded frame). Extend NEXUS_* below or add another dictionary
// entry in subghz_weather_decoder.cpp for other sensors.
//
// TPMS (tire-pressure sensors) are NOT decoded here: they almost always
// use FSK with manufacturer-specific framing (Schrader, Continental,
// etc. all differ), which needs a different CC1101 modulation setup than
// the OOK/PWM decode below - the module falls back to a raw capture dump
// for anything it doesn't recognize, TPMS included.
#define NEXUS_BIT_HIGH_US       500   // fixed "sync" HIGH pulse per bit
#define NEXUS_GAP_ZERO_US       1000  // LOW gap encoding bit value 0
#define NEXUS_GAP_ONE_US        2000  // LOW gap encoding bit value 1
#define NEXUS_GAP_TOLERANCE_US  300   // +/- matching tolerance
#define NEXUS_FRAME_BITS        36

// Wardriving CSV log filename (Module 2)
#define WARDRIVE_LOG_PATH "/wardriving_log.csv"
// Rogue-gateway join-attempt log filename (Module 3)
#define ROGUE_GW_LOG_PATH "/join_attempts_log.csv"

// Sub-GHz raw capture buffer size (number of pulse edges), Module 4/5.
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

// =============================================================================
// Sub-GHz: Static-Code Legacy System Discovery
// =============================================================================
// How many recent distinct captures to remember for repeat-detection, and
// how much per-pulse timing jitter (in microseconds) to tolerate when
// deciding two captures are "the same" code (cheap OOK transmitters are
// not crystal-perfect, so exact microsecond equality is too strict).
#define STATICCODE_HISTORY_SIZE 12
#define STATICCODE_MATCH_TOLERANCE_US 150
// A gap this long with no RF edges marks the end of one button-press
// "session" (as opposed to the much shorter gaps between the several
// back-to-back repeats a remote sends within a single press, which we
// deliberately do NOT use as the comparison boundary - repeats within one
// press are identical even for rolling-code remotes, so only comparing
// across separate sessions actually tests for a static/fixed code).
#define STATICCODE_SESSION_GAP_MS 300
#define STATICCODE_MAX_PULSES_PER_SESSION 256

// =============================================================================
// Sub-GHz: Wireless M-Bus Smart Meter Scanner
// =============================================================================
// wM-Bus S-mode (most common for EU water/gas/heat meters) lives at
// 868.95 MHz, Manchester-coded at 32.768 kbps. T-mode (electricity meters,
// frequent-transmit) uses 868.3 MHz at 100 kbps, 3-out-of-6 coded. Only
// S-mode is implemented here; T-mode decoding needs different framing.
#define WMBUS_SMODE_FREQ_MHZ 868.95f
#define WMBUS_TMODE_FREQ_MHZ 868.3f
// Manchester "chip" (half-bit) duration at 32.768 kbps: 1/(2*32768) s.
// NOTE: this is a fast, software-edge-timing-unfriendly rate - ESP32 GPIO
// interrupt latency is usually fine, but expect occasional missed/garbled
// edges on marginal signal, more so than the other (slower) OOK modules.
#define WMBUS_MANCHESTER_UNIT_US 15
#define WMBUS_MANCHESTER_TOLERANCE_US 6
// Plausibility bounds used to accept a decode in the absence of a
// verified CRC implementation (see subghz_wmbus_scanner.cpp header
// comment for why exact CRC framing isn't attempted here).
#define WMBUS_MIN_L_FIELD 9
#define WMBUS_MAX_L_FIELD 250

// =============================================================================
// Sub-GHz: Analog Bug / Continuous-Carrier Detector
// =============================================================================
// IMPORTANT HARDWARE LIMITATION: the CC1101 only tunes 300-348 MHz,
// 387-464 MHz and 779-928 MHz. Classic analog bugs/baby monitors in the
// 49 MHz or FM-broadcast (88-108 MHz) bands, and true VHF (30-300 MHz)
// devices, are physically outside what this radio can reach - that needs
// a wideband SDR, not a CC1101. What IS in range: many cheap 433/434 MHz
// and 900 MHz OOK/FSK audio/video bugs and FHSS baby monitors. This
// module scans the frequency list below and flags a CONTINUOUS carrier
// (RSSI staying above threshold for longer than a burst-y data
// transmission would) as a possible active analog transmitter.
#define BUG_SCAN_FREQ_LIST_MHZ { 390.0f, 433.92f, 434.42f, 446.0f, 869.525f, 915.0f }
#define BUG_CARRIER_RSSI_THRESHOLD_DBM -70.0f
#define BUG_CARRIER_MIN_DURATION_MS 2500
#define BUG_SCAN_DWELL_MS 400

// =============================================================================
// Sub-GHz: POCSAG/FLEX Pager Scanner
// =============================================================================
// Paging frequencies are licensed and vary enormously by country/site
// (hospitals and factories often run their own on-site paging transmitter
// in the 148/154/173/453-470 MHz ranges depending on region). There is no
// universal default - CHANGE THIS to the frequency you are authorized to
// audit. The value below is only a common example in some deployments.
#define POCSAG_FREQ_MHZ   466.230f
#define POCSAG_BAUD        1200
// Standard POCSAG frame synchronization codeword (BCH(31,21)-coded frame
// sync), preceded by a long 0xAA... bit-sync preamble.
#define POCSAG_FRAME_SYNC_CODEWORD 0x7CD215D8UL

// =============================================================================
// Sub-GHz: Preamble / Sync-Word Analyzer
// =============================================================================
// Small table of illustrative "known" sync words for operator reference
// when classifying captured hardware. NOTE: several commonly-quoted
// values (e.g. 0x2DD4, often cited for IEEE 802.15.4/Zigbee) belong to
// radios that normally operate at 2.4 GHz - out of CC1101's tuning range
// - and are listed here only as reference/education, not as something
// this hardware can actually receive. Extend SyncWordAuditor's table in
// subghz_syncword_analyzer.cpp with values relevant to your own targets.
