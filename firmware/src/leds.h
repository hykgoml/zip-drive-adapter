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
