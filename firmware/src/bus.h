#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

/* Parallel-port layer. All values are pin levels (0/1), never "asserted".
 * Status bit 0 = /ACK .. bit 4 = /ERROR. Ctrl bit 0 = /STROBE, 1 = /AUTOFD,
 * 2 = /INIT, 3 = /SELIN. */

/* The bus layer is main-loop only: it is not interrupt-safe. */

typedef void (*bus_fault_fn)(const char *why);

/* Power-up sequence; U2/U3 on, U1 off, data in, ctrl idle (1). Call it at
 * boot, or when the bus GPIOs are in their reset state: it relies on hal_init
 * releasing all pins at once. */
void bus_init(void);
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
