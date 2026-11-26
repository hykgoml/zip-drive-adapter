#ifndef FAKE_BOARD_H
#define FAKE_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* A simulated zipbridge board behind gpio_hal: the three buffers, the status
 * pull-ups, jumpers between DB25 pins, stuck pins, flaky contacts and
 * floating data lines. Every state-changing GPIO call is logged with a fake
 * timestamp (1 us per call, plus delays). Reads are not logged. Each hal_get_all
 * consumes simulation state (drift, alternation and flaky counters), and
 * fake_on_op hooks may only call fake_* getters, not hal_*. */

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

/* Number of logged calls after which two drivers were on one wire: U1 enabled
 * with DIR = cable->Pico while a data GPIO was a Pico output, or U3 enabled
 * while a status GPIO (GP0-GP4) was a Pico output. Must stay 0. */
extern int fake_fights;
/* Set once U1 drives the cable (enabled, DIR = Pico->cable). */
extern bool fake_data_driven;

void fake_reset(uint32_t seed);  /* power-on state, no wiring, empty log, hooks cleared */
void fake_log_clear(void);
void fake_jumper(int db25_a, int db25_b); /* join two DB25 pins */
void fake_stuck(int db25_pin, int level); /* force a pin's level (e.g. shorted to GND) */
void fake_flaky(int db25_pin);            /* flaky contact at a status input: every other read of that pin sees its pull-up (1) */

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
