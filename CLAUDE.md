# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Zip Drive Adapter: RP2350 PPA→USB bridge

Goal: plug a **parallel-port Iomega Zip 100** into a modern machine over USB. A Pico 2 talks the Zip's PPA protocol on the parallel side and shows up as a USB mass-storage device.

## Background
- A parallel Zip is a SCSI Zip with an Iomega PPA-3 bridge chip inside. The reference for the protocol is Linux `drivers/scsi/ppa.c` (and `imm.c` for the Zip Plus).
- USB→parallel cables (DB25/Centronics) can't work: they're printer-class only, not a real IEEE 1284 port.
- Level shifters that were ruled out: TXS0108E and BSS138 MOSFET boards (auto-direction or 10k pull-ups, too weak and slow for the cable), and the SN74LS245 (its output high is unsafe for 3.3V inputs).
- Related prior art: LPT100 (a PIC32 ZIP100 *emulator*, which is the reverse direction), and ZuluSCSI RP2040 USB-MSC firmware (for SCSI Zip drives).


### BOM
| Ref | Part | Notes |
|---|---|---|
| U4 | Pico 2 (RP2350) | USB at the left board edge |
| U1 | SN74LVC245AN | Data. DIR=GP17 (high = Pico→drive), OE#=GP18 |
| U2 | SN74LVC245AN | Control out. DIR tied to 3V3, OE#=GP19 |
| U3 | SN74LVC245AN | Status in. DIR tied to GND, OE#=GP19 |
| R1–R8 | 100Ω | Data series resistors, cable side |
| R9–R13 | 10k | Status pull-ups to 3V3 |
| R14–R15 | 10k | OE# pull-ups (buffers disabled at boot) |
| C1–C3 | 100nF | Decoupling |
| D1–D4 | 3mm LED, THT | PWR (green), READY (yellow), READ (green), WRITE (red). Avoid blue/white: their Vf is too high for 3.3V |
| R16–R19 | 470Ω | LED series resistors (standing / vertical mount, 2.54mm pitch); about 2–3mA each |
| J1 | DB25 female, right angle | Takes the Zip's original M-M cable |

All LVC245s run on 3V3 (inputs are 5V-tolerant). Use DIP sockets.

### GPIO map
| GPIO | Signal | DB25 pin |
|---|---|---|
| GP0 | /ACK (in) | 10 |
| GP1 | BUSY (in) | 11 |
| GP2 | PE (in) | 12 |
| GP3 | SELECT (in) | 13 |
| GP4 | /ERROR (in) | 15 |
| GP5–GP12 | D0–D7 (bidir, contiguous for PIO) | 2–9 |
| GP13 | /STROBE (out) | 1 |
| GP14 | /AUTOFD (out) | 14 |
| GP15 | /INIT (out) | 16 |
| GP16 | /SELIN (out) | 17 |
| GP17 | Data buffer DIR | — |
| GP18 | Data buffer OE# | — |
| GP19 | Ctrl/status buffers OE# | — |
| GP20 | READ LED D3 (high = on) | — |
| GP21 | WRITE LED D4 (high = on) | — |
| GP22 | READY LED D2 (high = on) | — |
DB25 pins 18–25 are GND.

## Firmware plan (not started)
- PIO handles the parallel timing (nibble mode first, EPP later).
- TinyUSB MSC presents the device to the host; its SCSI commands are translated into PPA-wrapped SCSI to the drive.
- Port the PPA connect/select/handshake sequences from `ppa.c`.
- Expect roughly 100–500 KB/s.
- LEDs: blink READ/WRITE per transfer with a minimum on-time (about 30–50ms) so short transfers stay visible; READY on once the Zip answers, blinking for errors. PWR is hardware (+3V3).
- Debugging tip: a CZH-LABS D-1042 DB25 breakout inline between a real PC and the drive, plus a logic analyzer, captures known-good PPA traffic.
