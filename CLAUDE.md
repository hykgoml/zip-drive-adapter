# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Zip Drive Adapter: RP2350 PPA→USB bridge

Goal: plug a **parallel-port Iomega Zip 100** into a modern machine over USB. A Pico 2 talks the Zip's PPA protocol on the parallel side and shows up as a USB mass-storage device.

## Background
- A parallel Zip is a SCSI Zip with an Iomega PPA-3 bridge chip inside. The reference for the protocol is Linux `drivers/scsi/ppa.c` (and `imm.c` for the Zip Plus).
- USB→parallel cables (DB25/Centronics) can't work: they're printer-class only, not a real IEEE 1284 port.
- Level shifters that were ruled out: TXS0108E and BSS138 MOSFET boards (auto-direction or 10k pull-ups, too weak and slow for the cable), and the SN74LS245 (its output high is unsafe for 3.3V inputs).
- Related prior art: LPT100 (a PIC32 ZIP100 *emulator*, which is the reverse direction), and ZuluSCSI RP2040 USB-MSC firmware (for SCSI Zip drives).

## Hardware (board v0.6, untested; ordered from PCBWay 2026-11-25, due 2026-12-01)
- Board files are versioned as `zipbridge_vX.Y.*`, and each version is also a git tag and a GitHub release with its gerbers attached. The newest version is the one to order. `README.md` has the PCBWay order options.
- **Colours: BLACK solder mask with YELLOW silkscreen** (both standard PCBWay options; PCBWay has no green silkscreen). The bottom artwork is light ink on a dark mask: the logo prints the sticker's green parts, and the QR prints its light cells plus a 2-cell quiet zone. `art/*_light.json` holds those bitmaps; `art/*_dark.json` holds the v0.3–v0.5 dark-ink versions.
- **DB25 bought:** Connectors Pro 10-pack DB25 female right-angle PCB mount, Amazon B00C58IPSO, ".318in set back style" (8.08mm) with two board-lock tabs. McMaster 5063N19 is the same type (its drawing puts the front pin row about 8.0mm behind the flange).
- `zipbridge_v0.6.kicad_pcb` is v0.5 with the bottom artwork swapped to light ink (`art/swap_artwork_v06.py`). The logo is 14mm at (44.2, 13); the QR is 33 cells × 0.25mm at (57.0, 39.0). Both are mirrored. KiCad DRC is clean; the QR was checked by decoding a yellow-on-black render. 215 drill hits, 260 bottom-silk regions.
- v0.5 (66 × 79.8mm) was v0.4 plus **status LEDs** in a column on the right edge beside the Pico: D1 PWR (+3V3), D2 READY (GP22), D3 READ (GP20, Zip→PC), D4 WRITE (GP21, PC→Zip). Each is a 3mm THT LED with a 470Ω standing resistor (R16–R19); the cathodes go to the GND pour.
  - Made by `art/add_leds_v05.py` (placement), then `router.py ... --only <nets>`, which routed just the new nets around the existing copper. All 287 v0.4 traces are unchanged and no vias were added. `build.py` includes the LEDs, and its pads and nets match v0.5 exactly.
  - KiCad DRC (board rules, 0.2mm clearance) and PCBPA checks (PCBWay standard, 0.15mm) are both clean. 215 drill hits.
- v0.4 moved the bottom edge from 82 to 79.8mm so the .318in DB25 sits flush; J1 became the `EdgePinOffset7.70mm` variant (same pads and holes).
- v0.3 (KiCad 9) added bottom-silk artwork to v0.2, made by `art/add_artwork.py`:
  - the Ick logo, 14mm at (44.2, 13) under the Pico, printing the sticker's black parts;
  - a QR code to https://retrorecoveryrestore.com/, 29 modules × 0.25mm at (57.6, 39.4), printing the dark modules, with a 1mm quiet zone left unprinted.
  Both are mirrored so they read correctly from below. The bottom pin-legend text moved 6.5mm left to make room.
  - The artwork is many simple filled polygons (fractured, no holes), because PCBPA's KiCad import drops polygons that have holes.
  - The QR was checked by decoding it from a bottom-view render.
- v0.2 fixes over v0.1: a +3V3 stub at U1.20 joined to the pad, and solid GND connections on U4.13 and U3.1. Older versions' files are kept.
- Generator scripts (run with KiCad's Python): `build.py` places parts and sets nets → `router.py in out` autoroutes on a grid → `pour.py in out` adds GND pours.
- There is no schematic. **`build.py` is the netlist source of truth.** To change any connection, edit it and rerun the whole pipeline, then regenerate the gerbers.
- Running on this Mac (KiCad 9.0 is installed):
  - Python: `KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3`. It's Python 3.9 and has `pcbnew`, but no `numpy`, which `router.py` needs (`$KPY -m pip install numpy`).
  - `build.py` reads footprints from KiCad.app by default (override with `KICAD_FOOTPRINT_DIR`) and writes `zipbridge.kicad_pcb` (unrouted, gitignored) next to itself. It handles both KiCad 7 and 9 API differences, plus the DSUB `Female`→`Socket` footprint rename.
  - Pipeline: `$KPY build.py` → `$KPY router.py zipbridge.kicad_pcb routed.kicad_pcb` → `$KPY pour.py routed.kicad_pcb zipbridge_vX.Y.kicad_pcb` (a regenerated board is routed fresh, so it won't match v0.2's layout).
  - The router retries up to 12 times and prints `failed: [...]`. Any failed net means the board is incomplete.
  - DRC and gerbers: `kicad-cli` is at `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli` (`pcb drc`, `pcb export gerbers`, `pcb export drill`).
- The router hardcodes the board size (66×82), the grid origin (aligned to the DIP/Pico pin pitch), and the design rules, which must match `build.py`. `pour.py` hardcodes which pads get a solid GND connection (U2/U3 tie-offs, plus U3.1 and U4.13 since v0.2).
- PCBPA MCP server (`pcbpa`, http://127.0.0.1:8765/mcp) only works while the PCBPA app is running.
  - It can only open files in `~/Documents/PCBPA` (or folders of designs already open), so copy boards there first.
  - `open_design` reports "Couldn't open" even when it worked. Check `list_documents` before retrying.
  - Its coordinates have Y pointing up (Y = −KiCad Y), and layer 16 is the bottom copper. `DELETE (x y)` won't select silkscreen text, so make text edits in KiCad.
  - `AUTOROUTE` only works with Preferences ▸ Labs ▸ `labs_autoroute` turned on (it's on now). Board design rules reset to JLCPCB when a board is opened, so run `RULES 'PCBWay'` again. `RULES clearance=0.2` (and `copper_clearance=0.2`) reply "set 1 rule" but don't change what `RULES` lists, so PCBPA's checks really run at PCBWay standard 0.15mm. KiCad DRC is the 0.2mm check.
  - Imported KiCad footprints render in 3D as plain blocks. In `zipbridge_v0.6.pcbpa`, U4 was swapped (`REPLACE MOD-RPI-PICO@pcbpa-modules U4`) for PCBPA's own Pico module, which has a real 3D model; the pads and nets are the same and DRC is clean. This only affects the PCBPA copy, not the KiCad board or the gerbers.
- Footprint check (2026-11-22, against KiCad 9 libraries):
  - **Pico 2 (U4):** all 40 pads match KiCad's `RaspberryPi_Pico_Common_THT` exactly (0.000mm error, 1.0mm drills, not mirrored). It's rotated −90° with the USB end at the left edge; the library outline reaches x=0.15. It assumes the Pico is mounted component-side up on headers.
  - **DB25 (J1):** identical to KiCad's `DSUB-25_Socket_Horizontal_..._EdgePinOffset9.90mm_Housed_MountingHolesOffset11.32mm` (descr cites Tonar 16730). The front pin row is 9.90mm from the board edge, and the 3.2mm board-lock holes are 47.1mm apart and 11.32mm from the edge, 1.42mm in front of the pin-1 row. Pin 1 is at the right end as seen from outside the board (correct for a female face).
  - v0.4 uses the 7.70mm edge-offset variant to match the .318in DB25 that was bought (see above).

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
