# Bring-up Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the zipbridge v0.6 bring-up firmware (USB serial console that exercises every LED, buffer and DB25 signal without a Zip drive), fully unit-tested on the Mac before the boards arrive on 2026-12-01.

**Architecture:** All board logic (`bus`, `leds`, `console`, `scan`, `exercise`) is plain C written against a thin `gpio_hal` and a two-function `io` layer. The firmware links the real RP2350 versions of those two layers. The Mac test build links a fake board, which simulates the buffers, pull-ups, jumpers, shorts and floating lines and records every GPIO call, plus a fake serial port.

**Tech Stack:** C11, Pico SDK 2.3.1 (`~/pico/pico-sdk`), Arm GNU Toolchain 15.3, CMake + Ninja, host clang with ASan/UBSan, ctest.

**Spec:** `docs/superpowers/specs/2026-11-25-bringup-firmware-design.md`. Read it first.

---

## Background the implementer needs

- **Hardware.** A Pico 2 drives a DB25 through three SN74LVC245 buffers. All three are non-inverting, so a GPIO level equals the DB25 pin level.
  - **U1 (data):** DIR=GP17, where high means Pico→cable. OE#=GP18, where low means enabled. 100Ω series resistors on the cable side.
  - **U2 (control out) and U3 (status in):** they share OE#=GP19. U3's cable side has 10k pull-ups to 3V3.
  - **Boot state:** 10k pull-ups on GP18 and GP19 keep every buffer disabled until the firmware runs.
- **The one dangerous condition.** U1 enabled with DIR low *and* GP5–GP12 set as Pico outputs means two chips drive the same wire with no resistor between them. Every direction change is ordered so this never happens. The fake board counts any moment it does happen (`fake_fights`), and every test asserts that the count stays 0.
- **Pin levels, never "asserted".** Names starting with `/` are active-low signals. The console shows and accepts raw 0/1 levels.
- **Deviations from the spec's file list:** small additions for testability and focus.

  | Addition | Why |
  |---|---|
  | `board.c` | Holds the name and pin tables. As `static const` arrays in a header they would trigger unused-variable warnings, which `-Werror` turns into errors. |
  | `io.h` and `io_usb.c` | The serial port, so the console can be tested with a fake port. |
  | `exercise.c` | Holds `walk` and `pattern`, keeping `console.c` focused. |
  | `bus_out_write(data, ctrl)` | Data and control in one masked write; used by `walk`, `pattern` and `scan` phase 2. |
  | `bus_wait_us` | Delay helper, so `scan` uses only `bus`. |
  | Fault getters on `bus` | Let the console and long-running commands see that the board is in the fault state. |
  | `hal_init`, `hal_chip_name` | Pin setup and the chip name shown by `info`. |

- **Fault behaviour** (making the spec's "keeping the console alive" concrete):
  - A fault is sticky until the board is unplugged.
  - Commands that change pins are refused with `error: fault (...)`.
  - `help`, `info`, `status`, `data` (read) and `leds` still work.
- **Erratum RP2350-E9.** GPIO pull-downs can latch near 2V after a pin was driven high. The data pins use pull-downs only so that they don't float mid-rail while U1 is disabled. Their level is meaningless in that state, and nothing reads it.

## File map

```
firmware/
  CMakeLists.txt              firmware build (Task 7)
  pico_sdk_import.cmake       copied from the SDK (Task 7)
  BRINGUP.md                  Tuesday checklist (Task 8)
  src/board.h, board.c        pin map, masks, signal names and DB25 pin tables (Task 1)
  src/gpio_hal.h              GPIO layer interface (Task 1)
  src/gpio_hal_rp2350.c       real GPIO layer (Task 7)
  src/io.h                    serial interface: io_getc, io_write (Task 1)
  src/io_usb.c                real serial over USB CDC (Task 7)
  src/bus.h, bus.c            parallel-port layer (Task 2)
  src/leds.h, leds.c          READY/READ/WRITE (Task 3)
  src/console.h, console.c    line editor, command table, out_printf (Task 4; walk/pattern Task 5; scan Task 6)
  src/exercise.h, exercise.c  walk, pattern (Task 5)
  src/scan.h, scan.c          loopback mapper (Task 6)
  src/main.c                  firmware entry point (Task 7)
  test/CMakeLists.txt         host test build (Task 1, grows each task)
  test/test.h                 CHECK macros
  test/fake_board.h, .c       simulated board + call log (Task 1)
  test/fake_io.h, .c          simulated serial port (Task 1)
  test/test_fake.c, test_bus.c, test_leds.c, test_console.c, test_exercise.c, test_scan.c
```

Test command, used everywhere below (run from the repo root):

```bash
cmake -S firmware/test -B firmware/test/build && cmake --build firmware/test/build && ctest --test-dir firmware/test/build --output-on-failure
```

---

### Task 1: Test harness, pin map, fake board

**Files:**
- Modify: `.gitignore`
- Create: `firmware/src/board.h`, `firmware/src/board.c`, `firmware/src/gpio_hal.h`, `firmware/src/io.h`
- Create: `firmware/test/CMakeLists.txt`, `firmware/test/test.h`, `firmware/test/fake_board.h`, `firmware/test/fake_board.c`, `firmware/test/fake_io.h`, `firmware/test/fake_io.c`
- Test: `firmware/test/test_fake.c`

- [ ] **Step 1: Ignore build folders**

Append to `.gitignore`:

```
# firmware builds
firmware/build/
firmware/test/build/
```

- [ ] **Step 2: Write `firmware/src/board.h`**

```c
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* zipbridge v0.6 pin map. All three LVC245 buffers are non-inverting, so a
 * GPIO level equals the DB25 pin level. */

#define PIN_STATUS_BASE 0u  /* GP0..GP4:  /ACK BUSY PE SELECT /ERROR (in) */
#define PIN_DATA_BASE   5u  /* GP5..GP12: D0..D7 */
#define PIN_CTRL_BASE  13u  /* GP13..GP16: /STROBE /AUTOFD /INIT /SELIN (out) */
#define PIN_U1_DIR     17u  /* data buffer direction, high = Pico -> cable */
#define PIN_U1_OE      18u  /* data buffer OE#, low = enabled (10k pull-up R14) */
#define PIN_U23_OE     19u  /* control + status buffers OE#, low = enabled (R15) */
#define PIN_LED_READ   20u  /* high = on */
#define PIN_LED_WRITE  21u
#define PIN_LED_READY  22u

#define STATUS_COUNT 5u
#define DATA_COUNT   8u
#define CTRL_COUNT   4u

#define STATUS_MASK (0x1Fu << PIN_STATUS_BASE)
#define DATA_MASK   (0xFFu << PIN_DATA_BASE)
#define CTRL_MASK   (0x0Fu << PIN_CTRL_BASE)
#define U1_DIR_MASK (1u << PIN_U1_DIR)
#define U1_OE_MASK  (1u << PIN_U1_OE)
#define U23_OE_MASK (1u << PIN_U23_OE)
#define LED_MASK    ((1u << PIN_LED_READ) | (1u << PIN_LED_WRITE) | (1u << PIN_LED_READY))
#define BUS_MASK    (STATUS_MASK | DATA_MASK | CTRL_MASK | U1_DIR_MASK | U1_OE_MASK | U23_OE_MASK)

/* Index i = bit i of the bus value (status bit 0 = /ACK, ctrl bit 0 = /STROBE). */
extern const char *const STATUS_NAME[STATUS_COUNT];
extern const uint8_t STATUS_DB25[STATUS_COUNT];
extern const char *const DATA_NAME[DATA_COUNT];
extern const uint8_t DATA_DB25[DATA_COUNT];
extern const char *const CTRL_NAME[CTRL_COUNT];
extern const uint8_t CTRL_DB25[CTRL_COUNT];

#endif
```

- [ ] **Step 3: Write `firmware/src/board.c`**

```c
#include "board.h"

const char *const STATUS_NAME[STATUS_COUNT] = {"/ACK", "BUSY", "PE", "SELECT", "/ERROR"};
const uint8_t STATUS_DB25[STATUS_COUNT] = {10, 11, 12, 13, 15};
const char *const DATA_NAME[DATA_COUNT] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"};
const uint8_t DATA_DB25[DATA_COUNT] = {2, 3, 4, 5, 6, 7, 8, 9};
const char *const CTRL_NAME[CTRL_COUNT] = {"/STROBE", "/AUTOFD", "/INIT", "/SELIN"};
const uint8_t CTRL_DB25[CTRL_COUNT] = {1, 14, 16, 17};
```

- [ ] **Step 4: Write `firmware/src/gpio_hal.h` and `firmware/src/io.h`**

```c
#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include <stdbool.h>
#include <stdint.h>

void hal_init(uint32_t mask);                      /* SIO function, input, output latch 0 */
void hal_set_output(uint32_t mask, bool out);      /* pin direction */
void hal_set_pulls(uint32_t mask, bool up, bool down);
void hal_put_masked(uint32_t mask, uint32_t value); /* one write for all pins in mask */
uint32_t hal_get_all(void);
void hal_delay_us(uint32_t us);
uint64_t hal_time_us(void);
const char *hal_chip_name(void);

#endif
```

```c
#ifndef IO_H
#define IO_H

int io_getc(void);            /* next received character, or -1 if none */
void io_write(const char *s); /* send a string */

#endif
```

- [ ] **Step 5: Write `firmware/test/test.h`**

```c
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

extern int test_failures; /* defined once in each test_*.c */

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
    test_failures++; } } while (0)

#define CHECK_CONTAINS(hay, needle) do { if (!strstr((hay), (needle))) { \
    fprintf(stderr, "%s:%d: output lacks \"%s\"\n--- output ---\n%s\n--------------\n", \
            __FILE__, __LINE__, (needle), (hay)); \
    test_failures++; } } while (0)

#define CHECK_LACKS(hay, needle) do { if (strstr((hay), (needle))) { \
    fprintf(stderr, "%s:%d: output has \"%s\"\n--- output ---\n%s\n--------------\n", \
            __FILE__, __LINE__, (needle), (hay)); \
    test_failures++; } } while (0)

#define RUN(t) do { fprintf(stderr, "  %s\n", #t); t(); } while (0)

static inline int count_of(const char *hay, const char *needle) {
    int n = 0;
    size_t len = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + len, needle)) n++;
    return n;
}

static inline int test_report(void) {
    if (test_failures) fprintf(stderr, "%d check(s) failed\n", test_failures);
    return test_failures != 0;
}

#endif
```

- [ ] **Step 6: Write `firmware/test/fake_board.h`**

```c
#ifndef FAKE_BOARD_H
#define FAKE_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* A simulated zipbridge board behind gpio_hal: the three buffers, the status
 * pull-ups, jumpers between DB25 pins, stuck pins, flaky contacts and
 * floating data lines. Every state-changing GPIO call is logged with a fake
 * timestamp (1 us per call, plus delays). Reads are not logged. */

typedef enum { FAKE_INIT, FAKE_SET_OUTPUT, FAKE_SET_PULLS, FAKE_PUT, FAKE_DELAY } fake_op_t;

typedef struct {
    fake_op_t op;
    uint32_t mask;
    uint32_t value; /* PUT: masked value; SET_OUTPUT: 1/0; SET_PULLS: up | down<<1; DELAY: us */
    uint64_t t;     /* fake time after the call, us */
} fake_call_t;

#define FAKE_LOG_MAX 200000
extern fake_call_t fake_log[FAKE_LOG_MAX];
extern int fake_log_len;
extern bool fake_log_overflow;

/* Called after every logged call (NULL = none). */
extern void (*fake_on_op)(const fake_call_t *call);

/* Number of logged calls after which U1 was enabled with DIR = cable->Pico
 * while a data GPIO was a Pico output (two drivers on one wire). Must stay 0. */
extern int fake_fights;
/* Set once U1 drives the cable (enabled, DIR = Pico->cable). */
extern bool fake_data_driven;

void fake_reset(uint32_t seed);  /* power-on state, no wiring, empty log, hooks cleared */
void fake_log_clear(void);
void fake_jumper(int db25_a, int db25_b); /* join two DB25 pins */
void fake_stuck(int db25_pin, int level); /* force a pin's level (e.g. shorted to GND) */
void fake_flaky(int db25_pin);            /* every other read of this pin sees the pull-up (1) */

/* Level on a DB25 pin: 0, 1, -1 = floating, -2 = two drivers disagree. */
int fake_cable_level(int db25_pin);
uint32_t fake_out_value(void);  /* Pico output latch */
uint32_t fake_out_enable(void); /* Pico output-enable bits */
bool fake_u1_enabled(void);
bool fake_u23_enabled(void);

/* Longest time (us) any two driven cable outputs were at different levels. */
uint64_t fake_max_contention_us(void);
void fake_contention_reset(void);

#endif
```

- [ ] **Step 7: Write `firmware/test/fake_io.h`**

```c
#ifndef FAKE_IO_H
#define FAKE_IO_H

/* Simulated serial port behind io.h. Input is a queue the test fills.
 * When the queue is empty, io_getc returns -1, except that after `limit`
 * empty reads in a row it returns ESC (0x1B) once, so loops waiting for a
 * key always end. Output collects into one string. */
void fake_io_reset(void);              /* empty queue and output, limit 1000 */
void fake_io_push(const char *s);
void fake_io_set_idle_limit(int limit);
const char *fake_io_output(void);
void fake_io_clear_output(void);

#endif
```

- [ ] **Step 8: Write the failing test `firmware/test/test_fake.c`**

```c
#include "board.h"
#include "fake_board.h"
#include "gpio_hal.h"
#include "test.h"

int test_failures;

/* Enable U1, U2, U3 by hand; data direction as given. */
static void enable_buffers(int data_out) {
    hal_put_masked(U1_OE_MASK | U23_OE_MASK | U1_DIR_MASK | CTRL_MASK,
                   (data_out ? U1_DIR_MASK : 0) | CTRL_MASK);
    hal_set_output(U1_OE_MASK | U23_OE_MASK | U1_DIR_MASK | CTRL_MASK, true);
}

static void test_power_on(void) {
    fake_reset(1);
    CHECK(!fake_u1_enabled());
    CHECK(!fake_u23_enabled());
    CHECK(fake_cable_level(1) == -1);  /* /STROBE not driven */
    CHECK(fake_cable_level(10) == 1);  /* /ACK has its pull-up */
    CHECK((hal_get_all() & STATUS_MASK) == 0); /* U3 off: Pico pull-downs */
    CHECK((hal_get_all() & (U1_OE_MASK | U23_OE_MASK)) == (U1_OE_MASK | U23_OE_MASK));
}

static void test_status_pullups(void) {
    fake_reset(1);
    enable_buffers(0);
    CHECK((hal_get_all() & STATUS_MASK) == STATUS_MASK);
}

static void test_ctrl_jumper_to_status(void) {
    fake_reset(1);
    fake_jumper(1, 10); /* /STROBE -> /ACK */
    enable_buffers(0);
    hal_put_masked(1u << PIN_CTRL_BASE, 0);
    CHECK(fake_cable_level(1) == 0);
    CHECK((hal_get_all() & 1u) == 0);
    hal_put_masked(1u << PIN_CTRL_BASE, 1u << PIN_CTRL_BASE);
    CHECK((hal_get_all() & 1u) == 1u);
}

static void test_data_out_to_status(void) {
    fake_reset(1);
    fake_jumper(2, 10); /* D0 -> /ACK */
    enable_buffers(1);
    hal_put_masked(DATA_MASK, 0);
    hal_set_output(DATA_MASK, true);
    CHECK(fake_cable_level(2) == 0);
    CHECK((hal_get_all() & 1u) == 0);
    CHECK(fake_data_driven);
    CHECK(fake_fights == 0);
}

static void test_ctrl_to_data_in(void) {
    fake_reset(1);
    fake_jumper(1, 2); /* /STROBE -> D0 */
    enable_buffers(0);
    hal_put_masked(1u << PIN_CTRL_BASE, 0);
    CHECK(((hal_get_all() >> PIN_DATA_BASE) & 1u) == 0);
    hal_put_masked(1u << PIN_CTRL_BASE, 1u << PIN_CTRL_BASE);
    CHECK(((hal_get_all() >> PIN_DATA_BASE) & 1u) == 1u);
}

static void test_floating_data_changes(void) {
    fake_reset(1);
    enable_buffers(0);
    int seen0 = 0, seen1 = 0;
    for (int i = 0; i < 200; i++) {
        if ((hal_get_all() >> PIN_DATA_BASE) & 1u) seen1 = 1; else seen0 = 1;
    }
    CHECK(seen0 && seen1);
}

static void test_fight_counted(void) {
    fake_reset(1);
    enable_buffers(0);               /* U1 drives the Pico side */
    hal_set_output(DATA_MASK, true); /* and so does the Pico: a fight */
    CHECK(fake_fights > 0);
}

static void test_contention_timed(void) {
    fake_reset(1);
    enable_buffers(0);
    hal_put_masked(CTRL_MASK, 0x0Eu << PIN_CTRL_BASE); /* /STROBE 0, others 1 */
    hal_delay_us(30);
    hal_put_masked(CTRL_MASK, CTRL_MASK);
    CHECK(fake_max_contention_us() >= 30);
    fake_contention_reset();
    CHECK(fake_max_contention_us() == 0);
}

static void test_stuck_pin(void) {
    fake_reset(1);
    fake_stuck(11, 0); /* BUSY */
    enable_buffers(0);
    CHECK((hal_get_all() & STATUS_MASK) == 0x1Du);
}

static void test_two_drivers_alternate(void) {
    fake_reset(1);
    fake_jumper(1, 10);
    fake_jumper(14, 10); /* /STROBE and /AUTOFD both on /ACK */
    enable_buffers(0);
    hal_put_masked(CTRL_MASK, 0x0Eu << PIN_CTRL_BASE); /* they disagree */
    CHECK(fake_cable_level(10) == -2);
    uint32_t a = hal_get_all() & 1u, b = hal_get_all() & 1u;
    CHECK(a != b);
}

static void test_flaky_alternates(void) {
    fake_reset(1);
    fake_jumper(1, 10);
    fake_flaky(10);
    enable_buffers(0);
    hal_put_masked(1u << PIN_CTRL_BASE, 0);
    uint32_t a = hal_get_all() & 1u, b = hal_get_all() & 1u;
    CHECK(a != b);
}

static void test_log_records_order(void) {
    fake_reset(1);
    hal_put_masked(CTRL_MASK, CTRL_MASK);
    hal_delay_us(7);
    CHECK(fake_log_len == 2);
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == CTRL_MASK && fake_log[0].value == CTRL_MASK);
    CHECK(fake_log[1].op == FAKE_DELAY && fake_log[1].value == 7);
    CHECK(fake_log[1].t - fake_log[0].t == 7);
}

int main(void) {
    RUN(test_power_on);
    RUN(test_status_pullups);
    RUN(test_ctrl_jumper_to_status);
    RUN(test_data_out_to_status);
    RUN(test_ctrl_to_data_in);
    RUN(test_floating_data_changes);
    RUN(test_fight_counted);
    RUN(test_contention_timed);
    RUN(test_stuck_pin);
    RUN(test_two_drivers_alternate);
    RUN(test_flaky_alternates);
    RUN(test_log_records_order);
    return test_report();
}
```

- [ ] **Step 9: Write `firmware/test/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.13)
project(zipbridge_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

# Firmware sources under test and test programs. Each task adds to these lists.
set(FW_SRC board.c)
set(TESTS fake)

add_compile_options(-Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

list(TRANSFORM FW_SRC PREPEND ${SRC_DIR}/)
add_library(fw_core STATIC ${FW_SRC} fake_board.c fake_io.c)
target_include_directories(fw_core PUBLIC ${SRC_DIR} ${CMAKE_CURRENT_LIST_DIR})

foreach(t ${TESTS})
    add_executable(test_${t} test_${t}.c)
    target_link_libraries(test_${t} fw_core)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
```

- [ ] **Step 10: Run the build and confirm it fails**

Run the test command.
Expected: CMake or link error, because `fake_board.c` and `fake_io.c` don't exist yet.

- [ ] **Step 11: Write `firmware/test/fake_io.c`**

```c
#include "fake_io.h"

#include <string.h>

#include "io.h"

static char in_buf[4096];
static size_t in_head, in_len;
static char out_buf[65536];
static size_t out_len;
static int idle_limit, idle_count;

void fake_io_reset(void) {
    in_head = in_len = 0;
    out_len = 0;
    out_buf[0] = 0;
    idle_limit = 1000;
    idle_count = 0;
}

void fake_io_push(const char *s) {
    size_t n = strlen(s);
    if (n > sizeof in_buf - in_len) n = sizeof in_buf - in_len;
    memcpy(in_buf + in_len, s, n);
    in_len += n;
}

void fake_io_set_idle_limit(int limit) {
    idle_limit = limit;
    idle_count = 0;
}

int io_getc(void) {
    if (in_head < in_len) {
        idle_count = 0;
        return (unsigned char)in_buf[in_head++];
    }
    if (++idle_count > idle_limit) {
        idle_count = 0;
        return 0x1B;
    }
    return -1;
}

void io_write(const char *s) {
    size_t n = strlen(s);
    if (n > sizeof out_buf - 1 - out_len) n = sizeof out_buf - 1 - out_len;
    memcpy(out_buf + out_len, s, n);
    out_len += n;
    out_buf[out_len] = 0;
}

const char *fake_io_output(void) { return out_buf; }

void fake_io_clear_output(void) {
    out_len = 0;
    out_buf[0] = 0;
}
```

- [ ] **Step 12: Write `firmware/test/fake_board.c`**

```c
#include "fake_board.h"

#include "board.h"
#include "gpio_hal.h"

fake_call_t fake_log[FAKE_LOG_MAX];
int fake_log_len;
bool fake_log_overflow;
void (*fake_on_op)(const fake_call_t *call);
int fake_fights;
bool fake_data_driven;

#define NPINS 26 /* DB25 pins 1..25; index 0 unused */
#define LEVEL_CONTENDED 2

static uint32_t oe, out, pull_up; /* Pico: output enables, output latch, pull-ups */
static uint64_t now;
static int parent[NPINS];      /* jumper nets (union-find) */
static int stuck[NPINS];       /* -1 = not stuck */
static int float_level[NPINS]; /* current level of a floating input */
static bool flaky[NPINS];
static unsigned flaky_count[NPINS];
static unsigned contend_count;
static uint32_t rng;
static bool in_cont;
static uint64_t cont_start, cont_max;

static uint32_t rng_next(void) { /* xorshift32 */
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int find(int p) {
    while (parent[p] != p) p = parent[p];
    return p;
}

static bool is_output(unsigned g) { return (oe >> g) & 1u; }

/* What the Pico itself puts on GPIO g: its output, else its pull (pull-down or none = 0). */
static int pico_level(unsigned g) {
    if (is_output(g)) return (int)((out >> g) & 1u);
    return (int)((pull_up >> g) & 1u);
}

bool fake_u1_enabled(void) { return is_output(PIN_U1_OE) && !((out >> PIN_U1_OE) & 1u); }
bool fake_u23_enabled(void) { return is_output(PIN_U23_OE) && !((out >> PIN_U23_OE) & 1u); }
static bool u1_dir_out(void) { return is_output(PIN_U1_DIR) && ((out >> PIN_U1_DIR) & 1u); }

/* Level a buffer drives onto DB25 pin p, or -1 if nothing drives it. */
static int driven(int p) {
    for (unsigned k = 0; k < CTRL_COUNT; k++)
        if (CTRL_DB25[k] == p) return fake_u23_enabled() ? pico_level(PIN_CTRL_BASE + k) : -1;
    for (unsigned k = 0; k < DATA_COUNT; k++)
        if (DATA_DB25[k] == p)
            return (fake_u1_enabled() && u1_dir_out()) ? pico_level(PIN_DATA_BASE + k) : -1;
    return -1;
}

static bool is_status_pin(int p) {
    for (unsigned k = 0; k < STATUS_COUNT; k++)
        if (STATUS_DB25[k] == p) return true;
    return false;
}

/* 0, 1, -1 = floating, LEVEL_CONTENDED = drivers disagree. */
static int net_level(int p) {
    int root = find(p), lv = -1;
    bool pulled = false;
    for (int q = 1; q < NPINS; q++) {
        if (find(q) != root) continue;
        if (stuck[q] >= 0) return stuck[q];
        if (is_status_pin(q)) pulled = true;
        int d = driven(q);
        if (d < 0) continue;
        if (lv < 0) lv = d;
        else if (lv != d) return LEVEL_CONTENDED;
    }
    if (lv >= 0) return lv;
    return pulled ? 1 : -1;
}

/* What a buffer input on DB25 pin p reads. */
static int read_cable(int p) {
    int lv = net_level(p);
    if (lv == LEVEL_CONTENDED) {
        lv = (int)(contend_count++ & 1u); /* mid-level: alternates */
    } else if (lv < 0) {
        if ((rng_next() & 7u) == 0) float_level[p] ^= 1; /* drifts now and then */
        lv = float_level[p];
    }
    if (flaky[p] && (flaky_count[p]++ & 1u) == 0) lv = 1; /* contact open: pull-up */
    return lv;
}

static void after_op(fake_op_t op, uint32_t mask, uint32_t value) {
    fake_call_t call = {op, mask, value, now};
    if (fake_log_len < FAKE_LOG_MAX) fake_log[fake_log_len++] = call;
    else fake_log_overflow = true;

    if (fake_u1_enabled() && !u1_dir_out() && (oe & DATA_MASK)) fake_fights++;
    if (fake_u1_enabled() && u1_dir_out()) fake_data_driven = true;

    bool any0 = false, any1 = false;
    for (unsigned k = 0; k < CTRL_COUNT + DATA_COUNT; k++) {
        int d = driven(k < CTRL_COUNT ? CTRL_DB25[k] : DATA_DB25[k - CTRL_COUNT]);
        if (d == 0) any0 = true;
        if (d == 1) any1 = true;
    }
    bool c = any0 && any1;
    if (c && !in_cont) {
        in_cont = true;
        cont_start = now;
    } else if (!c && in_cont) {
        in_cont = false;
        if (now - cont_start > cont_max) cont_max = now - cont_start;
    }
    if (fake_on_op) fake_on_op(&call);
}

void fake_reset(uint32_t seed) {
    oe = out = pull_up = 0; /* RP2350 reset: inputs with pull-downs */
    now = 0;
    rng = seed ? seed : 1;
    for (int p = 0; p < NPINS; p++) {
        parent[p] = p;
        stuck[p] = -1;
        flaky[p] = false;
        flaky_count[p] = 0;
        float_level[p] = (int)(rng_next() & 1u);
    }
    contend_count = 0;
    in_cont = false;
    cont_start = cont_max = 0;
    fake_log_clear();
    fake_on_op = 0;
    fake_fights = 0;
    fake_data_driven = false;
}

void fake_log_clear(void) {
    fake_log_len = 0;
    fake_log_overflow = false;
}

void fake_jumper(int a, int b) { parent[find(a)] = find(b); }
void fake_stuck(int p, int level) { stuck[p] = level; }
void fake_flaky(int p) { flaky[p] = true; }

int fake_cable_level(int p) {
    int lv = net_level(p);
    return lv == LEVEL_CONTENDED ? -2 : lv;
}

uint32_t fake_out_value(void) { return out; }
uint32_t fake_out_enable(void) { return oe; }

uint64_t fake_max_contention_us(void) {
    uint64_t m = cont_max;
    if (in_cont && now - cont_start > m) m = now - cont_start;
    return m;
}

void fake_contention_reset(void) {
    cont_max = 0;
    if (in_cont) cont_start = now;
}

/* ---- gpio_hal ---- */

void hal_init(uint32_t mask) {
    now++;
    oe &= ~mask;
    out &= ~mask;
    after_op(FAKE_INIT, mask, 0);
}

void hal_set_output(uint32_t mask, bool o) {
    now++;
    if (o) oe |= mask; else oe &= ~mask;
    after_op(FAKE_SET_OUTPUT, mask, o ? 1u : 0u);
}

void hal_set_pulls(uint32_t mask, bool up, bool down) {
    now++;
    if (up) pull_up |= mask; else pull_up &= ~mask;
    after_op(FAKE_SET_PULLS, mask, (up ? 1u : 0u) | (down ? 2u : 0u));
}

void hal_put_masked(uint32_t mask, uint32_t value) {
    now++;
    out = (out & ~mask) | (value & mask);
    after_op(FAKE_PUT, mask, value & mask);
}

uint32_t hal_get_all(void) {
    now++;
    uint32_t v = 0;
    for (unsigned g = 0; g < 30; g++) {
        int lv = pico_level(g);
        if (!is_output(g)) {
            if (g < PIN_STATUS_BASE + STATUS_COUNT) { /* PIN_STATUS_BASE is 0 */
                if (fake_u23_enabled()) lv = read_cable(STATUS_DB25[g - PIN_STATUS_BASE]);
            } else if (g >= PIN_DATA_BASE && g < PIN_DATA_BASE + DATA_COUNT) {
                if (fake_u1_enabled() && !u1_dir_out()) lv = read_cable(DATA_DB25[g - PIN_DATA_BASE]);
            } else if (g == PIN_U1_OE || g == PIN_U23_OE) {
                lv = 1; /* R14 / R15 */
            }
        }
        if (lv) v |= 1u << g;
    }
    return v;
}

void hal_delay_us(uint32_t us) {
    now += us;
    after_op(FAKE_DELAY, 0, us);
}

uint64_t hal_time_us(void) { return now++; }

const char *hal_chip_name(void) { return "fake RP2350"; }
```

- [ ] **Step 13: Run the tests and confirm they pass**

Run the test command.
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 14: Commit**

```bash
git add .gitignore firmware/src/board.h firmware/src/board.c firmware/src/gpio_hal.h firmware/src/io.h firmware/test
git commit -m "Firmware: host test harness and simulated board"
```

---

### Task 2: `bus` layer

**Files:**
- Create: `firmware/src/bus.h`, `firmware/src/bus.c`
- Test: `firmware/test/test_bus.c`
- Modify: `firmware/test/CMakeLists.txt`. Change the two list lines to `set(FW_SRC board.c bus.c)` and `set(TESTS fake bus)`.

- [ ] **Step 1: Write `firmware/src/bus.h`**

```c
#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

/* Parallel-port layer. All values are pin levels (0/1), never "asserted".
 * Status bit 0 = /ACK .. bit 4 = /ERROR. Ctrl bit 0 = /STROBE, 1 = /AUTOFD,
 * 2 = /INIT, 3 = /SELIN. */

typedef void (*bus_fault_fn)(const char *why);

void bus_init(void);  /* power-up sequence; U2/U3 on, U1 off, data in, ctrl idle (1) */
void bus_safe(void);  /* back to the power-up state (no-op once faulted) */

uint8_t bus_status_read(void);
void bus_ctrl_write(uint8_t levels);
uint8_t bus_ctrl_get(void);

void bus_data_dir_out(uint8_t preload); /* preload, DIR high, then Pico outputs */
void bus_data_dir_in(void);             /* Pico inputs, then DIR low */
void bus_data_write(uint8_t v);         /* faults if direction is in */
uint8_t bus_data_read(void);
void bus_out_write(uint8_t data, uint8_t ctrl); /* both in one write; faults if direction is in */

void bus_oe_data(bool on); /* off also returns the direction to in */
void bus_oe_ctrl(bool on);

bool bus_oe_data_on(void);
bool bus_oe_ctrl_on(void);
bool bus_data_is_out(void);

void bus_wait_us(uint32_t us);

/* Fault: all three buffers off, direction in, handler called once. Sticky
 * until bus_init (power cycle); bus calls that change pins do nothing. */
void bus_set_fault_handler(bus_fault_fn fn);
bool bus_faulted(void);
const char *bus_fault_reason(void);

#endif
```

- [ ] **Step 2: Write the failing test `firmware/test/test_bus.c`**

```c
#include "board.h"
#include "bus.h"
#include "fake_board.h"
#include "gpio_hal.h"
#include "test.h"

int test_failures;

static bool init_ok;

/* U1 must stay off during init; U2/U3 only once all control lines are idle outputs. */
static void init_invariant(const fake_call_t *c) {
    (void)c;
    if (fake_u1_enabled()) init_ok = false;
    if (fake_u23_enabled() &&
        ((fake_out_enable() & CTRL_MASK) != CTRL_MASK || (fake_out_value() & CTRL_MASK) != CTRL_MASK))
        init_ok = false;
}

static void test_init_order(void) {
    fake_reset(1);
    init_ok = true;
    fake_on_op = init_invariant;
    bus_init();
    fake_on_op = 0;
    CHECK(init_ok);
    CHECK(fake_u23_enabled());
    CHECK(!fake_u1_enabled());
    CHECK(fake_fights == 0);

    const fake_call_t want[] = {
        {FAKE_INIT, BUS_MASK, 0, 0},
        {FAKE_PUT, U1_OE_MASK | U23_OE_MASK, U1_OE_MASK | U23_OE_MASK, 0},
        {FAKE_SET_OUTPUT, U1_OE_MASK | U23_OE_MASK, 1, 0},
        {FAKE_PUT, U1_DIR_MASK, 0, 0},
        {FAKE_SET_OUTPUT, U1_DIR_MASK, 1, 0},
        {FAKE_SET_OUTPUT, DATA_MASK, 0, 0},
        {FAKE_SET_PULLS, DATA_MASK, 2, 0},
        {FAKE_SET_PULLS, STATUS_MASK, 1, 0},
        {FAKE_PUT, CTRL_MASK, CTRL_MASK, 0},
        {FAKE_SET_OUTPUT, CTRL_MASK, 1, 0},
        {FAKE_PUT, U23_OE_MASK, 0, 0},
    };
    int n = (int)(sizeof want / sizeof want[0]);
    CHECK(fake_log_len == n);
    for (int i = 0; i < n && i < fake_log_len; i++) {
        if (fake_log[i].op != want[i].op || fake_log[i].mask != want[i].mask ||
            fake_log[i].value != want[i].value) {
            fprintf(stderr, "    init call %d differs\n", i);
            test_failures++;
        }
    }
    CHECK(bus_status_read() == 0x1F); /* nothing connected: pull-ups */
    CHECK(bus_ctrl_get() == 0x0F);
    CHECK(!bus_oe_data_on() && bus_oe_ctrl_on() && !bus_data_is_out());
}

static void test_dir_out_order(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    fake_log_clear();
    bus_data_dir_out(0xA5);
    CHECK(fake_fights == 0);
    CHECK(fake_log_len == 3);
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == DATA_MASK &&
          fake_log[0].value == (0xA5u << PIN_DATA_BASE));
    CHECK(fake_log[1].op == FAKE_PUT && fake_log[1].mask == U1_DIR_MASK && fake_log[1].value == U1_DIR_MASK);
    CHECK(fake_log[2].op == FAKE_SET_OUTPUT && fake_log[2].mask == DATA_MASK && fake_log[2].value == 1);
    CHECK(fake_cable_level(2) == 1 && fake_cable_level(3) == 0 && fake_cable_level(9) == 1);
    CHECK(bus_data_is_out());
}

static void test_dir_in_order(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0x00);
    fake_log_clear();
    bus_data_dir_in();
    CHECK(fake_fights == 0);
    CHECK(fake_log_len == 2);
    CHECK(fake_log[0].op == FAKE_SET_OUTPUT && fake_log[0].mask == DATA_MASK && fake_log[0].value == 0);
    CHECK(fake_log[1].op == FAKE_PUT && fake_log[1].mask == U1_DIR_MASK && fake_log[1].value == 0);
    CHECK(!bus_data_is_out());
}

static void test_oe_data_off_resets_dir(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0xFF);
    bus_oe_data(false);
    CHECK(!fake_u1_enabled());
    CHECK(!bus_data_is_out());
    CHECK((fake_out_enable() & DATA_MASK) == 0);
    bus_oe_data(true);                /* must not start driving the cable */
    CHECK(fake_cable_level(2) == -1);
    CHECK(fake_fights == 0);
}

static void test_ctrl_write_one_put(void) {
    fake_reset(1);
    bus_init();
    fake_log_clear();
    bus_ctrl_write(0x05); /* /STROBE 1, /AUTOFD 0, /INIT 1, /SELIN 0 */
    CHECK(fake_log_len == 1 && fake_log[0].mask == CTRL_MASK);
    CHECK(bus_ctrl_get() == 0x05);
    CHECK(fake_cable_level(1) == 1 && fake_cable_level(14) == 0);
    CHECK(fake_cable_level(16) == 1 && fake_cable_level(17) == 0);
}

static void test_status_read_bits(void) {
    fake_reset(1);
    bus_init();
    fake_stuck(11, 0); /* BUSY */
    CHECK(bus_status_read() == 0x1D);
}

static void test_data_read_in(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    for (unsigned k = 0; k < DATA_COUNT; k++) fake_stuck(DATA_DB25[k], (0x3C >> k) & 1);
    CHECK(bus_data_read() == 0x3C);
}

static void test_out_write_one_put(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0);
    fake_log_clear();
    bus_out_write(0x81, 0x02);
    CHECK(fake_log_len == 1 && fake_log[0].mask == (DATA_MASK | CTRL_MASK));
    CHECK(fake_cable_level(2) == 1 && fake_cable_level(3) == 0 && fake_cable_level(9) == 1);
    CHECK(fake_cable_level(1) == 0 && fake_cable_level(14) == 1);
    CHECK(bus_ctrl_get() == 0x02);
}

static int fault_calls;
static void on_fault(const char *why) { (void)why; fault_calls++; }

static void test_fault_on_write_while_in(void) {
    fake_reset(1);
    bus_init();
    fault_calls = 0;
    bus_set_fault_handler(on_fault);
    bus_oe_data(true);
    bus_data_write(0x55);
    CHECK(bus_faulted());
    CHECK(fault_calls == 1);
    CHECK(!fake_u1_enabled() && !fake_u23_enabled());
    CHECK(!bus_data_is_out());
    CHECK(strlen(bus_fault_reason()) > 0);
    bus_safe();                    /* stays off */
    bus_oe_ctrl(true);
    CHECK(!fake_u23_enabled());
    bus_data_write(0x55);          /* no second report */
    CHECK(fault_calls == 1);
    bus_set_fault_handler(0);
    bus_init();                    /* power cycle clears it */
    CHECK(!bus_faulted());
}

static void test_safe(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0xFF);
    bus_ctrl_write(0);
    bus_oe_ctrl(false);
    bus_safe();
    CHECK(!fake_u1_enabled() && fake_u23_enabled());
    CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
    CHECK(fake_cable_level(1) == 1 && fake_cable_level(17) == 1);
    CHECK(fake_fights == 0);
}

int main(void) {
    RUN(test_init_order);
    RUN(test_dir_out_order);
    RUN(test_dir_in_order);
    RUN(test_oe_data_off_resets_dir);
    RUN(test_ctrl_write_one_put);
    RUN(test_status_read_bits);
    RUN(test_data_read_in);
    RUN(test_out_write_one_put);
    RUN(test_fault_on_write_while_in);
    RUN(test_safe);
    return test_report();
}
```

- [ ] **Step 3: Update `firmware/test/CMakeLists.txt` lists, run, confirm failure**

Set `set(FW_SRC board.c bus.c)` and `set(TESTS fake bus)`. Run the test command.
Expected: CMake error, because `bus.c` doesn't exist.

- [ ] **Step 4: Write `firmware/src/bus.c`**

```c
#include "bus.h"

#include "board.h"
#include "gpio_hal.h"

static bool oe_data, oe_ctrl, data_out, faulted;
static const char *fault_why;
static bus_fault_fn fault_fn;
static uint8_t ctrl_level;

static void fault(const char *why) {
    if (faulted) return;
    faulted = true;
    fault_why = why;
    hal_put_masked(U1_OE_MASK | U23_OE_MASK, U1_OE_MASK | U23_OE_MASK); /* all buffers off */
    hal_set_output(DATA_MASK, false);
    hal_put_masked(U1_DIR_MASK, 0);
    oe_data = oe_ctrl = data_out = false;
    if (fault_fn) fault_fn(why);
}

void bus_set_fault_handler(bus_fault_fn fn) { fault_fn = fn; }
bool bus_faulted(void) { return faulted; }
const char *bus_fault_reason(void) { return faulted ? fault_why : ""; }

void bus_init(void) {
    oe_data = oe_ctrl = data_out = faulted = false;
    fault_why = 0;
    hal_init(BUS_MASK); /* all inputs: R14/R15 keep the buffers off */

    /* 1. Buffer enables become outputs, driven high: still off. */
    hal_put_masked(U1_OE_MASK | U23_OE_MASK, U1_OE_MASK | U23_OE_MASK);
    hal_set_output(U1_OE_MASK | U23_OE_MASK, true);

    /* 2. Data direction in; data pins Pico inputs with pull-downs. */
    hal_put_masked(U1_DIR_MASK, 0);
    hal_set_output(U1_DIR_MASK, true);
    hal_set_output(DATA_MASK, false);
    hal_set_pulls(DATA_MASK, false, true);
    hal_set_pulls(STATUS_MASK, true, false);

    /* 3. Control lines at idle (all 1). */
    ctrl_level = 0x0F;
    hal_put_masked(CTRL_MASK, CTRL_MASK);
    hal_set_output(CTRL_MASK, true);

    /* 4. Enable U2/U3. U1 stays off. */
    hal_put_masked(U23_OE_MASK, 0);
    oe_ctrl = true;
}

void bus_safe(void) {
    if (faulted) return;
    bus_oe_data(false);
    bus_ctrl_write(0x0F);
    bus_oe_ctrl(true);
}

uint8_t bus_status_read(void) {
    return (uint8_t)((hal_get_all() >> PIN_STATUS_BASE) & 0x1Fu);
}

void bus_ctrl_write(uint8_t levels) {
    if (faulted) return;
    ctrl_level = levels & 0x0Fu;
    hal_put_masked(CTRL_MASK, (uint32_t)ctrl_level << PIN_CTRL_BASE);
}

uint8_t bus_ctrl_get(void) { return ctrl_level; }

void bus_data_dir_out(uint8_t preload) {
    if (faulted) return;
    hal_put_masked(DATA_MASK, (uint32_t)preload << PIN_DATA_BASE);
    hal_put_masked(U1_DIR_MASK, U1_DIR_MASK); /* U1 stops driving the Pico side */
    hal_set_output(DATA_MASK, true);          /* only now does the Pico drive */
    data_out = true;
}

void bus_data_dir_in(void) {
    if (faulted) return;
    hal_set_output(DATA_MASK, false); /* Pico lets go first */
    hal_put_masked(U1_DIR_MASK, 0);   /* then U1 drives the Pico side */
    data_out = false;
}

void bus_data_write(uint8_t v) {
    if (faulted) return;
    if (!data_out) {
        fault("data write while the data direction is in");
        return;
    }
    hal_put_masked(DATA_MASK, (uint32_t)v << PIN_DATA_BASE);
}

uint8_t bus_data_read(void) { return (uint8_t)(hal_get_all() >> PIN_DATA_BASE); }

void bus_out_write(uint8_t data, uint8_t ctrl) {
    if (faulted) return;
    if (!data_out) {
        fault("data write while the data direction is in");
        return;
    }
    ctrl_level = ctrl & 0x0Fu;
    hal_put_masked(DATA_MASK | CTRL_MASK,
                   ((uint32_t)data << PIN_DATA_BASE) | ((uint32_t)ctrl_level << PIN_CTRL_BASE));
}

void bus_oe_data(bool on) {
    if (faulted) return;
    if (on) {
        hal_put_masked(U1_OE_MASK, 0);
        oe_data = true;
    } else {
        hal_put_masked(U1_OE_MASK, U1_OE_MASK);
        oe_data = false;
        bus_data_dir_in();
    }
}

void bus_oe_ctrl(bool on) {
    if (faulted) return;
    hal_put_masked(U23_OE_MASK, on ? 0 : U23_OE_MASK);
    oe_ctrl = on;
}

bool bus_oe_data_on(void) { return oe_data; }
bool bus_oe_ctrl_on(void) { return oe_ctrl; }
bool bus_data_is_out(void) { return data_out; }

void bus_wait_us(uint32_t us) { hal_delay_us(us); }
```

- [ ] **Step 5: Run the tests and confirm they pass**

Run the test command.
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/bus.h firmware/src/bus.c firmware/test/test_bus.c firmware/test/CMakeLists.txt
git commit -m "Firmware: bus layer with safe power-up and direction order"
```

---

### Task 3: LEDs

**Files:**
- Create: `firmware/src/leds.h`, `firmware/src/leds.c`
- Test: `firmware/test/test_leds.c`
- Modify: `firmware/test/CMakeLists.txt`. Change the lists to `set(FW_SRC board.c bus.c leds.c)` and `set(TESTS fake bus leds)`.

- [ ] **Step 1: Write `firmware/src/leds.h`**

```c
#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>

typedef enum {
    LEDS_READY_WAITING, /* 1 Hz blink: no terminal yet */
    LEDS_READY_ON,      /* steady: terminal open */
    LEDS_READY_FAULT,   /* 5 Hz blink; sticky until leds_init */
} leds_ready_t;

#define LED_NONE  (-1)
#define LED_READY 0
#define LED_READ  1
#define LED_WRITE 2

void leds_init(void);
void leds_poll(void); /* call often: updates blink and flash timing */
void leds_set_ready(leds_ready_t mode);
leds_ready_t leds_ready_mode(void);
void leds_flash_read(void);  /* READ on for at least 30 ms */
void leds_flash_write(void); /* WRITE on for at least 30 ms */
void leds_override(int led); /* only this LED on (LED_NONE = normal) */

#endif
```

- [ ] **Step 2: Write the failing test `firmware/test/test_leds.c`**

```c
#include "board.h"
#include "fake_board.h"
#include "gpio_hal.h"
#include "leds.h"
#include "test.h"

int test_failures;

static bool led(unsigned pin) { return (fake_out_value() >> pin) & 1u; }

static void setup(void) {
    fake_reset(1);
    leds_init();
}

static void test_init_outputs(void) {
    setup();
    CHECK((fake_out_enable() & LED_MASK) == LED_MASK);
    CHECK(!led(PIN_LED_READ) && !led(PIN_LED_WRITE));
}

static void test_ready_blinks_1hz_while_waiting(void) {
    setup();
    CHECK(led(PIN_LED_READY));
    hal_delay_us(600000);
    leds_poll();
    CHECK(!led(PIN_LED_READY));
    hal_delay_us(500000);
    leds_poll();
    CHECK(led(PIN_LED_READY));
}

static void test_ready_steady_when_connected(void) {
    setup();
    leds_set_ready(LEDS_READY_ON);
    for (int i = 0; i < 30; i++) {
        hal_delay_us(50000);
        leds_poll();
        CHECK(led(PIN_LED_READY));
    }
}

static void test_fault_blinks_5hz_and_sticks(void) {
    setup();
    leds_set_ready(LEDS_READY_FAULT);
    int changes = 0;
    bool last = led(PIN_LED_READY);
    for (int i = 0; i < 100; i++) { /* 1 s in 10 ms steps */
        hal_delay_us(10000);
        leds_poll();
        if (led(PIN_LED_READY) != last) { changes++; last = !last; }
    }
    CHECK(changes >= 9 && changes <= 11);
    leds_set_ready(LEDS_READY_ON);
    CHECK(leds_ready_mode() == LEDS_READY_FAULT);
}

static void test_flash_min_30ms(void) {
    setup();
    leds_flash_read();
    CHECK(led(PIN_LED_READ));
    hal_delay_us(29000);
    leds_poll();
    CHECK(led(PIN_LED_READ));
    hal_delay_us(2000);
    leds_poll();
    CHECK(!led(PIN_LED_READ));

    leds_flash_write();
    CHECK(led(PIN_LED_WRITE));
    hal_delay_us(29000);
    leds_poll();
    CHECK(led(PIN_LED_WRITE));
    hal_delay_us(2000);
    leds_poll();
    CHECK(!led(PIN_LED_WRITE));
}

static void test_override(void) {
    setup();
    leds_set_ready(LEDS_READY_ON);
    leds_override(LED_WRITE);
    CHECK(led(PIN_LED_WRITE) && !led(PIN_LED_READY) && !led(PIN_LED_READ));
    leds_override(LED_READ);
    CHECK(led(PIN_LED_READ) && !led(PIN_LED_READY) && !led(PIN_LED_WRITE));
    leds_override(LED_NONE);
    CHECK(led(PIN_LED_READY) && !led(PIN_LED_READ));
}

int main(void) {
    RUN(test_init_outputs);
    RUN(test_ready_blinks_1hz_while_waiting);
    RUN(test_ready_steady_when_connected);
    RUN(test_fault_blinks_5hz_and_sticks);
    RUN(test_flash_min_30ms);
    RUN(test_override);
    return test_report();
}
```

- [ ] **Step 3: Update the CMake lists, run, confirm failure**

Run the test command. Expected: build error, because `leds.c` doesn't exist.

- [ ] **Step 4: Write `firmware/src/leds.c`**

```c
#include "leds.h"

#include <stdint.h>

#include "board.h"
#include "gpio_hal.h"

#define FLASH_US 30000u

static leds_ready_t mode;
static uint64_t read_until, write_until;
static int override_led;
static uint32_t shown; /* last value written, to skip redundant writes */

void leds_init(void) {
    mode = LEDS_READY_WAITING;
    read_until = write_until = 0;
    override_led = LED_NONE;
    shown = 0xFFFFFFFFu;
    hal_init(LED_MASK);
    hal_set_output(LED_MASK, true);
    leds_poll();
}

void leds_poll(void) {
    static const unsigned pin[3] = {PIN_LED_READY, PIN_LED_READ, PIN_LED_WRITE};
    uint64_t now = hal_time_us();
    uint32_t v = 0;
    if (override_led != LED_NONE) {
        v = 1u << pin[override_led];
    } else {
        bool ready = mode == LEDS_READY_ON ||
                     (mode == LEDS_READY_WAITING && (now / 500000u) % 2u == 0) ||
                     (mode == LEDS_READY_FAULT && (now / 100000u) % 2u == 0);
        if (ready) v |= 1u << PIN_LED_READY;
        if (now < read_until) v |= 1u << PIN_LED_READ;
        if (now < write_until) v |= 1u << PIN_LED_WRITE;
    }
    if (v != shown) {
        hal_put_masked(LED_MASK, v);
        shown = v;
    }
}

void leds_set_ready(leds_ready_t m) {
    if (mode != LEDS_READY_FAULT) mode = m;
    leds_poll();
}

leds_ready_t leds_ready_mode(void) { return mode; }

void leds_flash_read(void) {
    read_until = hal_time_us() + FLASH_US;
    leds_poll();
}

void leds_flash_write(void) {
    write_until = hal_time_us() + FLASH_US;
    leds_poll();
}

void leds_override(int led) {
    override_led = (led >= LED_READY && led <= LED_WRITE) ? led : LED_NONE;
    leds_poll();
}
```

- [ ] **Step 5: Run the tests and confirm they pass**

Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/leds.h firmware/src/leds.c firmware/test/test_leds.c firmware/test/CMakeLists.txt
git commit -m "Firmware: READY/READ/WRITE LEDs"
```

---

### Task 4: Console core and the pin commands

Covers `help`, `info`, `leds`, `status`, `ctrl`, `oe`, `dir` and `data`, plus line editing and fault reporting. Tasks 5 and 6 add `walk`, `pattern` and `scan`.

**Files:**
- Create: `firmware/src/console.h`, `firmware/src/console.c`
- Test: `firmware/test/test_console.c`
- Modify: `firmware/test/CMakeLists.txt`. Change the lists to `set(FW_SRC board.c bus.c leds.c console.c)` and `set(TESTS fake bus leds console)`.

- [ ] **Step 1: Write `firmware/src/console.h`**

```c
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

#define FW_VERSION "0.1"

void console_init(void);     /* also installs the bus fault handler */
void console_banner(void);   /* on terminal connect: banner and prompt */
void console_feed(int c);    /* one received character */
void console_exec(char *line); /* run one command line (modified in place) */

/* For long-running commands. A LF right after a CR (one Enter key on many
 * terminals) is skipped. */
int console_poll_key(void);    /* -1 if no key */
bool console_wait_enter(void); /* true = Enter, false = any other key */

void out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
```

- [ ] **Step 2: Write the failing test `firmware/test/test_console.c`**

```c
#include "board.h"
#include "bus.h"
#include "console.h"
#include "fake_board.h"
#include "fake_io.h"
#include "leds.h"
#include "test.h"

int test_failures;

static void setup(void) {
    fake_reset(1);
    fake_io_reset();
    leds_init();
    bus_init();
    console_init();
    fake_log_clear();
}

static void feed(const char *s) {
    for (; *s; s++) console_feed((unsigned char)*s);
}

/* Type one line (CR-terminated); returns everything printed. */
static const char *run(const char *line) {
    fake_io_clear_output();
    feed(line);
    console_feed('\r');
    return fake_io_output();
}

static int data_cable(void) {
    int v = 0;
    for (unsigned k = 0; k < DATA_COUNT; k++) {
        int lv = fake_cable_level(DATA_DB25[k]);
        if (lv < 0) return -1;
        v |= lv << k;
    }
    return v;
}

static void test_help_lists_commands(void) {
    setup();
    const char *out = run("help");
    CHECK_CONTAINS(out, "ctrl <strobe|autofd|init|selin> <0|1>");
    CHECK_CONTAINS(out, "oe <data|ctrl> <on|off>");
    CHECK_CONTAINS(out, "dir <in|out>");
    CHECK_CONTAINS(out, "data [<hex>]");
    CHECK_CONTAINS(out, "status");
    CHECK_CONTAINS(out, "info");
    CHECK_CONTAINS(out, "leds");
    CHECK_CONTAINS(out, "ok\n");
}

static void test_status_case_insensitive(void) {
    setup();
    const char *out = run("STATUS");
    CHECK_CONTAINS(out, "/ACK=1 BUSY=1 PE=1 SELECT=1 /ERROR=1\n");
    CHECK_CONTAINS(out, "ok\n");
}

static void test_status_shows_levels(void) {
    setup();
    fake_stuck(12, 0); /* PE */
    CHECK_CONTAINS(run("status"), "/ACK=1 BUSY=1 PE=0 SELECT=1 /ERROR=1\n");
}

static void test_unknown_command(void) {
    setup();
    const char *out = run("frob");
    CHECK_CONTAINS(out, "error: unknown command 'frob'");
    CHECK(fake_log_len == 0);
}

static void test_bad_arguments_change_nothing(void) {
    static const char *const bad[] = {
        "ctrl", "ctrl strobe", "ctrl strobe 2", "ctrl foo 1", "ctrl strobe 1 1",
        "oe", "oe data", "oe data maybe", "oe disk on",
        "dir", "dir sideways", "dir in out",
        "data 1ff", "data zz", "data 0x", "data 12 34",
        "status x", "info x", "leds x", "help x y",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        setup();
        const char *out = run(bad[i]);
        if (!strstr(out, "error: ") || !strstr(out, "usage: ") || fake_log_len != 0) {
            fprintf(stderr, "    '%s' was not rejected cleanly:\n%s\n", bad[i], out);
            test_failures++;
        }
    }
}

static void test_ctrl_sets_pin(void) {
    setup();
    CHECK_CONTAINS(run("ctrl strobe 0"), "ok\n");
    CHECK(fake_cable_level(1) == 0);
    CHECK_CONTAINS(run("ctrl SELIN 0"), "ok\n");
    CHECK(fake_cable_level(17) == 0);
    CHECK_CONTAINS(run("info"), "/STROBE=0 /AUTOFD=1 /INIT=1 /SELIN=0");
    run("ctrl strobe 1");
    CHECK(fake_cable_level(1) == 1);
}

static void test_dir_out_needs_oe(void) {
    setup();
    const char *out = run("dir out");
    CHECK_CONTAINS(out, "error: oe data is off");
    CHECK(fake_log_len == 0);
    CHECK(!fake_data_driven);
}

static void test_data_write_needs_dir_out(void) {
    setup();
    run("oe data on");
    fake_log_clear();
    CHECK_CONTAINS(run("data 5a"), "error: direction is in");
    CHECK(fake_log_len == 0);
}

static void test_data_path(void) {
    setup();
    CHECK_CONTAINS(run("oe data on"), "ok\n");
    CHECK_CONTAINS(run("dir out"), "ok\n");
    CHECK(data_cable() == 0x00); /* preload: last data, 0 after power-up */
    CHECK_CONTAINS(run("data 0x5A"), "ok\n");
    CHECK(data_cable() == 0x5A);
    CHECK_CONTAINS(run("data"), "data: 0x5A\n");
    run("dir in");
    run("dir out");
    CHECK(data_cable() == 0x5A); /* dir out preloads the last value */
    run("oe data off");
    CHECK(!bus_data_is_out());
    run("oe data on");
    CHECK(data_cable() == -1); /* not driving after re-enable */
    CHECK(fake_fights == 0);
}

static void test_data_read_in(void) {
    setup();
    run("oe data on");
    for (unsigned k = 0; k < DATA_COUNT; k++) fake_stuck(DATA_DB25[k], (0xC3 >> k) & 1);
    CHECK_CONTAINS(run("data"), "data: 0xC3\n");
}

static void test_oe_ctrl(void) {
    setup();
    run("oe ctrl off");
    CHECK(!fake_u23_enabled());
    CHECK_CONTAINS(run("info"), "oe data=off ctrl=off, dir=in");
    run("oe ctrl on");
    CHECK(fake_u23_enabled());
}

static void test_info(void) {
    setup();
    const char *out = run("info");
    CHECK_CONTAINS(out, "zipbridge bring-up " FW_VERSION);
    CHECK_CONTAINS(out, "fake RP2350");
    CHECK_CONTAINS(out, "oe data=off ctrl=on, dir=in");
    CHECK_CONTAINS(out, "/STROBE=1 /AUTOFD=1 /INIT=1 /SELIN=1");
    CHECK_CONTAINS(out, "data: 0x");
}

static void test_leds_command(void) {
    setup();
    const char *out = run("leds");
    CHECK_CONTAINS(out, "READY\nREAD\nWRITE\n");
    CHECK_CONTAINS(out, "ok\n");
    CHECK(((fake_out_value() >> PIN_LED_WRITE) & 1u) == 0); /* override cleared */
}

static void test_backspace(void) {
    setup();
    fake_io_clear_output();
    feed("statx\b");
    feed("us\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
    feed("x\x7f" "status\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
}

static void test_crlf_is_one_enter(void) {
    setup();
    fake_io_clear_output();
    feed("status\r\n");
    CHECK(count_of(fake_io_output(), "> ") == 1);
    CHECK(count_of(fake_io_output(), "ok\n") == 1);
}

static void test_lf_alone_works(void) {
    setup();
    fake_io_clear_output();
    feed("status\n");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
}

static void test_line_too_long(void) {
    setup();
    char line[101];
    memset(line, 'a', 100);
    line[100] = 0;
    CHECK_CONTAINS(run(line), "error: line too long");
    CHECK(fake_log_len == 0);
}

static void test_fault_reported_and_sticky(void) {
    setup();
    fake_io_clear_output();
    bus_oe_data(true);
    bus_data_write(0x01); /* direction is in: internal fault */
    CHECK_CONTAINS(fake_io_output(), "FAULT:");
    CHECK(leds_ready_mode() == LEDS_READY_FAULT);
    fake_log_clear();
    CHECK_CONTAINS(run("oe data on"), "error: fault");
    CHECK_CONTAINS(run("ctrl strobe 0"), "error: fault");
    CHECK(fake_log_len == 0);
    CHECK_CONTAINS(run("status"), "ok\n"); /* reads still work */
    CHECK_CONTAINS(run("info"), "FAULT:");
}

int main(void) {
    RUN(test_help_lists_commands);
    RUN(test_status_case_insensitive);
    RUN(test_status_shows_levels);
    RUN(test_unknown_command);
    RUN(test_bad_arguments_change_nothing);
    RUN(test_ctrl_sets_pin);
    RUN(test_dir_out_needs_oe);
    RUN(test_data_write_needs_dir_out);
    RUN(test_data_path);
    RUN(test_data_read_in);
    RUN(test_oe_ctrl);
    RUN(test_info);
    RUN(test_leds_command);
    RUN(test_backspace);
    RUN(test_crlf_is_one_enter);
    RUN(test_lf_alone_works);
    RUN(test_line_too_long);
    RUN(test_fault_reported_and_sticky);
    return test_report();
}
```

- [ ] **Step 3: Update the CMake lists, run, confirm failure**

Expected: build error, because `console.c` doesn't exist.

- [ ] **Step 4: Write `firmware/src/console.c`**

```c
#include "console.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "bus.h"
#include "gpio_hal.h"
#include "io.h"
#include "leds.h"

#define CONSOLE_LINE_MAX 80
#define MAX_ARGS 4
#define PROMPT "> "

static char line[CONSOLE_LINE_MAX + 1];
static size_t len;
static bool overflow;
static int prev_char;
static uint8_t last_data; /* last `data <hex>` value, preloaded by `dir out` */
static char errbuf[96];

/* Output longer than 159 characters is cut off: send long fixed text with io_write. */
void out_printf(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    io_write(buf);
}

static void on_fault(const char *why) {
    leds_set_ready(LEDS_READY_FAULT);
    out_printf("\nFAULT: %s. All buffers off; unplug to reset.\n", why);
}

static const char *fault_error(void) {
    snprintf(errbuf, sizeof errbuf, "fault (%s); unplug to reset", bus_fault_reason());
    return errbuf;
}

static bool parse_hex_byte(const char *s, uint8_t *v) {
    if (s[0] == '0' && s[1] == 'x') s += 2;
    size_t n = strlen(s);
    if (n < 1 || n > 2) return false;
    unsigned val = 0;
    for (; *s; s++) {
        int d = isdigit((unsigned char)*s) ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : -1;
        if (d < 0) return false;
        val = val * 16u + (unsigned)d;
    }
    *v = (uint8_t)val;
    return true;
}

/* ---- commands: each returns NULL (ok) or an error message ---- */

typedef const char *(*cmd_fn)(int argc, char **argv);
typedef struct {
    const char *name;
    const char *usage;
    cmd_fn fn;
    bool changes_pins; /* refused while faulted */
} cmd_t;

static const char *cmd_help(int argc, char **argv);
static const char *cmd_info(int argc, char **argv);
static const char *cmd_leds(int argc, char **argv);
static const char *cmd_status(int argc, char **argv);
static const char *cmd_ctrl(int argc, char **argv);
static const char *cmd_oe(int argc, char **argv);
static const char *cmd_dir(int argc, char **argv);
static const char *cmd_data(int argc, char **argv);

static const cmd_t CMDS[] = {
    {"help", "help", cmd_help, false},
    {"info", "info", cmd_info, false},
    {"leds", "leds", cmd_leds, false},
    {"status", "status", cmd_status, false},
    {"ctrl", "ctrl <strobe|autofd|init|selin> <0|1>", cmd_ctrl, true},
    {"oe", "oe <data|ctrl> <on|off>", cmd_oe, true},
    {"dir", "dir <in|out>", cmd_dir, true},
    {"data", "data [<hex>]", cmd_data, false}, /* write form checks the fault itself */
};
#define N_CMDS (sizeof CMDS / sizeof CMDS[0])

static const char *cmd_help(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    out_printf("commands (levels are pin levels, 0/1):\n");
    for (size_t i = 0; i < N_CMDS; i++) out_printf("  %s\n", CMDS[i].usage);
    return NULL;
}

static const char *cmd_info(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    uint8_t c = bus_ctrl_get();
    out_printf("zipbridge bring-up %s, built %s %s, chip %s\n", FW_VERSION, __DATE__, __TIME__,
               hal_chip_name());
    out_printf("oe data=%s ctrl=%s, dir=%s\n", bus_oe_data_on() ? "on" : "off",
               bus_oe_ctrl_on() ? "on" : "off", bus_data_is_out() ? "out" : "in");
    out_printf("ctrl:");
    for (unsigned k = 0; k < CTRL_COUNT; k++) out_printf(" %s=%u", CTRL_NAME[k], (c >> k) & 1u);
    out_printf("\ndata: 0x%02X\n", bus_data_read());
    if (bus_faulted()) out_printf("FAULT: %s\n", bus_fault_reason());
    return NULL;
}

static const char *cmd_leds(int argc, char **argv) {
    static const int order[3] = {LED_READY, LED_READ, LED_WRITE};
    static const char *const name[3] = {"READY", "READ", "WRITE"};
    (void)argv;
    if (argc != 1) return "too many arguments";
    for (int i = 0; i < 3; i++) {
        out_printf("%s\n", name[i]);
        leds_override(order[i]);
        hal_delay_us(500000);
    }
    leds_override(LED_NONE);
    return NULL;
}

static const char *cmd_status(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    uint8_t s = bus_status_read();
    leds_flash_read();
    for (unsigned k = 0; k < STATUS_COUNT; k++)
        out_printf("%s%s=%u", k ? " " : "", STATUS_NAME[k], (s >> k) & 1u);
    out_printf("\n");
    return NULL;
}

static const char *cmd_ctrl(int argc, char **argv) {
    static const char *const names[CTRL_COUNT] = {"strobe", "autofd", "init", "selin"};
    if (argc != 3) return argc < 3 ? "missing arguments" : "too many arguments";
    int k = -1;
    for (int i = 0; i < (int)CTRL_COUNT; i++)
        if (strcmp(argv[1], names[i]) == 0) k = i;
    if (k < 0) return "unknown control line";
    if (strcmp(argv[2], "0") != 0 && strcmp(argv[2], "1") != 0) return "level must be 0 or 1";
    uint8_t c = bus_ctrl_get();
    c = argv[2][0] == '1' ? (uint8_t)(c | (1u << k)) : (uint8_t)(c & ~(1u << k));
    bus_ctrl_write(c);
    leds_flash_write();
    return NULL;
}

static const char *cmd_oe(int argc, char **argv) {
    if (argc != 3) return argc < 3 ? "missing arguments" : "too many arguments";
    bool on;
    if (strcmp(argv[2], "on") == 0) on = true;
    else if (strcmp(argv[2], "off") == 0) on = false;
    else return "state must be on or off";
    if (strcmp(argv[1], "data") == 0) bus_oe_data(on);
    else if (strcmp(argv[1], "ctrl") == 0) bus_oe_ctrl(on);
    else return "buffer must be data or ctrl";
    return NULL;
}

static const char *cmd_dir(int argc, char **argv) {
    if (argc != 2) return argc < 2 ? "missing argument" : "too many arguments";
    if (strcmp(argv[1], "in") == 0) {
        bus_data_dir_in();
        return NULL;
    }
    if (strcmp(argv[1], "out") != 0) return "direction must be in or out";
    if (!bus_oe_data_on()) return "oe data is off; run 'oe data on' first";
    bus_data_dir_out(last_data);
    return NULL;
}

static const char *cmd_data(int argc, char **argv) {
    if (argc == 1) {
        uint8_t v = bus_data_read();
        leds_flash_read();
        out_printf("data: 0x%02X\n", v);
        return NULL;
    }
    if (argc > 2) return "too many arguments";
    uint8_t v;
    if (!parse_hex_byte(argv[1], &v)) return "not a hex byte (00-ff)";
    if (bus_faulted()) return fault_error();
    if (!bus_data_is_out()) return "direction is in; run 'dir out' first";
    bus_data_write(v);
    last_data = v;
    leds_flash_write();
    return NULL;
}

/* ---- line handling ---- */

void console_init(void) {
    len = 0;
    overflow = false;
    prev_char = 0;
    last_data = 0;
    bus_set_fault_handler(on_fault);
}

void console_banner(void) {
    out_printf("\nzipbridge bring-up %s. Type 'help'.\n%s", FW_VERSION, PROMPT);
}

void console_exec(char *s) {
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *t = strtok(s, " \t"); t; t = strtok(NULL, " \t")) {
        if (argc == MAX_ARGS) {
            out_printf("error: too many arguments\nusage: help\n");
            return;
        }
        argv[argc++] = t;
    }
    if (argc == 0) return;
    const cmd_t *c = NULL;
    for (size_t i = 0; i < N_CMDS; i++)
        if (strcmp(argv[0], CMDS[i].name) == 0) c = &CMDS[i];
    if (!c) {
        out_printf("error: unknown command '%s'\nusage: help\n", argv[0]);
        return;
    }
    const char *err = (c->changes_pins && bus_faulted()) ? fault_error() : c->fn(argc, argv);
    if (err) out_printf("error: %s\nusage: %s\n", err, c->usage);
    else out_printf("ok\n");
}

void console_feed(int c) {
    bool lf_after_cr = (c == '\n' && prev_char == '\r');
    prev_char = c;
    if (lf_after_cr) return;
    if (c == '\r' || c == '\n') {
        io_write("\n");
        if (overflow) {
            out_printf("error: line too long (max %d characters)\n", CONSOLE_LINE_MAX);
        } else {
            line[len] = 0;
            console_exec(line);
        }
        len = 0;
        overflow = false;
        io_write(PROMPT);
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (len > 0) {
            len--;
            io_write("\b \b");
        }
        return;
    }
    if (c < 0x20 || c > 0x7E) return;
    if (len < CONSOLE_LINE_MAX) {
        char echo[2] = {(char)c, 0};
        line[len++] = (char)c;
        io_write(echo);
    } else {
        overflow = true;
    }
}

int console_poll_key(void) {
    for (;;) {
        int c = io_getc();
        if (c < 0) return -1;
        bool lf_after_cr = (c == '\n' && prev_char == '\r');
        prev_char = c;
        if (!lf_after_cr) return c;
    }
}

bool console_wait_enter(void) {
    for (;;) {
        int c = console_poll_key();
        if (c == '\r' || c == '\n') return true;
        if (c >= 0) return false;
        leds_poll();
        hal_delay_us(1000);
    }
}
```

- [ ] **Step 5: Run the tests and confirm they pass**

Expected: `100% tests passed, 0 tests failed out of 4`. If `test_bad_arguments_change_nothing` names a line, fix that command's argument check so it runs before any pin call.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/console.h firmware/src/console.c firmware/test/test_console.c firmware/test/CMakeLists.txt
git commit -m "Firmware: serial console and pin commands"
```

---

### Task 5: `walk` and `pattern`

**Files:**
- Create: `firmware/src/exercise.h`, `firmware/src/exercise.c`
- Modify: `firmware/src/console.c`, to add the two commands
- Test: `firmware/test/test_exercise.c`, and new cases in `firmware/test/test_console.c`
- Modify: `firmware/test/CMakeLists.txt`. Change the lists to `set(FW_SRC board.c bus.c leds.c console.c exercise.c)` and `set(TESTS fake bus leds console exercise)`.

- [ ] **Step 1: Write `firmware/src/exercise.h`**

```c
#ifndef EXERCISE_H
#define EXERCISE_H

#include <stdint.h>

extern const char JUMPER_WARNING[];

/* Both print the jumper warning and wait for Enter (any other key cancels:
 * returns "cancelled", nothing driven). Any key stops them. They always end
 * in bus_safe(). Return NULL when they ran. */
const char *exercise_walk(uint32_t ms_per_step); /* one output high at a time */
const char *exercise_pattern(void);              /* line i toggles every i ticks of 24 kHz */

#endif
```

- [ ] **Step 2: Write the failing test `firmware/test/test_exercise.c`**

```c
#include <stdlib.h>

#include "board.h"
#include "bus.h"
#include "console.h"
#include "exercise.h"
#include "fake_board.h"
#include "fake_io.h"
#include "leds.h"
#include "test.h"

int test_failures;

static void setup(void) {
    fake_reset(1);
    fake_io_reset();
    leds_init();
    bus_init();
    console_init();
    fake_log_clear();
}

static void check_safe(void) {
    CHECK(!fake_u1_enabled() && fake_u23_enabled());
    CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
    CHECK(fake_fights == 0);
    CHECK(!fake_log_overflow);
}

/* Snapshot of the 12 outputs (D0..D7, /STROBE../SELIN) at each delay, when all are driven. */
static int snaps[64][12];
static int nsnap;

static void snap(const fake_call_t *c) {
    if (c->op != FAKE_DELAY) return;
    int lv[12];
    for (unsigned k = 0; k < 12; k++) {
        lv[k] = fake_cable_level(k < DATA_COUNT ? DATA_DB25[k] : CTRL_DB25[k - DATA_COUNT]);
        if (lv[k] < 0) return;
    }
    if (nsnap > 0 && memcmp(snaps[nsnap - 1], lv, sizeof lv) == 0) return;
    if (nsnap < 64) memcpy(snaps[nsnap++], lv, sizeof lv);
}

static void test_walk_one_hot(void) {
    setup();
    fake_io_push("\r");
    nsnap = 0;
    fake_on_op = snap;
    const char *err = exercise_walk(100);
    fake_on_op = 0;
    CHECK(err == NULL);
    CHECK(nsnap == 12);
    for (int s = 0; s < nsnap; s++)
        for (int k = 0; k < 12; k++) CHECK(snaps[s][k] == (k == s));
    const char *out = fake_io_output();
    CHECK_CONTAINS(out, "Remove all loopback jumpers");
    CHECK_CONTAINS(out, "D0 = pin 2 high\n");
    CHECK_CONTAINS(out, "D7 = pin 9 high\n");
    CHECK_CONTAINS(out, "/STROBE = pin 1 high\n");
    CHECK_CONTAINS(out, "/SELIN = pin 17 high\n");
    check_safe();
}

static void test_walk_step_time(void) {
    setup();
    fake_io_push("\r");
    exercise_walk(250);
    uint64_t t[16];
    int n = 0;
    for (int i = 0; i < fake_log_len && n < 16; i++)
        if (fake_log[i].op == FAKE_PUT && fake_log[i].mask == (DATA_MASK | CTRL_MASK)) t[n++] = fake_log[i].t;
    CHECK(n == 13); /* all-low start + 12 steps */
    for (int i = 2; i < n; i++) CHECK(t[i] - t[i - 1] >= 250000 && t[i] - t[i - 1] <= 252000);
}

static void test_walk_cancel(void) {
    setup();
    fake_io_push("x");
    CHECK(strcmp(exercise_walk(100), "cancelled") == 0);
    CHECK(!fake_data_driven);
    check_safe();
}

static void test_walk_key_stops(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(15); /* ESC after 15 empty polls: during step 2 */
    CHECK(exercise_walk(100) == NULL);
    CHECK_CONTAINS(fake_io_output(), "D1 = pin 3 high\n");
    CHECK_LACKS(fake_io_output(), "D2 = pin 4 high");
    CHECK_CONTAINS(fake_io_output(), "stopped\n");
    check_safe();
}

static void test_pattern_frequencies(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(3); /* stops after about 170 ms */
    CHECK(exercise_pattern() == NULL);
    uint64_t t_first = 0, t_last = 0;
    uint32_t prev = 0;
    int toggles[12] = {0};
    bool have = false;
    for (int i = 0; i < fake_log_len; i++) {
        const fake_call_t *c = &fake_log[i];
        if (c->op != FAKE_PUT || c->mask != (DATA_MASK | CTRL_MASK)) continue;
        uint32_t lines = ((c->value >> PIN_DATA_BASE) & 0xFFu) | (((c->value >> PIN_CTRL_BASE) & 0xFu) << 8);
        if (have) {
            for (int k = 0; k < 12; k++) toggles[k] += (int)(((lines ^ prev) >> k) & 1u);
            t_last = c->t;
        } else {
            t_first = c->t;
            have = true;
        }
        prev = lines;
    }
    double secs = (double)(t_last - t_first) / 1e6;
    CHECK(secs > 0.1);
    for (int k = 0; k < 12; k++) {
        double want = secs * 24000.0 / (k + 1); /* line k+1 toggles every k+1 ticks */
        if (abs(toggles[k] - (int)want) > (int)(want * 0.03) + 2) {
            fprintf(stderr, "    line %d: %d toggles, want about %.0f\n", k + 1, toggles[k], want);
            test_failures++;
        }
    }
    CHECK_CONTAINS(fake_io_output(), "stopped\n");
    check_safe();
}

static void test_pattern_cancel(void) {
    setup();
    fake_io_push("q");
    CHECK(strcmp(exercise_pattern(), "cancelled") == 0);
    CHECK(!fake_data_driven);
    check_safe();
}

int main(void) {
    RUN(test_walk_one_hot);
    RUN(test_walk_step_time);
    RUN(test_walk_cancel);
    RUN(test_walk_key_stops);
    RUN(test_pattern_frequencies);
    RUN(test_pattern_cancel);
    return test_report();
}
```

- [ ] **Step 3: Add console cases to `firmware/test/test_console.c`**

Add these functions above `main`, and add `RUN(...)` lines for each:

```c
static void test_walk_needs_enter(void) {
    setup();
    fake_io_push("x");
    CHECK_CONTAINS(run("walk 100"), "error: cancelled");
    CHECK(!fake_data_driven);
}

static void test_walk_crlf_then_enter(void) {
    setup();
    fake_io_push("\n\r"); /* LF of the command's CR LF is skipped; then Enter */
    const char *out = run("walk 100");
    CHECK_CONTAINS(out, "/SELIN = pin 17 high\n");
    CHECK_CONTAINS(out, "ok\n");
    CHECK(!fake_u1_enabled() && fake_u23_enabled());
}

static void test_walk_and_pattern_bad_args(void) {
    static const char *const bad[] = {"walk 50", "walk 20000", "walk abc", "walk 100 2", "pattern now"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        setup();
        const char *out = run(bad[i]);
        if (!strstr(out, "error: ") || !strstr(out, "usage: ") || fake_log_len != 0) {
            fprintf(stderr, "    '%s' was not rejected cleanly:\n%s\n", bad[i], out);
            test_failures++;
        }
    }
}

static void test_pattern_needs_enter(void) {
    setup();
    fake_io_push("q");
    CHECK_CONTAINS(run("pattern"), "error: cancelled");
    CHECK(!fake_data_driven);
}
```

Also add `CHECK_CONTAINS(out, "walk [ms]");` and `CHECK_CONTAINS(out, "pattern");` to `test_help_lists_commands`.

- [ ] **Step 4: Update the CMake lists, run, confirm failure**

Expected: build error, because `exercise.c` doesn't exist.

- [ ] **Step 5: Write `firmware/src/exercise.c`**

```c
#include "exercise.h"

#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "bus.h"
#include "console.h"
#include "gpio_hal.h"
#include "leds.h"

const char JUMPER_WARNING[] = "Remove all loopback jumpers. Enter to start, any other key cancels.\n";

#define N_OUT (DATA_COUNT + CTRL_COUNT)

static void start_driving(void) {
    bus_safe();
    bus_oe_data(true);
    bus_data_dir_out(0);
}

const char *exercise_walk(uint32_t ms) {
    out_printf("%s", JUMPER_WARNING);
    if (!console_wait_enter()) return "cancelled";
    start_driving();
    bus_out_write(0, 0);
    bool stopped = false;
    for (unsigned i = 0; i < N_OUT && !stopped && !bus_faulted(); i++) {
        bool is_data = i < DATA_COUNT;
        unsigned k = is_data ? i : i - DATA_COUNT;
        bus_out_write(is_data ? (uint8_t)(1u << k) : 0, is_data ? 0 : (uint8_t)(1u << k));
        out_printf("%s = pin %u high\n", is_data ? DATA_NAME[k] : CTRL_NAME[k],
                   (unsigned)(is_data ? DATA_DB25[k] : CTRL_DB25[k]));
        for (uint32_t t = 0; t < ms; t += 10) {
            if (console_poll_key() >= 0) {
                stopped = true;
                break;
            }
            leds_poll();
            hal_delay_us((ms - t < 10 ? ms - t : 10) * 1000u);
        }
    }
    bus_safe();
    if (stopped) out_printf("stopped\n");
    return NULL;
}

const char *exercise_pattern(void) {
    out_printf("%s", JUMPER_WARNING);
    if (!console_wait_enter()) return "cancelled";
    out_printf("pattern running; any key stops\n");
    start_driving();
    uint64_t t0 = hal_time_us();
    uint32_t last_tick = UINT32_MAX, last_poll = 0;
    for (;;) {
        uint32_t tick = (uint32_t)((hal_time_us() - t0) * 24u / 1000u); /* 24 kHz */
        if (tick == last_tick) continue;
        last_tick = tick;
        uint32_t lines = 0; /* bit i-1 = line i: D0..D7, then /STROBE../SELIN */
        for (unsigned i = 1; i <= N_OUT; i++)
            if ((tick / i) & 1u) lines |= 1u << (i - 1);
        bus_out_write((uint8_t)lines, (uint8_t)(lines >> 8));
        if (tick - last_poll >= 1024u) { /* about every 43 ms */
            last_poll = tick;
            leds_poll();
            if (console_poll_key() >= 0 || bus_faulted()) break;
        }
    }
    bus_safe();
    out_printf("stopped\n");
    return NULL;
}
```

- [ ] **Step 6: Add the commands to `firmware/src/console.c`**

Add `#include "exercise.h"`. Add the prototypes next to the others:

```c
static const char *cmd_walk(int argc, char **argv);
static const char *cmd_pattern(int argc, char **argv);
```

Append to `CMDS` after the `data` entry:

```c
    {"walk", "walk [ms]  (100-10000, default 1000; no jumpers)", cmd_walk, true},
    {"pattern", "pattern  (no jumpers)", cmd_pattern, true},
```

Add the definitions after `cmd_data`. `parse_uint` arrives here, not in Task 4, because an unused static function fails `-Werror`:

```c
static bool parse_uint(const char *s, uint32_t *v) {
    if (!*s) return false;
    uint32_t x = 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s) || x > 100000u) return false;
        x = x * 10u + (uint32_t)(*s - '0');
    }
    *v = x;
    return true;
}

static const char *cmd_walk(int argc, char **argv) {
    uint32_t ms = 1000;
    if (argc > 2) return "too many arguments";
    if (argc == 2 && (!parse_uint(argv[1], &ms) || ms < 100 || ms > 10000)) return "ms must be 100-10000";
    return exercise_walk(ms);
}

static const char *cmd_pattern(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    return exercise_pattern();
}
```

- [ ] **Step 7: Run the tests and confirm they pass**

Expected: `100% tests passed, 0 tests failed out of 5`.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/exercise.h firmware/src/exercise.c firmware/src/console.c firmware/test
git commit -m "Firmware: walk and pattern output tests"
```

---

### Task 6: `scan` loopback mapper

**Files:**
- Create: `firmware/src/scan.h`, `firmware/src/scan.c`
- Modify: `firmware/src/console.c`, to add `scan` and `help scan`
- Test: `firmware/test/test_scan.c`, and a case in `firmware/test/test_console.c`
- Modify: `firmware/test/CMakeLists.txt`. Change the lists to `set(FW_SRC board.c bus.c leds.c console.c exercise.c scan.c)` and `set(TESTS fake bus leds console exercise scan)`.

- [ ] **Step 1: Write `firmware/src/scan.h`**

```c
#ifndef SCAN_H
#define SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"

#define SCAN_OUTPUTS (DATA_COUNT + CTRL_COUNT)  /* 0..7 = D0..D7, 8..11 = /STROBE../SELIN */
#define SCAN_INPUTS  (DATA_COUNT + STATUS_COUNT) /* 0..7 = D0..D7, 8..12 = /ACK../ERROR */

typedef struct {
    bool link[SCAN_OUTPUTS][SCAN_INPUTS];    /* matched on all 8 samples (any phase) */
    bool unclear[SCAN_OUTPUTS][SCAN_INPUTS]; /* changed but didn't match */
    uint16_t sampled, seen0, seen1;          /* bit per input */
    bool phase2_skipped;
    bool aborted;
} scan_result_t;

extern const char SCAN_RULES[];

void scan_measure(scan_result_t *r);      /* both phases; ends in bus_safe() */
void scan_report(const scan_result_t *r); /* prints the report */
const char *scan_run(void);               /* rules + measure + report (console) */

#endif
```

- [ ] **Step 2: Write the failing test `firmware/test/test_scan.c`**

```c
#include "board.h"
#include "bus.h"
#include "console.h"
#include "fake_board.h"
#include "fake_io.h"
#include "leds.h"
#include "scan.h"
#include "test.h"

int test_failures;

/* Fresh board, apply wiring, run scan, return the output. */
static const char *scan_with(void (*wire)(void)) {
    fake_reset(1);
    fake_io_reset();
    leds_init();
    bus_init();
    console_init();
    if (wire) wire();
    fake_log_clear();
    fake_io_clear_output();
    fake_contention_reset();
    fake_data_driven = false;
    CHECK(scan_run() == NULL);
    return fake_io_output();
}

static void check_common(void) {
    CHECK(fake_max_contention_us() <= 20);
    CHECK(fake_fights == 0);
    CHECK(!fake_log_overflow);
    CHECK(!fake_u1_enabled() && fake_u23_enabled()); /* bus_safe() state */
    CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
}

/* An unwired data input may show either "floating?" or "stuck". */
static void check_floating_or_stuck(const char *out, const char *who) {
    char a[48], b[48];
    snprintf(a, sizeof a, "%s floating?\n", who);
    snprintf(b, sizeof b, "%s stuck ", who);
    if (!strstr(out, a) && !strstr(out, b)) {
        fprintf(stderr, "    no floating/stuck line for %s:\n%s\n", who, out);
        test_failures++;
    }
}

static void no_wiring(void) {}
static void pass_a(void) { fake_jumper(2, 10); fake_jumper(3, 11); fake_jumper(4, 12); fake_jumper(5, 13); fake_jumper(6, 15); }
static void pass_b(void) { fake_jumper(7, 10); fake_jumper(8, 11); fake_jumper(9, 12); fake_jumper(1, 13); fake_jumper(14, 15); }
static void pass_c(void) {
    fake_jumper(16, 10); fake_jumper(17, 11);
    fake_jumper(1, 2); fake_jumper(14, 3); fake_jumper(16, 4); fake_jumper(17, 5);
}
static void pass_d(void) { fake_jumper(1, 6); fake_jumper(14, 7); fake_jumper(16, 8); fake_jumper(17, 9); }
static void short_d3_d4(void) { fake_jumper(5, 13); fake_jumper(6, 13); }
static void weak_d3(void) { fake_jumper(5, 13); fake_flaky(13); }
static void ack_stuck_low(void) { fake_stuck(10, 0); }

static void test_rules_printed(void) {
    const char *out = scan_with(no_wiring);
    CHECK_CONTAINS(out, "Jumper rules");
    CHECK_CONTAINS(out, "At most one jumper per input pin");
}

static void test_no_wiring(void) {
    const char *out = scan_with(no_wiring);
    check_common();
    CHECK(count_of(out, ") -> ") == 0);
    CHECK_CONTAINS(out, "/ACK (pin 10) stuck high\n");
    CHECK_CONTAINS(out, "BUSY (pin 11) stuck high\n");
    CHECK_CONTAINS(out, "PE (pin 12) stuck high\n");
    CHECK_CONTAINS(out, "SELECT (pin 13) stuck high\n");
    CHECK_CONTAINS(out, "/ERROR (pin 15) stuck high\n");
    check_floating_or_stuck(out, "D0 (pin 2)");
    check_floating_or_stuck(out, "D7 (pin 9)");
    CHECK_LACKS(out, "possible short:");
    CHECK_LACKS(out, "weak");
    CHECK_LACKS(out, "phase 2 skipped");
    CHECK_CONTAINS(out, "scan: 0 links, 0 possible shorts, ");
}

static void test_pass_a(void) {
    const char *out = scan_with(pass_a);
    check_common();
    CHECK(count_of(out, ") -> ") == 5);
    CHECK_CONTAINS(out, "D0 (pin 2) -> /ACK (pin 10)\n");
    CHECK_CONTAINS(out, "D1 (pin 3) -> BUSY (pin 11)\n");
    CHECK_CONTAINS(out, "D2 (pin 4) -> PE (pin 12)\n");
    CHECK_CONTAINS(out, "D3 (pin 5) -> SELECT (pin 13)\n");
    CHECK_CONTAINS(out, "D4 (pin 6) -> /ERROR (pin 15)\n");
    CHECK_CONTAINS(out, "D0 (pin 2) stuck high\n"); /* jumpered to a pulled-up status pin */
    CHECK_CONTAINS(out, "D4 (pin 6) stuck high\n");
    check_floating_or_stuck(out, "D5 (pin 7)");
    check_floating_or_stuck(out, "D6 (pin 8)");
    check_floating_or_stuck(out, "D7 (pin 9)");
    CHECK_LACKS(out, "possible short:");
    CHECK_LACKS(out, "weak");
    CHECK_LACKS(out, "(pin 10) stuck");
    CHECK_CONTAINS(out, "scan: 5 links, 0 possible shorts, ");
}

static void test_pass_b(void) {
    const char *out = scan_with(pass_b);
    check_common();
    CHECK(count_of(out, ") -> ") == 5); /* control -> status found in both phases, shown once */
    CHECK_CONTAINS(out, "D5 (pin 7) -> /ACK (pin 10)\n");
    CHECK_CONTAINS(out, "D6 (pin 8) -> BUSY (pin 11)\n");
    CHECK_CONTAINS(out, "D7 (pin 9) -> PE (pin 12)\n");
    CHECK_CONTAINS(out, "/STROBE (pin 1) -> SELECT (pin 13)\n");
    CHECK_CONTAINS(out, "/AUTOFD (pin 14) -> /ERROR (pin 15)\n");
    CHECK_CONTAINS(out, "D5 (pin 7) stuck high\n");
    check_floating_or_stuck(out, "D0 (pin 2)");
    CHECK_LACKS(out, "possible short:");
    CHECK_CONTAINS(out, "scan: 5 links, 0 possible shorts, ");
}

static void test_pass_c(void) {
    const char *out = scan_with(pass_c);
    check_common();
    CHECK(!fake_data_driven); /* phase 2 skipped: data never driven */
    CHECK(count_of(out, ") -> ") == 6);
    CHECK_CONTAINS(out, "/STROBE (pin 1) -> D0 (pin 2)\n");
    CHECK_CONTAINS(out, "/AUTOFD (pin 14) -> D1 (pin 3)\n");
    CHECK_CONTAINS(out, "/INIT (pin 16) -> D2 (pin 4)\n");
    CHECK_CONTAINS(out, "/INIT (pin 16) -> /ACK (pin 10)\n");
    CHECK_CONTAINS(out, "/SELIN (pin 17) -> D3 (pin 5)\n");
    CHECK_CONTAINS(out, "/SELIN (pin 17) -> BUSY (pin 11)\n");
    CHECK_CONTAINS(out, "PE (pin 12) stuck high\n");
    CHECK_CONTAINS(out, "SELECT (pin 13) stuck high\n");
    CHECK_CONTAINS(out, "/ERROR (pin 15) stuck high\n");
    check_floating_or_stuck(out, "D4 (pin 6)");
    check_floating_or_stuck(out, "D7 (pin 9)");
    CHECK_CONTAINS(out, "phase 2 skipped: control -> data jumpers present\n");
    CHECK_CONTAINS(out, "scan: 6 links, 0 possible shorts, ");
}

static void test_pass_d(void) {
    const char *out = scan_with(pass_d);
    check_common();
    CHECK(!fake_data_driven);
    CHECK(count_of(out, ") -> ") == 4);
    CHECK_CONTAINS(out, "/STROBE (pin 1) -> D4 (pin 6)\n");
    CHECK_CONTAINS(out, "/AUTOFD (pin 14) -> D5 (pin 7)\n");
    CHECK_CONTAINS(out, "/INIT (pin 16) -> D6 (pin 8)\n");
    CHECK_CONTAINS(out, "/SELIN (pin 17) -> D7 (pin 9)\n");
    CHECK_CONTAINS(out, "/ACK (pin 10) stuck high\n");
    CHECK_CONTAINS(out, "/ERROR (pin 15) stuck high\n");
    check_floating_or_stuck(out, "D0 (pin 2)");
    check_floating_or_stuck(out, "D3 (pin 5)");
    CHECK_CONTAINS(out, "phase 2 skipped: control -> data jumpers present\n");
    CHECK_CONTAINS(out, "scan: 4 links, 0 possible shorts, ");
}

static void test_possible_short(void) {
    const char *out = scan_with(short_d3_d4);
    check_common();
    CHECK_CONTAINS(out, "possible short: D3 / D4 -> SELECT (pin 13)\n");
    CHECK_LACKS(out, "D3 (pin 5) -> SELECT");
    CHECK_CONTAINS(out, "scan: 0 links, 1 possible shorts, ");
}

static void test_weak_link(void) {
    const char *out = scan_with(weak_d3);
    check_common();
    CHECK_CONTAINS(out, "weak or intermittent: D3 -> SELECT?\n");
    CHECK_LACKS(out, "D3 (pin 5) -> SELECT");
    CHECK_LACKS(out, "possible short:");
}

static void test_stuck_low(void) {
    const char *out = scan_with(ack_stuck_low);
    check_common();
    CHECK_CONTAINS(out, "/ACK (pin 10) stuck low\n");
}

static void test_floating_never_short_or_weak(void) {
    for (uint32_t seed = 1; seed <= 20; seed++) {
        fake_reset(seed);
        fake_io_reset();
        leds_init();
        bus_init();
        console_init();
        pass_c(); /* four floating data lines */
        fake_io_clear_output();
        scan_run();
        const char *out = fake_io_output();
        if (strstr(out, "possible short:") || strstr(out, "weak") || count_of(out, ") -> ") != 6) {
            fprintf(stderr, "    seed %u:\n%s\n", (unsigned)seed, out);
            test_failures++;
        }
    }
}

int main(void) {
    RUN(test_rules_printed);
    RUN(test_no_wiring);
    RUN(test_pass_a);
    RUN(test_pass_b);
    RUN(test_pass_c);
    RUN(test_pass_d);
    RUN(test_possible_short);
    RUN(test_weak_link);
    RUN(test_stuck_low);
    RUN(test_floating_never_short_or_weak);
    return test_report();
}
```

- [ ] **Step 3: Add a console case to `firmware/test/test_console.c`**

```c
static void test_scan_command(void) {
    setup();
    CHECK_CONTAINS(run("scan"), "scan: 0 links");
    CHECK_CONTAINS(run("help scan"), "At most one jumper per input pin");
    CHECK_CONTAINS(run("scan now"), "error: too many arguments");
}
```

Add `RUN(test_scan_command);`, add `CHECK_CONTAINS(out, "scan");` to `test_help_lists_commands`, and add `"scan x"` and `"help x"` to the `bad` list in `test_bad_arguments_change_nothing`.

- [ ] **Step 4: Update the CMake lists, run, confirm failure**

Expected: build error, because `scan.c` doesn't exist.

- [ ] **Step 5: Write `firmware/src/scan.c`**

```c
#include "scan.h"

#include <string.h>

#include "bus.h"
#include "console.h"
#include "io.h"

#define SAMPLES 8
#define DATA_IN_MASK   0x00FFu
#define STATUS_IN_MASK 0x1F00u

const char SCAN_RULES[] =
    "Jumper rules: wire an output to an input only (data to status, control to status,\n"
    "control to data). Never join two control pins or two data pins.\n"
    "At most one jumper per input pin. Remove all jumpers before walk and pattern.\n";

/* Irregular, so a floating input picking up crosstalk is unlikely to match. */
static const uint8_t SEQ[SAMPLES] = {0, 1, 1, 0, 1, 0, 0, 1};

static const char *out_name(unsigned o) { return o < DATA_COUNT ? DATA_NAME[o] : CTRL_NAME[o - DATA_COUNT]; }
static unsigned out_pin(unsigned o) { return o < DATA_COUNT ? DATA_DB25[o] : CTRL_DB25[o - DATA_COUNT]; }
static const char *in_name(unsigned i) { return i < DATA_COUNT ? DATA_NAME[i] : STATUS_NAME[i - DATA_COUNT]; }
static unsigned in_pin(unsigned i) { return i < DATA_COUNT ? DATA_DB25[i] : STATUS_DB25[i - DATA_COUNT]; }

/* Every output at rest (1) except output o at `level`. */
static void drive(bool phase2, unsigned o, unsigned level) {
    uint8_t d = 0xFF, c = 0x0F;
    if (!level) {
        if (o < DATA_COUNT) d = (uint8_t)(d & ~(1u << o));
        else c = (uint8_t)(c & ~(1u << (o - DATA_COUNT)));
    }
    if (phase2) bus_out_write(d, c);
    else bus_ctrl_write(c);
}

static void test_output(scan_result_t *r, bool phase2, unsigned o, uint16_t in_mask) {
    uint8_t match[SCAN_INPUTS] = {0};
    uint16_t first = 0, changed = 0;
    for (unsigned k = 0; k < SAMPLES; k++) {
        drive(phase2, o, SEQ[k]);
        bus_wait_us(5);
        uint16_t in = 0;
        if (in_mask & DATA_IN_MASK) in |= bus_data_read();
        in |= (uint16_t)(bus_status_read() << DATA_COUNT);
        drive(phase2, o, 1); /* back to rest at once */
        in &= in_mask;
        if (k == 0) first = in;
        else changed |= (uint16_t)(in ^ first);
        r->seen1 |= in;
        r->seen0 |= (uint16_t)(~in & in_mask);
        for (unsigned i = 0; i < SCAN_INPUTS; i++)
            if (((in_mask >> i) & 1u) && ((in >> i) & 1u) == SEQ[k]) match[i]++;
    }
    r->sampled |= in_mask;
    for (unsigned i = 0; i < SCAN_INPUTS; i++) {
        if (!((in_mask >> i) & 1u)) continue;
        if (match[i] == SAMPLES) r->link[o][i] = true;
        else if ((changed >> i) & 1u) r->unclear[o][i] = true;
    }
}

void scan_measure(scan_result_t *r) {
    memset(r, 0, sizeof *r);
    bus_safe();

    /* Phase 1: control -> data and status. Data buffer on, direction in. */
    bus_oe_data(true);
    for (unsigned o = DATA_COUNT; o < SCAN_OUTPUTS && !r->aborted; o++) {
        test_output(r, false, o, DATA_IN_MASK | STATUS_IN_MASK);
        if (bus_faulted()) r->aborted = true;
    }

    bool ctrl_to_data = false;
    for (unsigned o = DATA_COUNT; o < SCAN_OUTPUTS; o++)
        for (unsigned i = 0; i < DATA_COUNT; i++)
            if (r->link[o][i]) ctrl_to_data = true;

    /* Phase 2: every output -> status. Data driven, all at rest (1). */
    if (ctrl_to_data) {
        r->phase2_skipped = true;
    } else if (!r->aborted) {
        bus_data_dir_out(0xFF);
        for (unsigned o = 0; o < SCAN_OUTPUTS && !r->aborted; o++) {
            test_output(r, true, o, STATUS_IN_MASK);
            if (bus_faulted()) r->aborted = true;
        }
    }
    bus_safe();
}

void scan_report(const scan_result_t *r) {
    unsigned links = 0, shorts = 0, stuck = 0;

    for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
        for (unsigned i = 0; i < SCAN_INPUTS; i++)
            if (r->link[o][i]) {
                out_printf("%s (pin %u) -> %s (pin %u)\n", out_name(o), out_pin(o), in_name(i), in_pin(i));
                links++;
            }

    for (unsigned i = DATA_COUNT; i < SCAN_INPUTS; i++) { /* status inputs only */
        unsigned n = 0, only = 0;
        for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
            if (r->unclear[o][i] && !r->link[o][i]) { n++; only = o; }
        if (n >= 2) {
            out_printf("possible short: ");
            bool first = true;
            for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
                if (r->unclear[o][i] && !r->link[o][i]) {
                    out_printf("%s%s", first ? "" : " / ", out_name(o));
                    first = false;
                }
            out_printf(" -> %s (pin %u)\n", in_name(i), in_pin(i));
            shorts++;
        } else if (n == 1) {
            out_printf("weak or intermittent: %s -> %s?\n", out_name(only), in_name(i));
        }
    }

    for (unsigned i = 0; i < SCAN_INPUTS; i++) {
        if (!((r->sampled >> i) & 1u)) continue;
        bool changed = ((r->seen0 & r->seen1) >> i) & 1u;
        bool has_link = false;
        for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
            if (r->link[o][i]) has_link = true;
        if (!changed) {
            out_printf("%s (pin %u) stuck %s\n", in_name(i), in_pin(i), ((r->seen1 >> i) & 1u) ? "high" : "low");
            stuck++;
        } else if (i < DATA_COUNT && !has_link) {
            out_printf("%s (pin %u) floating?\n", in_name(i), in_pin(i));
        }
    }

    if (r->phase2_skipped) out_printf("phase 2 skipped: control -> data jumpers present\n");
    if (r->aborted) out_printf("scan aborted\n");
    out_printf("scan: %u links, %u possible shorts, %u stuck\n", links, shorts, stuck);
}

const char *scan_run(void) {
    scan_result_t r;
    io_write(SCAN_RULES); /* longer than out_printf's 160-character buffer */
    scan_measure(&r);
    scan_report(&r);
    return r.aborted ? "scan aborted by a fault" : NULL;
}
```

- [ ] **Step 6: Add `scan` and `help scan` to `firmware/src/console.c`**

Add `#include "scan.h"`. Add the prototype `static const char *cmd_scan(int argc, char **argv);`. Append to `CMDS`:

```c
    {"scan", "scan  (loopback mapper; see 'help scan')", cmd_scan, true},
```

Change the `help` entry's usage to `"help [scan]"`. Replace `cmd_help` with:

```c
static const char *cmd_help(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "scan") == 0) {
        io_write(SCAN_RULES); /* longer than out_printf's buffer */
        return NULL;
    }
    if (argc != 1) return "usage is 'help' or 'help scan'";
    out_printf("commands (levels are pin levels, 0/1):\n");
    for (size_t i = 0; i < N_CMDS; i++) out_printf("  %s\n", CMDS[i].usage);
    return NULL;
}
```

Add after `cmd_pattern`:

```c
static const char *cmd_scan(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    return scan_run();
}
```

- [ ] **Step 7: Run the tests and confirm they pass**

Expected: `100% tests passed, 0 tests failed out of 6`.

If `test_floating_never_short_or_weak` or a pass test reports an extra link from a floating line, that is an 8/8 chance match by the fake's drift model. That should be vanishingly rare with the drift rate of 1 in 8. Report it rather than changing the seed silently.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/scan.h firmware/src/scan.c firmware/src/console.c firmware/test
git commit -m "Firmware: scan loopback mapper"
```

---

### Task 7: Firmware build for the Pico 2

**Files:**
- Create: `firmware/CMakeLists.txt`, `firmware/pico_sdk_import.cmake`, `firmware/src/gpio_hal_rp2350.c`, `firmware/src/io_usb.c`, `firmware/src/main.c`

- [ ] **Step 1: Check for picotool**

Run: `which picotool`.

- If it is missing, ask the user before installing anything. The recommendation is `brew install picotool`, version 2.3.1, which matches SDK 2.3.1.
- Without picotool, the SDK downloads and builds it during the first CMake run, from github.com/raspberrypi/picotool.

- [ ] **Step 2: Copy the SDK import script**

```bash
cp ~/pico/pico-sdk/external/pico_sdk_import.cmake firmware/pico_sdk_import.cmake
```

- [ ] **Step 3: Write `firmware/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.13)

# Use ~/pico/pico-sdk when PICO_SDK_PATH isn't set.
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{HOME}/pico/pico-sdk")
endif()
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)
project(zipbridge_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(APP_SRC
    src/main.c src/board.c src/gpio_hal_rp2350.c src/io_usb.c
    src/bus.c src/leds.c src/console.c src/exercise.c src/scan.c)

add_executable(zipbridge_bringup ${APP_SRC})
# Strict warnings for our files only (SDK sources are compiled into the target too).
set_source_files_properties(${APP_SRC} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
target_link_libraries(zipbridge_bringup pico_stdlib)
pico_enable_stdio_usb(zipbridge_bringup 1)
pico_enable_stdio_uart(zipbridge_bringup 0)
pico_add_extra_outputs(zipbridge_bringup)
```

- [ ] **Step 4: Write `firmware/src/gpio_hal_rp2350.c`**

```c
#include <stdio.h>

#include "gpio_hal.h"
#include "hardware/gpio.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

void hal_init(uint32_t mask) { gpio_init_mask(mask); }

void hal_set_output(uint32_t mask, bool out) {
    if (out) gpio_set_dir_out_masked(mask);
    else gpio_set_dir_in_masked(mask);
}

void hal_set_pulls(uint32_t mask, bool up, bool down) {
    for (uint pin = 0; pin < 32; pin++)
        if (mask & (1u << pin)) gpio_set_pulls(pin, up, down);
}

void hal_put_masked(uint32_t mask, uint32_t value) { gpio_put_masked(mask, value); }
uint32_t hal_get_all(void) { return gpio_get_all(); }
void hal_delay_us(uint32_t us) { busy_wait_us_32(us); }
uint64_t hal_time_us(void) { return time_us_64(); }

const char *hal_chip_name(void) {
    static char name[24];
    snprintf(name, sizeof name, "RP2350 A%u", (unsigned)rp2350_chip_version());
    return name;
}
```

- [ ] **Step 5: Write `firmware/src/io_usb.c`**

```c
#include <stdio.h>

#include "io.h"
#include "pico/stdlib.h"

int io_getc(void) {
    int c = getchar_timeout_us(0); /* PICO_ERROR_TIMEOUT (-2) when nothing waits */
    return c < 0 ? -1 : c;
}

void io_write(const char *s) {
    fputs(s, stdout); /* stdio turns \n into \r\n */
    fflush(stdout);
}
```

- [ ] **Step 6: Write `firmware/src/main.c`**

```c
#include <stdbool.h>

#include "bus.h"
#include "console.h"
#include "leds.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

int main(void) {
    bus_init(); /* first: buffers stay off, control lines idle, before anything slow */
    leds_init();
    stdio_init_all();
    console_init();

    bool connected = false;
    for (;;) {
        bool now_connected = stdio_usb_connected();
        if (now_connected != connected) {
            connected = now_connected;
            leds_set_ready(connected ? LEDS_READY_ON : LEDS_READY_WAITING);
            if (connected) console_banner();
        }
        int c = getchar_timeout_us(1000);
        if (c >= 0) console_feed(c);
        leds_poll();
    }
}
```

- [ ] **Step 7: Build**

```bash
cmake -S firmware -B firmware/build -G Ninja && ninja -C firmware/build
```

Expected: no warnings from `firmware/src`, and `firmware/build/zipbridge_bringup.uf2` exists. Check with `ls -l firmware/build/zipbridge_bringup.uf2`, which should show about 60–120 KB.

If `rp2350_chip_version` is not found, add `#include "hardware/platform_defs.h"`, or replace the body with `return "RP2350";`. The chip name is informational only.

- [ ] **Step 8: Re-run the host tests**

Run the test command. Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 9: Commit**

```bash
git add firmware/CMakeLists.txt firmware/pico_sdk_import.cmake firmware/src/gpio_hal_rp2350.c firmware/src/io_usb.c firmware/src/main.c
git commit -m "Firmware: Pico 2 build (zipbridge_bringup.uf2)"
```

---

### Task 8: `BRINGUP.md` and README

**Files:**
- Create: `firmware/BRINGUP.md`
- Modify: `README.md:89`. Add a row after the `models/` row.

- [ ] **Step 1: Write `firmware/BRINGUP.md`**

````markdown
# Bring-up checklist (board v0.6)

Do these in order. Stop at the first failure; the "if not" column says where to look.

Tools: USB cable, serial terminal (`screen /dev/tty.usbmodem* 115200`, or any terminal), multimeter, logic analyzer, DB25 breakout with jumper wires.

## 0. Build and flash

```bash
cmake -S firmware -B firmware/build -G Ninja && ninja -C firmware/build
```

Hold BOOTSEL, plug in USB, and copy `firmware/build/zipbridge_bringup.uf2` to the `RP2350` drive. The Pico reboots by itself.

## Steps

| # | Do | Expect | If not |
|---|---|---|---|
| 1 | Look the board over: chip orientation in the sockets, solder bridges. Plug in USB with nothing on the DB25. | PWR LED on. | 3V3 short or LED/R16 wrong way round. Unplug at once if anything gets hot. |
| 2 | Open the serial port. | READY blinks at 1 Hz before the port opens, then stays on. The banner and `>` prompt appear. | No serial device: UF2 not flashed, or bad USB cable. |
| 3 | `info` | Version, chip, `oe data=off ctrl=on, dir=in`, all control lines `=1`. | — |
| 4 | `leds` | READY, then READ, then WRITE, each on alone for 0.5 s. | LED fitted backwards, or wrong resistor. |
| 5 | `status` with nothing connected | `/ACK=1 BUSY=1 PE=1 SELECT=1 /ERROR=1` | A 0 points at that line's 10k pull-up (R9–R13) or U3. |
| 6 | `ctrl strobe 0`, `status`, `ctrl strobe 1` | READ and WRITE flash. No change in status (nothing is wired yet). | — |
| 7 | No jumpers. Multimeter between DB25 pin 18 (GND) and the pin named. Run `walk 3000` and press Enter. | Each named pin reads about 3.3 V while it's high, and every other output pin reads about 0 V. | A pin stuck low or high: U1/U2 solder, or its 100Ω resistor (data only). A pin that is high at the wrong step: swapped traces. |
| 8 | No jumpers. Logic analyzer on pins 2–9, 1, 14, 16, 17 (GND on 18). Run `pattern`, press Enter, and capture 50 ms. | D0 12 kHz, D1 6, D2 4, D3 3, D4 2.4, D5 2, D6 1.71, D7 1.5, /STROBE 1.33, /AUTOFD 1.2, /INIT 1.09, /SELIN 1.0 kHz. Clean edges. | A missing line points at its buffer pin. Two lines that look the same point at a bridge between them. |
| 9 | Loopback passes A–D below, with `scan`. | The output listed for each pass. | See "Reading scan output". |

## Jumper rules (scan)

- Wire an **output to an input** only: data → status, control → status, or control → data.
- **Never** join two control pins, or two data pins.
- **At most one jumper per input pin.**
- **Remove all jumpers** before `walk` and `pattern`.

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
| `possible short: A / B -> Y` | Two outputs reach the same input. Check the jumpers, then look for a solder bridge. |
| `weak or intermittent: A -> Y?` | The link follows only some of the time: loose jumper, or a cracked joint. |
| `stuck low` on a status pin | The pin is shorted to GND, or U3 or the line's pull-up is faulty. |
| `FAULT: ...` | Firmware hit an internal error. All buffers are off; unplug to reset, and report the message. |
````

- [ ] **Step 2: Add the README row**

After line 89 (the `models/` row) of `README.md`, insert:

```
| `firmware/` | Bring-up firmware for the Pico 2: USB serial console to test every LED, buffer and DB25 signal without a Zip drive. Checklist in `firmware/BRINGUP.md` |
```

- [ ] **Step 3: Commit**

```bash
git add firmware/BRINGUP.md README.md
git commit -m "Firmware: bring-up checklist; README file table"
```

---

### Task 9: Final verification

- [ ] **Step 1: Clean build of both targets**

```bash
rm -rf firmware/build firmware/test/build
cmake -S firmware/test -B firmware/test/build && cmake --build firmware/test/build && ctest --test-dir firmware/test/build --output-on-failure
cmake -S firmware -B firmware/build -G Ninja && ninja -C firmware/build
```

Expected: `100% tests passed, 0 tests failed out of 6`, and `zipbridge_bringup.uf2` is built.

- [ ] **Step 2: PII check before anything goes public**

```bash
git diff origin/main --stat
git diff origin/main | grep -nEi -e "/Users/" -e "@gmail" -e "$(git config --global user.email)" || echo "clean"
git log origin/main..HEAD --format='%an <%ae>' | sort -u
```

Expected:
- `clean`.
- Every author is `hykgoml <5400010+hykgoml@users.noreply.github.com>`.

- [ ] **Step 3: Ask the user before pushing**

The repo is public, so never push without a yes.
