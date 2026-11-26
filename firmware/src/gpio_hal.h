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
