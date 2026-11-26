#include <stdlib.h>

#include "board.h"
#include "bus.h"
#include "console.h"
#include "exercise.h"
#include "fake_board.h"
#include "fake_io.h"
#include "leds.h"
#include "test.h"

int test_failures;

static void setup(void) {
    fake_reset(1);
    fake_io_reset();
    leds_init();
    bus_init();
    console_init();
    fake_log_clear();
}

static void check_safe(void) {
    CHECK(!fake_u1_enabled() && fake_u23_enabled());
    CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
    CHECK(fake_fights == 0);
    CHECK(!fake_log_overflow);
}

/* Snapshot of the 12 outputs (D0..D7, /STROBE../SELIN) at each delay, when all are driven. */
static int snaps[64][12];
static int nsnap;

static void snap(const fake_call_t *c) {
    if (c->op != FAKE_DELAY) return;
    int lv[12];
    for (unsigned k = 0; k < 12; k++) {
        lv[k] = fake_cable_level(k < DATA_COUNT ? DATA_DB25[k] : CTRL_DB25[k - DATA_COUNT]);
        if (lv[k] < 0) return;
    }
    if (nsnap > 0 && memcmp(snaps[nsnap - 1], lv, sizeof lv) == 0) return;
    if (nsnap < 64) memcpy(snaps[nsnap++], lv, sizeof lv);
}

static void test_walk_one_hot(void) {
    setup();
    fake_io_push("\r");
    nsnap = 0;
    fake_on_op = snap;
    const char *err = exercise_walk(100);
    fake_on_op = 0;
    CHECK(err == NULL);
    CHECK(nsnap == 12);
    for (int s = 0; s < nsnap; s++)
        for (int k = 0; k < 12; k++) CHECK(snaps[s][k] == (k == s));
    const char *out = fake_io_output();
    CHECK_CONTAINS(out, "Remove all loopback jumpers");
    CHECK_CONTAINS(out, "D0 = pin 2 high\n");
    CHECK_CONTAINS(out, "D7 = pin 9 high\n");
    CHECK_CONTAINS(out, "/STROBE = pin 1 high\n");
    CHECK_CONTAINS(out, "/SELIN = pin 17 high\n");
    check_safe();
}

static void test_walk_step_time(void) {
    setup();
    fake_io_push("\r");
    exercise_walk(250);
    uint64_t t[16];
    int n = 0;
    for (int i = 0; i < fake_log_len && n < 16; i++)
        if (fake_log[i].op == FAKE_PUT && fake_log[i].mask == (DATA_MASK | CTRL_MASK)) t[n++] = fake_log[i].t;
    CHECK(n == 13); /* all-low start + 12 steps */
    for (int i = 2; i < n; i++) CHECK(t[i] - t[i - 1] >= 250000 && t[i] - t[i - 1] <= 252000);
}

static void test_walk_cancel(void) {
    setup();
    fake_io_push("x");
    CHECK(strcmp(exercise_walk(100), "cancelled") == 0);
    CHECK(!fake_data_driven);
    check_safe();
}

static void test_walk_key_stops(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(15); /* ESC after 15 empty polls: during step 2 */
    CHECK(exercise_walk(100) == NULL);
    CHECK_CONTAINS(fake_io_output(), "D1 = pin 3 high\n");
    CHECK_LACKS(fake_io_output(), "D2 = pin 4 high");
    CHECK_CONTAINS(fake_io_output(), "stopped\n");
    check_safe();
}

static void test_pattern_frequencies(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(3); /* stops after about 170 ms */
    CHECK(exercise_pattern() == NULL);
    uint64_t t_first = 0, t_last = 0;
    uint32_t prev = 0;
    int toggles[12] = {0};
    bool have = false;
    for (int i = 0; i < fake_log_len; i++) {
        const fake_call_t *c = &fake_log[i];
        if (c->op != FAKE_PUT || c->mask != (DATA_MASK | CTRL_MASK)) continue;
        uint32_t lines = ((c->value >> PIN_DATA_BASE) & 0xFFu) | (((c->value >> PIN_CTRL_BASE) & 0xFu) << 8);
        if (have) {
            for (int k = 0; k < 12; k++) toggles[k] += (int)(((lines ^ prev) >> k) & 1u);
            t_last = c->t;
        } else {
            t_first = c->t;
            have = true;
        }
        prev = lines;
    }
    double secs = (double)(t_last - t_first) / 1e6;
    CHECK(secs > 0.1);
    for (int k = 0; k < 12; k++) {
        double want = secs * 24000.0 / (k + 1); /* line k+1 toggles every k+1 ticks */
        if (abs(toggles[k] - (int)want) > (int)(want * 0.03) + 2) {
            fprintf(stderr, "    line %d: %d toggles, want about %.0f\n", k + 1, toggles[k], want);
            test_failures++;
        }
    }
    CHECK_CONTAINS(fake_io_output(), "stopped\n");
    check_safe();
}

static void test_pattern_cancel(void) {
    setup();
    fake_io_push("q");
    CHECK(strcmp(exercise_pattern(), "cancelled") == 0);
    CHECK(!fake_data_driven);
    check_safe();
}

int main(void) {
    RUN(test_walk_one_hot);
    RUN(test_walk_step_time);
    RUN(test_walk_cancel);
    RUN(test_walk_key_stops);
    RUN(test_pattern_frequencies);
    RUN(test_pattern_cancel);
    return test_report();
}
