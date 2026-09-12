# WaveRogue

RF security-auditing firmware for the **M5Stack Cardputer** (ESP32-S3),
built with PlatformIO + Arduino. It provides five keyboard-navigable
modules for auditing LoRaWAN and simple sub-GHz (OOK/ASK) devices and
networks.

## ⚠️ Legal & ethical use

This firmware is for **authorized security auditing and education only**:
networks and devices you own, or that you have explicit written permission
to test (e.g. a pentest engagement, a CTF, or your own home-automation
gear). It does not decrypt anything — it only reads unencrypted protocol
metadata (LoRaWAN MAC headers) or raw RF timing you already have physical
access to receive.

Radio transmission (used by Module 5, and optionally referenced but not
implemented in Module 3) is regulated in most countries — check your local
rules (e.g. ETSI EN 300 220 in the EU, FCC Part 15 in the US) before
enabling any TX path, and never transmit against equipment you don't own
or lack permission to test. You are responsible for how you use this code.

## Hardware

- M5Stack Cardputer (ESP32-S3, 240×135 TFT, built-in keyboard, microSD slot)
- A LoRa transceiver: SX1262 or SX1276 module/breakout (via SPI)
- A CC1101 sub-GHz transceiver module (via SPI)
- A UART GNSS/GPS module (for Module 2, wardriving)

All pin assignments live in **`src/config.h`** — edit that one file to
match your actual wiring (Grove port, internal header, or a HAT/Unit).
Nothing else in the codebase needs to change for a different pinout.

To switch between SX1262 and SX1276, flip the `WAVEROGUE_LORA_SX1262`
define at the top of `config.h` — RadioLib exposes both chips through the
same driver interface, so the rest of `lora_auditor.cpp` is unaffected.

## Project layout

```
platformio.ini        Board, framework, and library dependencies
src/
  config.h            All pin/frequency macros (edit this for your wiring)
  main.cpp            setup()/loop() state machine, dispatches to modules
  ui_manager.*         Menu + scrolling log UI on the Cardputer's TFT
  gps_logger.*         TinyGPS++ wrapper used by Module 2
  lora_auditor.*        Modules 1-3 (LoRaWAN header sniffing/wardriving/honeypot)
  subghz_auditor.*      Modules 4-5 (raw OOK/ASK sniffing and replay testing)
```

## Modules

1. **LoRaWAN Sniffer & Meta-Analyzer** — passively receives LoRa PHY
   frames and decodes only the plaintext LoRaWAN MAC header: MType
   (Join-Request, (Un)confirmed Data Up/Down, ...), DevAddr, and FCnt. The
   application payload (FRMPayload) is always AES-128 encrypted under the
   session's AppSKey and is never touched.

2. **LoRaWAN Wardriving & Heat-Mapper** — extends Module 1 by pairing each
   decoded frame with the current GPS fix and a timestamp, appending a row
   to `wardriving_log.csv` on the SD card for later mapping/analysis in
   an external tool (e.g. QGIS, a spreadsheet).

3. **Rogue LoRa Gateway Emulator (Honeypot)** — listens specifically for
   Join-Request frames and logs the handshake attempt (AppEUI, DevEUI,
   DevNonce, RSSI/SNR) to `join_attempts_log.csv`. It deliberately does
   **not** send a spoofed Join-Accept: a valid one requires the device's
   real AppKey, which passive sniffing can never reveal, and broadcasting
   an unverifiable one would just be discarded by any spec-compliant
   stack. The useful audit signal here is purely in observing which
   devices attempt to join and how often/how they back off.

4. **Sub-GHz Sniffer & Protocol Analyzer (OOK/ASK)** — puts the CC1101 in
   RadioLib's "direct mode" (raw demodulated signal on GDO0) and times
   edges in an interrupt to capture pulse widths without assuming any
   particular protocol. Prints the raw pulse durations and a rough ASCII
   visualization to help you eyeball whether a signal looks like
   Manchester, PWM, etc. It is a decoding *aid*, not a full decoder.

5. **Sub-GHz Replay Vulnerability Tester** — records one press worth of
   raw pulses the same way Module 4 does, then, on Enter, re-transmits
   the exact recorded waveform via the CC1101 in TX direct mode. Use this
   only against your own receiver, to check whether it accepts a replayed
   fixed code (i.e., lacks a rolling code) — a classic and well-documented
   class of RF vulnerability.

## Keyboard controls

The Cardputer keyboard has no dedicated arrow keys, so navigation reuses
the punctuation row (adjust the mapping in `ui_manager.h` if your keyboard
layout differs):

| Key | Action |
|-----|--------|
| `;` | Up (menu navigation) |
| `.` | Down (menu navigation) |
| Enter | Select menu item / trigger Module 5 replay |
| `` ` `` | Back/ESC — stop current module, return to main menu |

## Building

```
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial log (115200 baud)
```
