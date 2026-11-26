# Bring-up firmware: design

Date: 2026-11-25. Status: approved in conversation, awaiting review of this write-up.

## Purpose

Firmware for the zipbridge board (v0.6, Pico 2 / RP2350) that proves the board works **without a Zip drive**: every LED, every buffer, every DB25 signal in both directions where the hardware allows it. It talks over USB serial with a small command console.

It is the first of three firmware pieces, each with its own spec, plan and implementation:

1. **Bring-up firmware** (this spec).
2. PPA driver plus a serial console that talks to the Zip (nibble mode, `ppa.c` handshakes).
3. USB mass storage (TinyUSB MSC) on top of the driver.

Out of scope here: PIO, the PPA protocol, TinyUSB MSC, EPP.

The boards arrive 2026-12-01. Everything except the real pins must be built and unit-tested on the Mac before then.

## Hardware facts this depends on

From the board's netlist (`build.py`) and BOM:

| GPIO | Signal | DB25 pin | Path |
|---|---|---|---|
| GP0–GP4 | /ACK, BUSY, PE, SELECT, /ERROR (in) | 10, 11, 12, 13, 15 | U3 (LVC245, DIR tied low = cable→Pico), 10k pull-ups R9–R13 to 3V3 on the cable side |
| GP5–GP12 | D0–D7 (bidirectional) | 2–9 | U1 (LVC245), 100Ω series R1–R8 on the cable side |
| GP13–GP16 | /STROBE, /AUTOFD, /INIT, /SELIN (out) | 1, 14, 16, 17 | U2 (LVC245, DIR tied high = Pico→cable) |
| GP17 | U1 DIR (high = Pico→cable) | — | |
| GP18 | U1 OE# (low = enabled) | — | 10k pull-up R14: disabled at boot |
| GP19 | U2 + U3 OE# (low = enabled) | — | 10k pull-up R15: disabled at boot |
| GP20, GP21, GP22 | READ, WRITE, READY LEDs (high = on) | — | |

All three buffers are non-inverting, so a GPIO level equals the DB25 pin level. Signal names with `/` are active-low; the console always shows and takes **pin levels** (0/1), never "asserted".

## Architecture

```
firmware/
  CMakeLists.txt            PICO_BOARD=pico2; outputs zipbridge_bringup.uf2.
                            Falls back to ~/pico/pico-sdk when PICO_SDK_PATH is unset.
  pico_sdk_import.cmake     copied from the SDK
  src/board.h               the pin map above, nothing else
  src/gpio_hal.h            thin GPIO layer: pin direction, pulls, masked put, get, delay
  src/gpio_hal_rp2350.c     the real one (SDK calls); the Mac tests supply a fake
  src/bus.h, bus.c          the parallel-port layer, written only against gpio_hal (reused by the PPA driver)
  src/leds.h, leds.c        READY / READ / WRITE
  src/console.h, console.c  line input, command table, argument parsing, help
  src/scan.h, scan.c        loopback mapping, written only against bus
  src/main.c                init order, main loop
  test/                     native (Mac) build: fake gpio_hal + unit tests, run with ctest
  BRINGUP.md                the Tuesday checklist
```

### Layers

`bus.c` holds all the parallel-port logic, including power-up and direction-change ordering, and calls only `gpio_hal`. So the Mac tests run the **real** `bus.c` over a fake `gpio_hal` that simulates the board (buffers, jumpers, pull-ups) and records every GPIO call in order. `gpio_hal`:

- `hal_set_output(mask, bool)` — pin direction for the pins in `mask`.
- `hal_set_pulls(mask, up, down)`.
- `hal_put_masked(mask, value)` — one SIO write for all pins in `mask`.
- `hal_get_all() -> uint32_t`.
- `hal_delay_us(uint32_t)`, `hal_time_us() -> uint64_t`.

### `bus` interface

What the rest of the code (and later the PPA driver) uses:

- `bus_init()` — power-up sequence below.
- `bus_status_read() -> uint8_t` — 5 status bits (bit 0 = /ACK … bit 4 = /ERROR), raw pin levels.
- `bus_ctrl_write(uint8_t)` / `bus_ctrl_get() -> uint8_t` — 4 control bits (bit 0 = /STROBE, 1 = /AUTOFD, 2 = /INIT, 3 = /SELIN), pin levels, written in one masked write.
- `bus_data_dir_out(uint8_t preload)`, `bus_data_dir_in()`, `bus_data_write(uint8_t)`, `bus_data_read() -> uint8_t`.
- `bus_oe_data(on: bool)`, `bus_oe_ctrl(on: bool)` — buffer enables (`on` = OE# low).
- `bus_safe()` — return to the power-up state (used after `walk`, `pattern`, `scan` and on faults).

### Power-up order (safety)

R14/R15 keep every buffer disabled until firmware runs. `bus_init()` then, in this order:

1. Sets GP18 and GP19 as outputs, driven **high** (buffers stay disabled).
2. Sets the data direction to input (GP17 output, low) and GP5–GP12 as Pico inputs with internal pull-downs (they float while U1 is disabled).
3. Sets control outputs GP13–GP16 to their idle level: /STROBE=1, /AUTOFD=1, /INIT=1, /SELIN=1.
4. Enables U2/U3 (GP19 low). The data buffer U1 stays **disabled** until a command enables it.

### Direction changes (safety)

With U1 enabled and DIR low, U1 drives GP5–GP12 directly (no resistor on the Pico side). So the order matters:

- `bus_data_dir_out(value)`: preload the GP5–GP12 output register with `value`, set GP17 **high**, then make GP5–GP12 Pico outputs. The `dir out` command passes the last `data` value (0 after power-up); `scan` phase 2 passes 0xFF so the data lines start at the rest level.
- `bus_data_dir_in()`: make GP5–GP12 Pico **inputs first**, then set GP17 low.
- `bus_oe_data(off)` also returns the direction to `in`, so a later `oe data on` never starts driving the cable by itself.

Data is only driven onto the cable after explicit `oe data on` and `dir out` (or by `walk`, `pattern` and `scan`, which call `bus_safe()` when they finish or are interrupted).

### LEDs

- READY: slow blink (1 Hz) until a terminal opens the USB serial port, then steady on.
- READ: flashes for at least 30 ms on every `data` read and `status` read.
- WRITE: flashes for at least 30 ms on every data or control write.
- Fault state: if the firmware hits an internal error (an assert in `bus` or `scan`), it turns all three buffers off (U1, U2, U3) and sets the data direction to `in` and blinks READY fast (5 Hz), keeping the console alive to report the error.

## Console

USB CDC serial via the SDK's `pico_stdio_usb` (115200 nominal; the baud rate is ignored over USB). One command per line, case-insensitive, Backspace edits the line. Each reply ends with `ok` or `error: <reason>` followed by the usage line, so output is easy to read and to script. A bad command or argument changes nothing.

| Command | Action |
|---|---|
| `help` | List commands with usage |
| `info` | Firmware version, build date, chip and stepping, buffer enables, data direction, current control and data levels |
| `leds` | READY, READ, WRITE on in turn, 0.5 s each, then back to normal |
| `status` | Read the 5 status pins: `/ACK=1 BUSY=0 PE=0 SELECT=1 /ERROR=1` |
| `ctrl <strobe\|autofd\|init\|selin> <0\|1>` | Set one control pin level |
| `oe <data\|ctrl> <on\|off>` | Enable or disable a buffer (`ctrl` covers U2 and U3) |
| `dir <in\|out>` | Data buffer direction. `out` is refused unless `oe data` is on, to keep the order deliberate. `oe data off` resets it to `in` |
| `data <hex>` | Write a byte to D0–D7. Refused unless direction is `out` |
| `data` | Read D0–D7 (any direction; with `out` it reads back what is driven) |
| `walk [ms]` | Multimeter test. Prints "Remove all loopback jumpers. Enter to start, any other key cancels" and waits. Then enables buffers, sets data out, and raises one output at a time while all others are low: D0…D7, /STROBE, /AUTOFD, /INIT, /SELIN. Prints `D3 = pin 5 high` per step. Default 1000 ms per step, range 100–10000. Any key stops it. Calls `bus_safe()` at the end. |
| `pattern` | Logic analyzer test. Same jumper warning and Enter to start. Line i (D0 = 1 … /SELIN = 12) toggles every i ticks of a 24 kHz timer, so each runs at 12/i kHz, all different: D0 12, D1 6, D2 4, D3 3, D4 2.4, D5 2, D6 1.71, D7 1.5, /STROBE 1.33, /AUTOFD 1.2, /INIT 1.09, /SELIN 1.0 kHz. Runs until any key, then `bus_safe()`. |
| `scan` | Loopback mapper (below) |

## Jumper rules (safety)

The control lines have no series resistor and the data lines have 100Ω, so some wirings can make two outputs fight. `BRINGUP.md`, `help scan` and the start of every `scan` state the rules:

- **Allowed:** an output to an input: data → status, control → status, control → data.
- **Never:** two control pins together (a direct short between U2 outputs), or two data pins together.
- **At most one jumper per input pin.** Two outputs on one pin are in effect wired straight to each other.
- **Remove all jumpers** before `walk` and `pattern`, which hold outputs at different levels for long periods.

`scan` limits the damage if a rule is broken anyway, or a jumper makes poor contact: **every output rests at the same level (1)** in both phases, and only the output under test leaves it, for one sample at a time (set, wait 5 µs, read, put back). So no two outputs are ever at different levels for more than about 10 µs: brief enough to be unlikely to cause damage, even for a direct short between two outputs.

## `scan`: loopback mapper

Maps whatever allowed jumpers are wired on the breakout. It doesn't assume a particular plug.

**Sampling.** Testing an output means 8 samples with the irregular level sequence L H H L H L L H (so a floating input picking up crosstalk is unlikely to match). For each sample: set the output, wait 5 µs, read the inputs, put the output back to its rest level (1). For each (output, input) pair the result is:
- **link:** the input matched the output on all 8 samples;
- **unclear:** the input changed during the test but didn't match on all 8;
- **none:** the input didn't change.

**Phase 1: control → data and status** (data buffer as input, so the data lines can't fight).
Rest levels: all control lines at idle (1). Each of the 4 control lines is tested; the 8 data lines and the 5 status lines are sampled.

**Phase 2: outputs → status** (data buffer as output).
**Skipped, with a message, if phase 1 found any control → data link** (driving the data lines would fight the control buffer through the 100Ω resistors). Rest levels: every output at 1 (data at 1 is harmless against the 10k status pull-ups; control idle is already 1). Each of the 12 outputs (D0–D7, then the 4 control lines) is tested; the 5 status lines are sampled.

**Report.**
- Each link, once, even when both phases find it (a control → status link is found in both): `D3 (pin 5) -> SELECT (pin 13)`.
- A **status** input that is **unclear** for two or more outputs: `possible short: D3 / D4 -> SELECT (pin 13)`. Unclear for exactly one output: `weak or intermittent: D3 -> SELECT?`.
- **Data inputs with no link** (phase 1) are only ever reported as `D6 (pin 8) floating?` (changed) or `D6 (pin 8) stuck low/high` (never changed), never as unclear, weak or short. The data lines have no pull-ups, so unwired ones float.
- An input that never changed over the whole scan (all phases it was sampled in): `/ACK (pin 10) stuck high`. That's normal for an unwired status pin (pull-up). Data lines are only sampled in phase 1, status lines in both; the report covers only what was sampled.
- If phase 2 was skipped: `phase 2 skipped: control -> data jumpers present`.
- A summary line: `scan: N links, M possible shorts, K stuck`.
- `bus_safe()` at the end.

### Recommended loopback passes (in `BRINGUP.md`)

Together these test every output and every input in both directions: data out (A, B), status in (A), control out (B, C), data in (C, D).

| Pass | Jumpers on the breakout | Tests |
|---|---|---|
| A | 2→10, 3→11, 4→12, 5→13, 6→15 | D0–D4 out, all 5 status in |
| B | 7→10, 8→11, 9→12, 1→13, 14→15 | D5–D7 out; /STROBE, /AUTOFD out |
| C | 16→10, 17→11, plus 1→2, 14→3, 16→4, 17→5 | /INIT, /SELIN out (phase 1 reads status); D0–D3 in. Phase 2 is skipped automatically. |
| D | 1→6, 14→7, 16→8, 17→9 | D4–D7 in |

`BRINGUP.md` lists, for every pass, the output `scan` should print. Unwired data lines in passes C and D float, so those lines may show either `floating?` or `stuck`; everything else is exact.

## Error handling

- Commands: argument checks before any pin changes; on failure print `error:` plus usage, and leave the state unchanged.
- Long-running commands (`walk`, `pattern`, `scan`) poll for a keypress at least every 50 ms and always call `bus_safe()` on exit.
- Internal faults: all three buffers off (U1, U2 and U3 disabled), data direction `in`, fast READY blink, message on the console (see LEDs).
- USB disconnect: the firmware keeps running; when the terminal reopens, the prompt reappears.

## Testing

**On the Mac, before the boards arrive** (`cmake -S firmware/test -B firmware/test/build && ctest --test-dir firmware/test/build`), using a host compiler, the real `bus.c`, `scan.c` and `console.c`, and a fake `gpio_hal`:

- **Fake board:** models the three buffers (enable, direction), the status pull-ups, jumpers (output → input links), two outputs shorted on one input (that input reads randomly while they differ), stuck inputs, and a floating data input (reads from a seeded generator, so every run is the same). It records every GPIO call with a fake timestamp.
- **Power-up:** `bus_init` makes its calls in the documented order; no buffer is enabled before the control lines are at idle; U1 stays disabled.
- **Direction changes:** `dir out` sets GP17 before any of GP5–GP12 becomes an output; `dir in` makes them inputs before GP17 goes low; `oe data off` returns the direction to `in`.
- **`scan`:** finds each simulated link in both phases (including control → status in phase 1); reports possible shorts, weak links, floating data inputs and stuck inputs by the rules above; skips phase 2 when a control → data link exists and never drives data out in that case; in both phases no two outputs are at different levels for more than 20 µs (rest levels included); ends in the `bus_safe()` state. Each recommended pass A–D, simulated, produces the output listed in `BRINGUP.md` (unwired data lines: `floating?` or `stuck`). A floating data input is never reported as a short or weak link.
- **Console:** parsing of every command and argument form, case-insensitivity, rejection of bad arguments with no GPIO change, `dir out` refused while `oe data` is off, `data <hex>` refused while the direction is `in`, `walk`/`pattern` don't start without Enter.

**On the board, Tuesday** (`firmware/BRINGUP.md`), in this order, each step with what you should see and what a failure points to:

1. Visual check and power: PWR LED on from USB alone.
2. Flash the UF2 (BOOTSEL), open the serial port, `info`, `leds`.
3. `status` with nothing connected: all 5 read 1 (pull-ups).
4. Multimeter on the DB25 during `walk` (no jumpers): each pin high in turn, all others low.
5. Logic analyzer during `pattern` (no jumpers): every output at its listed frequency, clean edges.
6. Loopback passes A–D with `scan`.

## Build and flash

- Toolchain: Arm GNU Toolchain 15.3 (`brew install --cask gcc-arm-embedded`; installed), CMake + Ninja (installed), pico-sdk 2.3.1 at `~/pico/pico-sdk` (cloned with submodules).
- Build: `cmake -S firmware -B firmware/build -G Ninja && ninja -C firmware/build`. `firmware/build/` and `firmware/test/build/` are gitignored.
- Flash: hold BOOTSEL, plug in USB, copy `zipbridge_bringup.uf2` to the `RP2350` drive. `picotool` is optional.
