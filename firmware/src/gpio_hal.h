#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include <stdbool.h>
#include <stdint.h>

void hal_init(uint32_t mask);                      /* SIO function, input, output latch 0 */
void hal_set_output(uint32_t mask, bool out);      /* pin direction */
void hal_set_pulls(uint32_t mask, bool up, bool down);
void hal_put_masked(uint32_t mask, uint32_t value); /* one write for all pins in mask */
uint32_t hal_get_all(void);
void hal_delay_us(uint32_t us); /* RP2350: busy_wait_us_32, never a sleep: scan calls it with interrupts off */
uint64_t hal_time_us(void);
uint32_t hal_irq_save(void);        /* RP2350: save_and_disable_interrupts() */
void hal_irq_restore(uint32_t state); /* RP2350: restore_interrupts(state) */
const char *hal_chip_name(void);

#endif
