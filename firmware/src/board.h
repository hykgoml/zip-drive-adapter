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
