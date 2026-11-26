#ifndef SCAN_H
#define SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"

#define SCAN_OUTPUTS (DATA_COUNT + CTRL_COUNT)  /* 0..7 = D0..D7, 8..11 = /STROBE../SELIN */
#define SCAN_INPUTS  (DATA_COUNT + STATUS_COUNT) /* 0..7 = D0..D7, 8..12 = /ACK../ERROR */

typedef struct {
    bool link[SCAN_OUTPUTS][SCAN_INPUTS];    /* matched on all 8 samples (any phase) */
    bool unclear[SCAN_OUTPUTS][SCAN_INPUTS]; /* changed but didn't match */
    uint16_t sampled, seen0, seen1;          /* bit per input */
    bool phase2_skipped;
    bool aborted;
} scan_result_t;

extern const char SCAN_RULES[];

void scan_measure(scan_result_t *r);      /* both phases; ends in bus_safe() */
void scan_report(const scan_result_t *r); /* prints the report */
const char *scan_run(void);               /* rules + measure + report (console) */

#endif
