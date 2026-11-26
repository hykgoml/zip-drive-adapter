#include <stdio.h>

#include "gpio_hal.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
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
uint32_t hal_irq_save(void) { return save_and_disable_interrupts(); }
void hal_irq_restore(uint32_t state) { restore_interrupts(state); }

const char *hal_chip_name(void) {
    static char name[24];
    snprintf(name, sizeof name, "RP2350 A%u", (unsigned)rp2350_chip_version());
    return name;
}
