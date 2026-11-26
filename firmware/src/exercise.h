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
