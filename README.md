# WaveRogue

RF security-auditing firmware for the **M5Stack Cardputer** (ESP32-S3),
built with PlatformIO + Arduino. It provides 10 keyboard-navigable
modules, organized into **LoRa Tools**, **Sub-GHz Tools**, and **NFC
Tools** menus, for auditing LoRaWAN networks, simple sub-GHz (OOK/ASK)
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

Every LoRa and Sub-GHz module is receive-only (the Rogue Gateway module
only ever listens for Join-Requests; it never sends a spoofed
Join-Accept). The Wi-Fi promiscuous (monitor-mode) capture used by the
GWMP module is likewise receive-only, but is still subject to local
wiretapping/interception law in many jurisdictions - only use it on
networks you're authorized to assess. Radio transmission on licensed/ISM
bands is still regulated in most countries regardless (e.g. ETSI EN 300
220 in the EU, FCC Part 15 in the US) - keep that in mind if you extend
any module with a TX path of your own. You are responsible for how you
use this code.

## Hardware

- M5Stack Cardputer (ESP32-S3, 240×135 TFT, built-in keyboard, microSD slot)
- A LoRa transceiver: SX1262 or SX1276 module/breakout (via SPI)
- A CC1101 sub-GHz transceiver module (via SPI) - M5Stack's Cap CC1101 also
  carries an ST25R3916 NFC front-end on the same board, used by NFC Tools
  (driven via M5Stack's own M5UnitUnified + M5Unit-NFC stack)
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
by **NFC Tools**, on its own CS (`NFC_CS_PIN`, G6) and IRQ (G4) but the
same shared Cap-Bus SPI bus - M5Stack's own `m5::unit::CapCC1101NFC` unit
class already defaults to this same CS internally; `nfc_reader.cpp` still
passes it explicitly (`CapCC1101NFC unit{NFC_CS_PIN}`) so `config.h`
stays the one place to change it. G6/G4 happen to numerically match
`LORA_BUSY_PIN`/`LORA_DIO1_PIN` - not a conflict, for the same reason as
above: only one cap is ever physically plugged in at a time, so it "owns"
those pins regardless of which module's macro name you look at. See the
NFC Tools section below for the pin-mixup investigation this project
went through before settling on these values.

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

  subghz_audit.*                  Sub-GHz Audit: band-scoped sweep/lock/decode/repeat-detect
  subghz_rf_switch.*              Cap CC1101 antenna-path (RF_SW0) selection

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

A single **Sub-GHz Audit** module. On entry it asks which ISM-band preset
to sniff — **315, 433, 868, or 915 MHz** — then sweeps that band alone
looking for activity:

- **Sweep & lock** — hops across the band a short dwell at a time, the
  same way a spectrum-analyzer-style scanner does; as soon as a channel
  shows enough raw OOK/ASK edges to look like a real burst (not noise), it
  locks on and captures the full pulse train, live, until the channel goes
  quiet again.
- **Continuous-carrier watch** — runs at the same time as the sweep/lock
  logic above: if RSSI stays above threshold far longer than any data
  burst would, that's flagged separately as a possible active analog
  bug/transmitter rather than a remote or sensor.
- **Generic PWM fixed-code decode** — every captured burst is run through
  a decoder for the short/long-pulse-with-sync-gap shape used by cheap
  fixed-code remotes and their countless clones. PWM remotes don't all
  use the same short:long pulse ratio, so each frame is tried against
  both common public ratios - **~1:3** (Princeton/PT2262/EV1527-style)
  and **~1:2** (Holtek HT12x / CAME-style gate-and-garage remotes) -
  and whichever cleanly decodes the most bits wins. A real remote
  usually repeats the same frame several times per button press, so
  every sync-delimited frame *within* one capture is decoded on its own
  and cross-checked against its neighbor: two consecutive frames
  agreeing is reported as **repeat-confirmed** (marked with a trailing
  `*`), a materially stronger signal than a single decode. The result is
  then labeled against the matching ratio/bit-count range (24-bit
  PT2262/EV1527-family, 12-bit Holtek/CAME-style, 32+-bit long
  PT2262-style) - a coarse heuristic, not a full protocol fingerprint
  database. Anything that doesn't fit either ratio at all is kept and
  reported as unrecognized raw data rather than forced into a decode.
- **Repeat / rolling-code detection** — each capture on a given channel is
  compared against recent captures on that *same* channel: an exact
  repeat across separate button presses means a static/fixed code
  (100% replay-vulnerable); a different payload every time suggests a
  rolling code instead.

Every finding (capture, repeat, continuous carrier) is appended to
`subghz_audit_log.csv` on the SD card. Tune the sweep step/dwell,
detection thresholds, and history size via the `SUBGHZ_AUDIT_*` macros in
`config.h`.

**315 MHz caveat:** the Cap CC1101's antenna switch (`RF_SW0`) only
exposes a 433 MHz path and an 868/915 MHz path in software (see
`subghz_rf_switch.h`) — there's no dedicated matching path for 315 MHz on
this hardware, so that preset tunes through the 433 MHz path instead, with
reduced range/sensitivity as a result.

**On sweep width vs. catching short transmissions:** all four presets are
scoped to the sub-band actually used by simple fixed-frequency devices in
that range, rather than a wide spectrum-analyzer-style sweep across the
whole regulatory allocation - specifically so a full sweep completes in a
few seconds instead of tens of seconds:

| Preset | Range | Channels |
|--------|-------|----------|
| 315 MHz | 314.0–316.0 MHz | ~21 |
| 433 MHz | 433.05–434.79 MHz | ~18 |
| 868 MHz | 868.0–868.6 MHz | ~7 |
| 915 MHz | 914.0–916.0 MHz | ~21 |

A short manual transmission (a Flipper Zero "Send", a garage remote
press - often under a second) has to land inside the CC1101's dwell
window on the right channel to be caught at all; at the default
`SUBGHZ_AUDIT_DWELL_MS`/`SUBGHZ_AUDIT_STEP_MHZ`, a 15-20 MHz-wide sweep
(what these presets used before being scoped down) takes on the order of
30-40 seconds per pass, so a one-off short burst is likely to be missed
even though reception itself works fine. The tradeoff is coverage: a
device sitting well outside these narrower windows (e.g. 868.95 MHz
Wireless M-Bus, or a 915 MHz device frequency-hopping across the full
902-928 MHz US ISM band) won't be swept at all. Hold/repeat a
transmission for the width of a full sweep pass if a single send isn't
being picked up, or widen the relevant entry in `subghz_audit.cpp`'s
`kBands[]` if you're specifically auditing a device outside these
ranges.

The overall sweep/lock/decode/repeat-detect approach - including
decoding every repeat of a captured frame independently and
cross-checking them against each other for higher-confidence results -
follows the same general design used by other Sub-GHz auditing tools for
Cardputer-class hardware (e.g. the CC1101 tooling in
[Evil-M5Project](https://github.com/7h30th3r0n3/Evil-M5Project), which
uses a richer table-driven multi-protocol decoder built on the same
idea). That architecture and general PT2262/EV1527/Holtek HT12x public
timing facts are independently implemented here rather than copied,
since that project's repository carries no explicit open-source license
- WaveRogue's decoder is deliberately simpler (one generic shape plus a
bit-count family label, not a maintained per-protocol table).

## NFC Tools

1. **NFC Reader/Writer** — polls for NFC-A tags/badges and reports
   UID/ATQA/SAK. If the SAK matches a MIFARE Classic variant (Mini/1K/4K),
   it automatically sweeps every sector against a **188-key built-in
   dictionary** of widely-published default/well-known/pattern keys (Key A
   and Key B alike) - the same kind of seed dictionary shipped by common
   open-source MIFARE auditing tools (mfoc, libnfc's `nfc-mfclassic`) -
   plus any extra keys loaded from an optional **`/nfc-wordlist.txt`** on
   the SD card root, tried after the built-in dictionary on every sector.
   One key per line, as plain hex (`FFFFFFFFFFFF`) or hex separated by
   `:`/`-`/space (`FF:FF:FF:FF:FF:FF`); blank lines and lines starting
   with `#` are skipped, and a missing file is not an error (up to
   `NFC_WORDLIST_MAX_KEYS` = 500 keys are loaded, see `config.h`). Any
   cracked sector is read and appended to a per-UID dump file on the SD
   card (`/nfc/<UID>.txt`, rewritten fresh on every re-scan of the same
   tag), and the module runs a one-time **write-access self-test** on the
   first ordinary data block it can reach - it writes the block's own
   bytes back unchanged and reads them again to confirm the write path
   genuinely works, without ever changing what's stored on the tag. A tag
   that isn't a recognized MIFARE Classic SAK is still logged (UID/ATQA/SAK
   only) - this is a generic NFC-A reader first, a MIFARE Classic auditor
   second.

   Setup: this module needs the ST25R3916 NFC front-end that ships on the
   **same Cap CC1101 module** as the CC1101 (same Cap-Bus slot/SPI bus,
   separate CS/IRQ - G6/G4). It's built on M5Stack's own official
   **M5UnitUnified + M5Unit-NFC** stack
   ([m5stack/M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC),
   `m5::unit::CapCC1101NFC` + `m5::nfc::NFCLayerA`) rather than a
   standalone ST25R3916 Arduino library or a from-scratch
   ISO14443A/Crypto1 implementation. `platformio.ini` lists it in
   `lib_deps` (`m5stack/M5Utility`, `m5stack/M5HAL`,
   `m5stack/M5UnitUnified`, `m5stack/M5Unit-NFC`) - PlatformIO resolves
   and downloads all four automatically, no vendoring or manual install
   step needed.

   Every failed key attempt costs a full tag reactivation (HLTA + WUPA +
   re-select) - a real MIFARE Classic tag needs that fresh select cycle
   before it will accept another Auth attempt at all, so this is what
   makes each wrong guess relatively expensive. To keep that cost from
   compounding across a whole card, whichever (key, key-type) pairs
   already cracked a sector this sweep are tried first on every later
   sector, before falling back to the full dictionary + wordlist - real
   cards overwhelmingly reuse the same handful of keys (often just one)
   across sectors, so this turns the common case into one attempt per
   sector instead of a full dictionary scan every time. An SD wordlist is
   also deduplicated against the built-in dictionary (and against itself)
   at load time, so a public keys-list file's overlap with the built-in
   188 keys isn't tried twice.

   A **known, bounded limitation of this first version**: a full sweep of
   a locked 4K card (40 sectors × 2 key types × the built-in 188-key
   dictionary, plus any SD wordlist) still isn't interruptible mid-sweep
   and can run several minutes in the worst case (every sector genuinely
   using a distinct, never-before-seen key) - the status bar keeps
   updating per sector so it's clear the device hasn't frozen. Even a
   188-key dictionary plus a large wordlist is still not exhaustive: a
   sector that resists every key tried is *not* proven secure, only not
   trivially default-keyed.

   **Two hardware quirks this module works around**, both worth knowing
   if you're porting it to different Cap CC1101 wiring:

   1. The CC1101 and ST25R3916 share one SPI bus but M5UnitUnified's own
      SPI adapter only ever manages its own chip's CS - it never
      deselects the *other* chip on the bus. `NfcReader::begin()`
      explicitly drives the CC1101's CS (`SUBGHZ_CS_PIN`) HIGH before
      touching the NFC chip at all.
   2. The ST25R3916's full bring-up (chip detection, reset, oscillator
      enable, RF field on) only tolerates running once per boot -
      calling `Units.begin()` again on an already-initialized chip with
      its field already on makes detection fail. `NfcReader::begin()`
      guards against this and only re-initializes once per boot;
      re-entering the module afterwards just resumes polling.

   If `Units.begin()` still fails on your hardware, the module prints
   the resolved board ID and SPI pins so a wiring mismatch is
   diagnosable, and double-check `NFC_CS_PIN`/`SUBGHZ_CS_PIN` in
   `config.h` against your Cap CC1101's actual CS assignments (these can
   vary across hardware revisions - trust more than one source, e.g. the
   chip's datasheet and the vendor driver's own hardcoded pins, over a
   single label or reference if they disagree).

## Keyboard controls

The Cardputer keyboard has no dedicated arrow keys, so navigation reuses
the punctuation row (adjust the mapping in `ui_manager.h` if your keyboard
layout differs):

| Key | Action |
|-----|--------|
| `;` | Up (menu navigation) |
| `.` | Down (menu navigation) |
| Enter | Select menu item |
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

**NFC Tools' M5UnitUnified dependency and the same risk, one level up:**
`m5stack/M5UnitUnified` (added for NFC Tools) does not itself pin
`m5stack/M5Unified` - it only requires *some* compatible version to
already be present, expecting it via M5Unified's own `M5.begin()` /
`M5.getBoard()` / `M5.getPin()`, which `M5Cardputer.begin()` (already
called once at boot in `UIManager::begin()`) already provides. In
principle PlatformIO reconciles this against the same M5Unified version
`M5Cardputer` pulls in; this combination is confirmed to build and run on
real Cardputer-ADV + Cap CC1101 hardware (that's how this migration was
verified), but if your resolved dependency graph ends up different (a
lockfile from a much older or newer `M5Cardputer` release, for instance),
a first build may need `m5stack/M5Cardputer`, `m5stack/M5UnitUnified`,
and `m5stack/M5Unit-NFC` nudged to versions that all agree on one
M5Unified release. `rm -rf .pio` plus a fresh `pio run` (as above) is the
first thing to try if dependency resolution looks stale.

## A note on scope and honesty

Sub-GHz Audit's PWM fixed-code decoder implements a real signal-processing
technique (locating a sync gap, then classifying short/long pulse pairs)
but, without lab hardware to validate every timing constant against, is
best described as a solid starting point rather than a certified decoder.
It falls back to showing the raw captured pulse train rather than silently
failing when a burst doesn't match its expected shape - so the tool stays
useful (and honest about its limits) on signals outside what it currently
recognizes.
