# Bring-up checklist (board v0.6)

Do these in order. Stop at the first failure; the "if not" column says where to look.

Tools: USB cable, serial terminal (`screen /dev/tty.usbmodem* 115200`, or any terminal), multimeter, logic analyzer, DB25 breakout with jumper wires.

## 0. Build and flash

```bash
cmake -S firmware -B firmware/build -G Ninja && ninja -C firmware/build
```

Hold BOOTSEL, plug in USB, and copy `firmware/build/zipbridge_bringup.uf2` to the `RP2350` drive. The Pico reboots by itself.

Or, with the board in BOOTSEL mode and `picotool` installed:

```bash
picotool load -x firmware/build/zipbridge_bringup.uf2
```

## Steps

| # | Do | Expect | If not |
|---|---|---|---|
| 1 | Look the board over: chip orientation in the sockets, solder bridges. Plug in USB with nothing on the DB25. | PWR LED on. | 3V3 short or LED/R16 wrong way round. Unplug at once if anything gets hot. |
| 2 | Open the serial port. | READY blinks at 1 Hz before the port opens, then stays on. The banner and `>` prompt appear. Close and reopen the terminal: the banner and prompt come back. | No serial device: UF2 not flashed, or bad USB cable. |
| 3 | `info` | Version, chip, `oe data=off ctrl=on, dir=in`, all control lines `=1`. | — |
| 4 | `leds` | READY, then READ, then WRITE, each on alone for 0.5 s. | LED fitted backwards, or wrong resistor. |
| 5 | `status` with nothing connected | `/ACK=1 BUSY=1 PE=1 SELECT=1 /ERROR=1` | A 0 points at that line's 10k pull-up (R9–R13) or U3. |
| 6 | `ctrl strobe 0`, `status`, `ctrl strobe 1` | READ and WRITE flash. No change in status (nothing is wired yet). | — |
| 7 | No jumpers. Multimeter between DB25 pin 18 (GND) and the pin named. Run `walk 3000` and press Enter. | Each named pin reads about 3.3 V while it's high, and every other output pin reads about 0 V. | A pin stuck low or high: U1/U2 solder, or its 100Ω resistor (data only). A pin that is high at the wrong step: swapped traces. Refused with no jumpers fitted: crosstalk; run `scan` (step 9) to see it, then use `walk force`. |
| 8 | No jumpers. Logic analyzer on pins 2–9, 1, 14, 16, 17 (GND on 18). Run `pattern`, press Enter, and capture 50 ms. | D0 12 kHz, D1 6, D2 4, D3 3, D4 2.4, D5 2, D6 1.71, D7 1.5, /STROBE 1.33, /AUTOFD 1.2, /INIT 1.09, /SELIN 1.0 kHz. Clean edges. | A missing line points at its buffer pin. Two lines that look the same point at a bridge between them. Refused with no jumpers fitted: crosstalk; run `scan` (step 9) to see it, then use `pattern force`. |
| 9 | No jumpers. `scan` | `scan: 0 links, 0 possible shorts, ...`. The 5 status pins read `stuck high`; the data lines read `floating?` or `stuck`. | Any link here means crosstalk (see below). Treat later results with care. |
| 10 | Loopback passes A–D below, with `scan`. | The output listed for each pass. | See "Reading scan output". |

Notes:

- Step 7: WRITE flashes for 30 ms at each `walk` step. Step 8: during `pattern` WRITE looks steadily lit.
- Step 8: edges have a few µs of jitter from USB interrupts, but the average frequency is exact. D0 can very rarely miss one pulse (a stretched half-period). Neither is a wiring fault.
- The data pins have pull-ups (RP2350 erratum E9). With `oe data off`, `data` reads `0xFF`. That is normal.
- Step 9: an unwired data line can pick up a neighbouring control line through crosstalk and show a false link. If this scan shows any link, crosstalk is present.

The terminal must raise DTR (`screen` does). Otherwise `walk` and `pattern` cancel at once and no output is shown.

Console: `help scan` shows the jumper rules. Ctrl-U or Ctrl-C clears the line. Keys typed during `leds` run after it finishes.

## Jumper rules (scan)

- Wire an **output to an input** only: data → status, control → status, or control → data.
- **Never** join two control pins, or two data pins.
- **At most one jumper per input pin.**
- **Remove all jumpers** before `walk`, `pattern` and `dir out`; the firmware refuses if it detects control → data jumpers, but it can't see data↔data or control↔control ones.

The scan can't see every wiring mistake (see "Blind spots"), so check the jumpers by eye.

## Loopback passes

Pins are DB25 pin numbers. Each "x→y" is one jumper on the breakout.

### Pass A: 2→10, 3→11, 4→12, 5→13, 6→15

```
D0 (pin 2) -> /ACK (pin 10)
D1 (pin 3) -> BUSY (pin 11)
D2 (pin 4) -> PE (pin 12)
D3 (pin 5) -> SELECT (pin 13)
D4 (pin 6) -> /ERROR (pin 15)
D0 (pin 2) stuck high
D1 (pin 3) stuck high
D2 (pin 4) stuck high
D3 (pin 5) stuck high
D4 (pin 6) stuck high
D5 (pin 7) floating?
D6 (pin 8) floating?
D7 (pin 9) floating?
scan: 5 links, 0 possible shorts, 5 stuck
```

D0–D4 read "stuck high" in phase 1 because they are wired to a status pin's 10k pull-up. That is expected.

### Pass B: 7→10, 8→11, 9→12, 1→13, 14→15

```
D5 (pin 7) -> /ACK (pin 10)
D6 (pin 8) -> BUSY (pin 11)
D7 (pin 9) -> PE (pin 12)
/STROBE (pin 1) -> SELECT (pin 13)
/AUTOFD (pin 14) -> /ERROR (pin 15)
D0 (pin 2) floating?
D1 (pin 3) floating?
D2 (pin 4) floating?
D3 (pin 5) floating?
D4 (pin 6) floating?
D5 (pin 7) stuck high
D6 (pin 8) stuck high
D7 (pin 9) stuck high
scan: 5 links, 0 possible shorts, 3 stuck
```

### Pass C: 16→10, 17→11, 1→2, 14→3, 16→4, 17→5

Pin 16 and pin 17 each carry two jumpers. That's fine: each is one output feeding two inputs.

```
/STROBE (pin 1) -> D0 (pin 2)
/AUTOFD (pin 14) -> D1 (pin 3)
/INIT (pin 16) -> D2 (pin 4)
/INIT (pin 16) -> /ACK (pin 10)
/SELIN (pin 17) -> D3 (pin 5)
/SELIN (pin 17) -> BUSY (pin 11)
D4 (pin 6) floating?
D5 (pin 7) floating?
D6 (pin 8) floating?
D7 (pin 9) floating?
PE (pin 12) stuck high
SELECT (pin 13) stuck high
/ERROR (pin 15) stuck high
phase 2 skipped: control -> data jumpers present
scan: 6 links, 0 possible shorts, 3 stuck
```

### Pass D: 1→6, 14→7, 16→8, 17→9

```
/STROBE (pin 1) -> D4 (pin 6)
/AUTOFD (pin 14) -> D5 (pin 7)
/INIT (pin 16) -> D6 (pin 8)
/SELIN (pin 17) -> D7 (pin 9)
D0 (pin 2) floating?
D1 (pin 3) floating?
D2 (pin 4) floating?
D3 (pin 5) floating?
/ACK (pin 10) stuck high
BUSY (pin 11) stuck high
PE (pin 12) stuck high
SELECT (pin 13) stuck high
/ERROR (pin 15) stuck high
phase 2 skipped: control -> data jumpers present
scan: 4 links, 0 possible shorts, 5 stuck
```

Each scan output starts with the jumper rules. The **unwired data lines** (the `floating?` lines) may instead show `stuck low` or `stuck high`, which raises the "stuck" count in the summary. Everything else should match exactly.

## Reading scan output

| Line | Means |
|---|---|
| `X (pin a) -> Y (pin b)` | A jumper from a to b works. |
| A link from the table missing | Bad jumper contact, or the signal path for X or Y: buffer, series resistor, solder. |
| `possible short: A / B -> Y` | Two or more outputs reach the same input (status or data), even when each reads as a clean link. Check the jumpers, then look for a solder bridge. |
| `weak or intermittent: A -> Y?` | The link follows only some of the time: loose jumper, or a cracked joint. |
| `stuck low` on a status pin | The pin is shorted to GND, or U3 or the line's pull-up is faulty. |
| `FAULT: <reason>. All buffers off; unplug to reset.` | Firmware hit an internal error. Report the reason. If it happened during a command, a second line follows: `error: fault (<reason>); unplug to reset`. |
| `scan aborted (partial)` | A fault stopped the scan. The links found so far are listed first, then the FAULT lines. No summary. |

### Blind spots

- Two outputs on one input whose fight reads as 1 look like an unwired input (`stuck high`).
- A control output and a data output on one status pin show as a single link from the control output (it wins).
