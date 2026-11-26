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
