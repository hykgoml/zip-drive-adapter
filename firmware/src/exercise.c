#include "exercise.h"

#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "bus.h"
#include "console.h"
#include "gpio_hal.h"
#include "io.h"
#include "leds.h"
#include "scan.h"

const char JUMPER_WARNING[] = "Remove all loopback jumpers. Enter to start, any other key cancels.\n";

#define N_OUT (DATA_COUNT + CTRL_COUNT)

/* NULL when driving started. Refuses (ending in bus_safe) if control -> data
 * jumpers are fitted: data driven then would fight the control buffer. After a
 * fault it also returns NULL, so the console reports the fault itself. */
static const char *start_driving(bool force) {
    bus_safe();
    bus_oe_data(true);
    if (bus_faulted()) return NULL;
    const char *err = force ? NULL : scan_ctrl_to_data_refusal(true);
    if (err) {
        bus_safe();
        return bus_faulted() ? NULL : err;
    }
    bus_data_dir_out(0);
    return NULL;
}

static void warn(bool force) {
    out_printf("%s", JUMPER_WARNING);
    if (force) out_printf("force: jumper check skipped\n");
}

const char *exercise_walk(uint32_t ms, bool force) {
    warn(force);
    if (!console_wait_enter()) return "cancelled";
    const char *err = start_driving(force);
    if (err || bus_faulted()) return err;
    bus_out_write(0, 0);
    bool stopped = false;
    for (unsigned i = 0; i < N_OUT && !stopped && !bus_faulted(); i++) {
        bool is_data = i < DATA_COUNT;
        unsigned k = is_data ? i : i - DATA_COUNT;
        bus_out_write(is_data ? (uint8_t)(1u << k) : 0, is_data ? 0 : (uint8_t)(1u << k));
        if (bus_faulted()) break;
        leds_flash_write();
        out_printf("%s = pin %u high\n", is_data ? DATA_NAME[k] : CTRL_NAME[k],
                   (unsigned)(is_data ? DATA_DB25[k] : CTRL_DB25[k]));
        for (uint32_t t = 0; t < ms; t += 10) {
            if (console_poll_key() >= 0 || !io_connected()) {
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

const char *exercise_pattern(bool force) {
    warn(force);
    if (!console_wait_enter()) return "cancelled";
    const char *err = start_driving(force);
    if (err || bus_faulted()) return err;
    out_printf("pattern running; any key stops\n");
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
        if (bus_faulted()) break;
        if (tick - last_poll >= 1024u) { /* about every 43 ms */
            last_poll = tick;
            leds_flash_write();
            if (console_poll_key() >= 0 || bus_faulted() || !io_connected()) break;
        }
    }
    bus_safe();
    out_printf("stopped\n");
    return NULL;
}
