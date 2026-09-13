# WaveRogue

RF security-auditing firmware for the **M5Stack Cardputer** (ESP32-S3),
built with PlatformIO + Arduino. It provides 17 keyboard-navigable
modules, organized into **LoRa Tools**, **Sub-GHz Tools**, and **NFC
Tools** menus, for auditing LoRaWAN networks, simple sub-GHz (OOK/ASK/FSK)
devices, and NFC-A/MIFARE Classic badges.

## ⚠️ Legal & ethical use

This firmware is for **authorized security auditing and education only**:
networks and devices you own, or that you have explicit written permission
to test (e.g. a pentest engagement, a CTF, or your own home-automation
gear). Almost every module stops short of breaking any encryption — it
either reads unencrypted protocol metadata, flags data that *looks*
unencrypted via statistical heuristics (entropy), or works with raw
RF/Wi-Fi/NFC timing you already have physical access to receive. The one
deliberate exception is **NFC Tools' MIFARE Classic default-key auditor**,
which actively tries to authenticate against a card's sectors using a
small dictionary of widely-published default/well-known keys - only run
it against badges/cards you own or are authorized to assess.

Radio transmission (the Sub-GHz Replay Tester, and optionally referenced
but not implemented in the Rogue Gateway module) is regulated in most
countries — check your local rules (e.g. ETSI EN 300 220 in the EU, FCC
Part 15 in the US) before enabling any TX path, and never transmit against
equipment you don't own or lack permission to test. The Wi-Fi promiscuous
(monitor-mode) capture used by the GWMP module is also subject to local
wiretapping/interception law in many jurisdictions even though it's
receive-only - only use it on networks you're authorized to assess. You
are responsible for how you use this code.

## Hardware

- M5Stack Cardputer (ESP32-S3, 240×135 TFT, built-in keyboard, microSD slot)
- A LoRa transceiver: SX1262 or SX1276 module/breakout (via SPI)
- A CC1101 sub-GHz transceiver module (via SPI) - M5Stack's Cap CC1101 also
  carries an ST25R3916 NFC front-end on the same board, used by NFC Tools
- A UART GNSS/GPS module (for the Wardriving module)

`config.h`'s LoRa pin defaults (`LORA_CS_PIN`, `LORA_SPI_*`, etc.) match
M5Stack's official **Cap LoRa-1262** Cardputer Cap-Bus add-on out of the
box. Two things about that module are easy to miss and will otherwise
produce RadioLib's `begin()` returning `-2`
(`RADIOLIB_ERR_CHIP_NOT_FOUND`) or a radio that inits but never
receives anything:

1. It brings its own dedicated SCK/MISO/MOSI on the Cap-Bus header,
   separate from the Cardputer's internal display SPI bus - `lora_auditor.cpp`
   and `lora_beacon_scanner.cpp` each open a second `SPIClass(HSPI)` on
   `LORA_SPI_SCK_PIN`/`LORA_SPI_MISO_PIN`/`LORA_SPI_MOSI_PIN` for this
   reason rather than using the default global `SPI`.
2. Its RF antenna switch is gated by P0 of an on-board PI4IOE5V6408 I2C
   GPIO expander, which must be driven high before RX/TX works -
   `lora_antenna_switch.*` does this over the Cardputer's existing
   internal I2C bus (set `WAVEROGUE_LORA_HAS_ANT_SWITCH` to 0 in
   `config.h` if your LoRa hardware has no such expander).

If you're using different LoRa hardware (a bare SX1262/SX1276 breakout,
wired by hand, sharing the main SPI bus, no antenna-switch expander),
update the pins in `config.h` accordingly and set
`WAVEROGUE_LORA_HAS_ANT_SWITCH` to 0.

The Sub-GHz pin defaults similarly match M5Stack's official **Cap
CC1101** Cardputer Cap-Bus add-on. It plugs into the exact same physical
Cap-Bus slot (and CS/SPI pins) as the Cap LoRa-1262 module above - on
real hardware the two are mutually exclusive, you swap whichever cap you
need for the LoRa Tools vs. Sub-GHz Tools menu. Two equivalent gotchas
apply here too:

1. Same dedicated SPI bus as the LoRa module (`SUBGHZ_SPI_*`, aliased to
   the same `LORA_SPI_*` pins in `config.h` since it's physically the
   same bus) - every `subghz_*.cpp` module opens its own `SPIClass(HSPI)`
   for this rather than the default global `SPI`.
2. Its antenna path is band-selected by an RF_SW0 GPIO rather than an
   I2C expander - `subghz_rf_switch.*` drives it per M5Stack's published
   truth table. Note only two of the three documented bands are
   reachable (433MHz and 868/915MHz) since RF_SW1 isn't broken out on the
   Cap-Bus header; 315MHz isn't selectable on this module.

If you're using a different Sub-GHz module/wiring, update the
`SUBGHZ_*`/`LORA_SPI_*` pins in `config.h` and adjust or remove the
`SubGhzRfSwitch::selectForFrequency()` calls if your hardware has no such
switch.

That same Cap CC1101 board also carries the ST25R3916 NFC front-end used
by **NFC Tools**, on its own CS/IRQ (`NFC_CS_PIN`/`NFC_IRQ_PIN` in
`config.h`) but the same shared Cap-Bus SPI bus. Those pins happen to
numerically match `LORA_BUSY_PIN`/`LORA_DIO1_PIN` - not a conflict, for
the same reason as above: only one cap is ever physically plugged in at
a time, so it "owns" those pins regardless of which module's macro name
you look at.

All pin assignments and RF parameters live in **`src/config.h`** — edit
that one file to match your actual wiring (Grove port, internal header, or
a HAT/Unit) and your region/target frequencies. Nothing else in the
codebase needs to change for a different pinout.

To switch between SX1262 and SX1276, flip the `WAVEROGUE_LORA_SX1262`
define at the top of `config.h` — RadioLib exposes both chips through the
same driver interface, so the rest of the LoRa code is unaffected.

## Project layout

```
platformio.ini                 Board, framework, and library dependencies
lib/                            Vendored NFC-RFAL + ST25R3916_ELECHOUSE (see "NFC Tools" below)
src/
  config.h                     All pin/frequency/threshold macros (edit this for your hardware)
  main.cpp                     Two-level menu state machine, dispatches to modules
  ui_manager.*                 Scrollable menu + scrolling log UI on the Cardputer's TFT
  rf_utils.*                   Shared entropy/hex/CRC helpers
  gps_logger.*                 TinyGPS++ wrapper used by the Wardriving module

  lora_auditor.*                Sniffer, Wardriver, Rogue Gateway, DevAddr Mapper,
                                 NetID Extractor, Plaintext Detector
  lora_inventory.*               Shared DevAddr table + NetID heuristic backend
  lora_beacon_scanner.*          Class-B gateway beacon scanner
  gwmp_sniffer.*                 Wi-Fi promiscuous GWMP backhaul metadata sniffer

  subghz_auditor.*               Raw sniffer + replay tester
  subghz_static_code.*           Fixed/static-code legacy remote discovery
  subghz_weather_decoder.*       Weather/temp sensor decoder (Nexus-style dictionary)
  subghz_wmbus_scanner.*         Wireless M-Bus smart meter scanner
  subghz_bug_detector.*          Analog bug / continuous-carrier detector
  subghz_pocsag_scanner.*        POCSAG pager scanner
  subghz_syncword_analyzer.*     Preamble/sync-word fingerprinting

  nfc_reader.*                   NFC-A reader/writer + MIFARE Classic default-key auditor
```

## LoRa Tools

1. **Sniffer & Meta-Analyzer** — passively receives LoRa PHY frames and
   decodes only the plaintext LoRaWAN MAC header: MType (Join-Request,
   (Un)confirmed Data Up/Down, ...), DevAddr, and FCnt. The application
   payload (FRMPayload) is always AES-128 encrypted under the session's
   AppSKey and is never decrypted.

2. **Wardriver (GPS+SD)** — extends the Sniffer by pairing each decoded
   frame with the current GPS fix and a timestamp, appending a row to
   `wardriving_log.csv` on the SD card for later mapping/analysis in an
   external tool (e.g. QGIS, a spreadsheet).

3. **Rogue Gateway (Honeypot)** — listens specifically for Join-Request
   frames and logs the handshake attempt (AppEUI, DevEUI, DevNonce,
   RSSI/SNR) to `join_attempts_log.csv`. It deliberately does **not** send
   a spoofed Join-Accept: a valid one requires the device's real AppKey,
   which passive sniffing can never reveal.

4. **DevAddr Mapper** — builds a live inventory of every DevAddr heard
   (hit count, last RSSI, last-seen time), turning passive sniffing into a
   device census of the local LoRaWAN sensor population.

5. **NetID / Provider ID** — for each DevAddr in the inventory, infers the
   LoRaWAN NetType and NwkID from the address's prefix bits (per the
   spec's DevAddr allocation table) and matches it against a small
   built-in table of known NetIDs (currently just The Things Network's
   legacy community NetID as a confidently-verified example). **A
   Join-Accept's real NetID field is encrypted with the device's AppKey
   and can't be read passively** - this module works from ordinary data
   frames instead. Extend the table in `lora_inventory.cpp` with NetIDs
   you've verified for networks you're authorized to audit (see the LoRa
   Alliance's public NetID registry).

6. **Class-B Beacon Scanner** — retunes to the fixed Class B beacon
   channel/SF and listens for the periodic (~128s), fixed-length,
   unencrypted beacon frame that confirms a Class-B-capable gateway is
   present and operating. Best-effort decodes the embedded GPS time/
   coordinates when present; the periodicity + presence itself is the
   reliable part of the finding.

7. **Plaintext Payload Detector** — same DevAddr inventory, computes
   Shannon entropy + printable-ASCII ratio on each device's FRMPayload,
   and flags devices whose payload looks statistically like it *isn't*
   properly encrypted (a null/default AppSKey, or no encryption at all).
   It never decrypts anything - a low score is itself the finding.

8. **GWMP Gateway Metadata (Wi-Fi)** — the odd one out: doesn't touch the
   LoRa radio at all. Puts the ESP32's own Wi-Fi in promiscuous mode and
   looks for legacy, **unencrypted** UDP/1700 "Semtech Packet Forwarder"
   traffic between a gateway and its network server, extracting the
   gateway's own GPS coordinates and status from its cleartext JSON `stat`
   messages. Only works against an open/unencrypted Wi-Fi backhaul within
   radio range - a wired or TLS-protected (Basic Station) backhaul shows
   nothing.

## Sub-GHz Tools

1. **Raw Sniffer (OOK/ASK)** — puts the CC1101 in RadioLib's "direct
   mode" and times edges in an interrupt to capture pulse widths without
   assuming any particular protocol. A decoding *aid* (raw widths + a
   rough ASCII visualization), not a full decoder.

2. **Replay Vulnerability Tester** — records one button-press worth of
   raw pulses, then, on Enter, re-transmits the exact recorded waveform.
   Use only against your own receiver, to check whether it accepts a
   replayed fixed code (i.e., lacks a rolling code).

3. **Static-Code Discovery** — captures each button-press as a separate
   timing "session" (segmented by the gap when you release the button,
   not the short gaps between a remote's own repeats within one press)
   and flags when two SEPARATE presses produce the identical code: proof
   of a fixed/static code, 100% vulnerable to replay.

4. **Weather/TPMS Decoder** — an rtl_433-style dictionary decoder with one
   fully-worked entry: the very common "Nexus"-style temp/humidity sensor
   protocol (sold under many rebrands). Falls back to a raw pulse dump for
   anything unrecognized, TPMS included (TPMS needs FSK + vendor-specific
   framing, out of scope for the OOK/PWM decoder here).

5. **Wireless M-Bus Scanner** — tunes to 868.95 MHz S-mode, Manchester-
   decodes the raw signal, and parses the fixed header every smart meter
   telegram starts with: manufacturer code, serial/device type, and a
   best-effort read of the CI-field's encryption mode (flagging meters
   that transmit with **no encryption**).

6. **Analog Bug Detector** — sweeps a configurable frequency list and
   flags a **continuous carrier** (unlike the short bursts of digital OOK/
   FSK devices) as a possible active analog transmitter. Note: the CC1101
   only tunes 300-348/387-464/779-928 MHz - classic 49 MHz/FM-broadcast/
   VHF bugs are physically out of reach of this hardware.

7. **POCSAG Pager Scanner** — NRZ-FSK bit recovery + POCSAG frame-sync
   detection, with best-effort numeric-message decode. Alphanumeric
   decode and FLEX are explicitly not implemented (documented as a known
   limitation, not silently wrong) - unrecognized/alpha codewords still
   show as raw hex so you can see the traffic exists. **Set
   `POCSAG_FREQ_MHZ` in config.h to a frequency you're actually authorized
   to audit** - paging frequencies are licensed and vary by country/site.

8. **Sync-Word Analyzer** — auto-detects the capture's timing unit,
   reconstructs a candidate 32-bit preamble/sync fingerprint (in both
   normal and bit-reversed form), and checks it against a small reference
   table (e.g. the CC1101's own factory-default sync word) to help
   classify unknown hardware by vendor. Extend the table in
   `subghz_syncword_analyzer.cpp` with your own findings.

## NFC Tools

1. **NFC Reader/Writer** — polls for NFC-A tags/badges and reports
   UID/ATQA/SAK. If the SAK matches a MIFARE Classic variant (Mini/1K/4K),
   it automatically sweeps every sector against a small dictionary of
   widely-published default/well-known keys (Key A and Key B alike) - the
   same kind of seed dictionary shipped by common open-source MIFARE
   auditing tools (mfoc, libnfc's `nfc-mfclassic`). Any cracked sector is
   read and appended to a per-UID dump file on the SD card
   (`/nfc/<UID>.txt`, rewritten fresh on every re-scan of the same tag),
   and the module runs a one-time **write-access self-test** on the first
   ordinary data block it can reach - it writes the block's own bytes back
   unchanged and reads them again to confirm the write path genuinely
   works, without ever changing what's stored on the tag. A tag that
   isn't a recognized MIFARE Classic SAK is still logged (UID/ATQA/SAK
   only) - this is a generic NFC-A reader first, a MIFARE Classic auditor
   second.

   Setup: this module needs the ST25R3916 NFC front-end that ships on the
   **same Cap CC1101 module** as the CC1101 (same Cap-Bus slot/SPI bus,
   separate CS/IRQ - `NFC_CS_PIN`/`NFC_IRQ_PIN` in `config.h`). It's built
   on the ESP32-validated "ST25R3916 + NFC-RFAL" Arduino library
   (`rfal_nfc.h`/`rfal_mf1.h`, from
   [wilson-elechouse/ST25R3916](https://github.com/wilson-elechouse/ST25R3916))
   rather than a from-scratch ISO14443A/Crypto1 implementation. That
   library is already **vendored into this repo** under `lib/NFC-RFAL/`
   and `lib/ST25R3916_ELECHOUSE/` - PlatformIO picks both up automatically
   (each has its own `library.properties`), no extra install step needed.
   If you ever need to update it: re-clone the repo above and replace
   those two folders (its GitHub Actions workflow folder is stripped out;
   nothing else needs to change).

   A **known, bounded limitation of this first version**: a full sweep of
   a locked 4K card (40 sectors × 2 key types × the dictionary in
   `nfc_reader.cpp`) can take on the order of tens of seconds and isn't
   interruptible mid-sweep - the status bar keeps updating per sector so
   it's clear the device hasn't frozen. The dictionary itself is
   intentionally small and clearly labeled as non-exhaustive: a sector
   that resists every key in it is *not* proven secure, only not
   trivially default-keyed.

   **Licensing note:** `lib/NFC-RFAL` and `lib/ST25R3916_ELECHOUSE` are
   STMicroelectronics/ELECHOUSE code under ST's own SLA0052 license (see
   the `LICENSE` file in each folder) - not MIT/Apache like the rest of
   this project's dependencies. SLA0052 permits redistribution but
   restricts use to "an integrated circuit that is manufactured by or for
   STMicroelectronics and is an NFC tag, NFC dynamic tag, NFC reader, or
   UHF reader" (the ST25R3916 qualifies) and forbids relicensing that code
   under an open-source license. This only affects those two vendored
   folders - it doesn't change how you can license the rest of WaveRogue.

## Keyboard controls

The Cardputer keyboard has no dedicated arrow keys, so navigation reuses
the punctuation row (adjust the mapping in `ui_manager.h` if your keyboard
layout differs):

| Key | Action |
|-----|--------|
| `;` | Up (menu navigation) |
| `.` | Down (menu navigation) |
| Enter | Select menu item / trigger a replay transmission |
| `` ` `` | Back/ESC — leave the current module, or go up one menu level |

Menus scroll automatically (▲/▼ indicators) once there are more items than
fit on screen.

## Building

```
pio run -e cardputer               # build (the only env is "cardputer", so plain `pio run` also works)
pio run -e cardputer -t upload     # flash
pio device monitor                 # serial log (115200 baud)
```

The `espressif32` platform version is pinned in `platformio.ini` to the
arduino-esp32 2.x / ESP-IDF 4.4 generation, which is what M5GFX/M5Unified/
M5Cardputer are built against. Newer platform releases jump to
arduino-esp32 3.x (IDF 5.x) and fail to build these libraries with an
`i2c_periph_signal_t ... has no member named 'module'` error. If a build
still fails on the pinned version (e.g. after a prior `pio run` cached a
different platform), clear the local build/package cache first:

```
rm -rf .pio
pio run -e cardputer
```

Also note that `lib_deps` deliberately does NOT pin `m5stack/M5GFX` or
`m5stack/M5Unified` directly - `m5stack/M5Cardputer` already depends on
specific, mutually-compatible versions of both. Pinning them again at the
top level lets PlatformIO's resolver satisfy a looser range with a newer
release instead, which can pull in a M5Unified version that has since
removed/renamed APIs (e.g. `Button_Class::getButton()`) that this
M5Cardputer release still calls, causing a
`has no member named 'getButton'` build error. If you need a newer
M5Unified/M5GFX for some other reason, update `m5stack/M5Cardputer` to a
release that's actually compatible with it rather than pinning them
independently.

## A note on scope and honesty

Several of the newer Sub-GHz modules (weather decode, wM-Bus, POCSAG,
sync-word analysis) implement real signal-processing techniques (NRZ/
Manchester bit recovery from edge timing, frame-sync search, etc.) but,
without lab hardware to validate every timing constant against, are
best described as solid starting points rather than certified decoders.
Each module's header comment is explicit about what's verified vs.
best-effort, and every one falls back to showing raw captured data rather
than silently failing when its specific decode doesn't match - so the
tool stays useful (and honest about its limits) even on signals outside
what it currently recognizes.
