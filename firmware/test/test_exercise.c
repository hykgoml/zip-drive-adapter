#include <stdlib.h>

#include "board.h"
#include "bus.h"
#include "console.h"
#include "exercise.h"
#include "fake_board.h"
#include "fake_io.h"
#include "io.h"
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
    const char *err = exercise_walk(100, false);
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
    exercise_walk(250, false);
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
    CHECK(strcmp(exercise_walk(100, false), "cancelled") == 0);
    CHECK(!fake_data_driven);
    check_safe();
}

static void test_walk_key_stops(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(15); /* ESC after 15 empty polls: during step 2 */
    CHECK(exercise_walk(100, false) == NULL);
    CHECK_CONTAINS(fake_io_output(), "D1 = pin 3 high\n");
    CHECK_LACKS(fake_io_output(), "D2 = pin 4 high");
    CHECK_CONTAINS(fake_io_output(), "stopped\n");
    check_safe();
}

static void test_pattern_frequencies(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(3); /* stops after about 170 ms */
    CHECK(exercise_pattern(false) == NULL);
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
    CHECK(strcmp(exercise_pattern(false), "cancelled") == 0);
    CHECK(!fake_data_driven);
    check_safe();
}

/* WRITE LED must be lit at some delay in every walk step. */
static int write_seen[16];
static int write_step = -1;
static int last_cur[12];

static void snap_write(const fake_call_t *c) {
    if (c->op != FAKE_DELAY) return;
    int lv[12];
    for (unsigned k = 0; k < 12; k++) {
        lv[k] = fake_cable_level(k < DATA_COUNT ? DATA_DB25[k] : CTRL_DB25[k - DATA_COUNT]);
        if (lv[k] < 0) return;
    }
    int hot = -1, n = 0;
    for (int k = 0; k < 12; k++)
        if (lv[k]) {
            hot = k;
            n++;
        }
    if (n != 1) return;
    if (fake_out_value() & (1u << PIN_LED_WRITE)) write_seen[hot] = 1;
    (void)last_cur;
    (void)write_step;
}

static void test_walk_write_led(void) {
    setup();
    fake_io_push("\r");
    memset(write_seen, 0, sizeof write_seen);
    fake_on_op = snap_write;
    exercise_walk(100, false);
    fake_on_op = 0;
    for (int k = 0; k < 12; k++) CHECK(write_seen[k]);
    check_safe();
}

static void test_walk_poll_spacing(void) {
    setup();
    fake_io_push("\r");
    exercise_walk(100, false);
    int n = fake_io_poll_count();
    CHECK(n > 100);
    for (int i = 1; i < n; i++) CHECK(fake_io_poll_time(i) - fake_io_poll_time(i - 1) <= 10500);
}

static void test_pattern_poll_spacing_and_led(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(20);
    exercise_pattern(false);
    int n = fake_io_poll_count();
    CHECK(n > 10);
    for (int i = 1; i < n; i++) CHECK(fake_io_poll_time(i) - fake_io_poll_time(i - 1) <= 50000);
    check_safe();
}

static void fault_on_first_put(const fake_call_t *c) {
    if (c->op != FAKE_PUT || c->mask != (DATA_MASK | CTRL_MASK)) return;
    fake_on_op = 0;
    bus_data_dir_in();
    bus_data_write(1);
}

static void test_walk_fault_stops(void) {
    setup();
    fake_io_push("\r");
    fake_on_op = fault_on_first_put;
    CHECK(exercise_walk(100, false) == NULL);
    fake_on_op = 0;
    CHECK(bus_faulted());
    CHECK_LACKS(fake_io_output(), " high\n");
    CHECK(fake_fights == 0);
}

static int put_count, fault_at;

static void fault_on_nth_put(const fake_call_t *c) {
    if (c->op != FAKE_PUT || c->mask != (DATA_MASK | CTRL_MASK)) return;
    if (++put_count < fault_at) return;
    fake_on_op = 0;
    bus_data_dir_in();
    bus_data_write(1);
}

static void test_walk_fault_later_step(void) {
    setup();
    fake_io_push("\r");
    put_count = 0;
    fault_at = 4; /* all-low, D0, D1, then D2's write faults */
    fake_on_op = fault_on_nth_put;
    exercise_walk(100, false);
    fake_on_op = 0;
    CHECK(bus_faulted());
    CHECK_CONTAINS(fake_io_output(), "D1 = pin 3 high\n");
    CHECK_LACKS(fake_io_output(), "D2 = pin 4 high");
}

static void test_pattern_fault_stops(void) {
    setup();
    fake_io_push("\r");
    fake_on_op = fault_on_first_put;
    exercise_pattern(false);
    fake_on_op = 0;
    CHECK(bus_faulted());
    int puts = 0;
    for (int i = 0; i < fake_log_len; i++)
        if (fake_log[i].op == FAKE_PUT && fake_log[i].mask == (DATA_MASK | CTRL_MASK)) puts++;
    CHECK(puts <= 2);
}

static void test_pattern_fault_after_put(void) {
    setup();
    fake_io_push("\r");
    put_count = 0;
    fault_at = 3;
    fake_on_op = fault_on_nth_put;
    exercise_pattern(false);
    fake_on_op = 0;
    int puts = 0;
    for (int i = 0; i < fake_log_len; i++)
        if (fake_log[i].op == FAKE_PUT && fake_log[i].mask == (DATA_MASK | CTRL_MASK)) puts++;
    CHECK(bus_faulted());
    CHECK(puts <= 4); /* stops at the write that faulted, not at the next poll */
}

/* ---- USB disconnect ---- */
static int hook_delays, hook_puts, hook_after;
static void disconnect_after_delays(const fake_call_t *c) {
    if (c->op == FAKE_DELAY && c->value >= 10000 && ++hook_delays >= hook_after) {
        fake_on_op = 0;
        fake_io_set_connected(false);
    }
}
static void disconnect_after_puts(const fake_call_t *c) {
    if (c->op == FAKE_PUT && c->mask == (DATA_MASK | CTRL_MASK) && ++hook_puts >= hook_after) {
        fake_on_op = 0;
        fake_io_set_connected(false);
    }
}
static void disconnect_in_wait(const fake_call_t *c) {
    if (c->op == FAKE_DELAY && c->value == 1000 && ++hook_delays >= hook_after) {
        fake_on_op = 0;
        fake_io_set_connected(false);
    }
}

static void test_disconnect_while_waiting_for_enter(void) {
    for (int which = 0; which < 2; which++) {
        setup();
        fake_io_set_idle_limit(1000000); /* never the idle ESC */
        hook_delays = 0;
        hook_after = 5;
        fake_on_op = disconnect_in_wait;
        const char *err = which ? exercise_pattern(false) : exercise_walk(100, false);
        fake_on_op = 0;
        CHECK(err != NULL && strcmp(err, "cancelled") == 0);
        CHECK(!fake_data_driven);
        check_safe();
    }
}

static void test_disconnect_mid_walk(void) {
    setup();
    fake_io_push("\r");
    hook_delays = 0;
    hook_after = 30; /* about 3 steps of 100 ms */
    fake_on_op = disconnect_after_delays;
    CHECK(exercise_walk(100, false) == NULL);
    fake_on_op = 0;
    CHECK(!io_connected());
    CHECK(fake_data_driven);
    CHECK(count_of(fake_io_output(), " high\n") < 12);
    check_safe();
}

static void test_disconnect_mid_pattern(void) {
    setup();
    fake_io_push("\r");
    fake_io_set_idle_limit(1000000);
    hook_puts = 0;
    hook_after = 3000;
    fake_on_op = disconnect_after_puts;
    CHECK(exercise_pattern(false) == NULL);
    fake_on_op = 0;
    CHECK(!io_connected());
    CHECK(fake_data_driven);
    check_safe();
}

/* ---- control -> data jumpers: refuse to drive data ---- */
static void test_walk_refused_with_ctrl_data_jumper(void) {
    setup();
    fake_jumper(1, 6); /* pass D */
    fake_jumper(14, 7);
    fake_io_push("\r");
    fake_contention_reset();
    const char *err = exercise_walk(100, false);
    CHECK(err != NULL && strstr(err, "control -> data link seen: /STROBE (pin 1) -> D4 (pin 6)") != NULL);
    CHECK(strstr(err, "(+1 more") != NULL && strstr(err, "'walk force' / 'pattern force'") != NULL);
    CHECK_CONTAINS(fake_io_output(), "control -> data link: /STROBE (pin 1) -> D4 (pin 6)\n");
    CHECK_CONTAINS(fake_io_output(), "control -> data link: /AUTOFD (pin 14) -> D5 (pin 7)\n");
    CHECK(!fake_data_driven);
    CHECK(fake_max_contention_us() <= 10 && fake_max_contention_irq_on_us() == 0);
    check_safe();
}

static void test_walk_force_skips_check(void) {
    setup();
    fake_jumper(1, 6);
    fake_io_push("\r");
    CHECK(exercise_walk(100, true) == NULL);
    CHECK(fake_data_driven);
    CHECK_CONTAINS(fake_io_output(), "force: jumper check skipped\n");
    CHECK_CONTAINS(fake_io_output(), "Remove all loopback jumpers");
}

static void test_force_still_needs_enter(void) {
    setup();
    fake_jumper(1, 6);
    fake_io_push("x");
    CHECK(strcmp(exercise_walk(100, true), "cancelled") == 0);
    CHECK(!fake_data_driven);
    setup();
    fake_io_push("x");
    CHECK(strcmp(exercise_pattern(true), "cancelled") == 0);
    CHECK(!fake_data_driven);
}

static void test_pattern_force_skips_check(void) {
    setup();
    fake_jumper(1, 6);
    fake_io_push("\r");
    fake_io_set_idle_limit(3);
    CHECK(exercise_pattern(true) == NULL);
    CHECK(fake_data_driven);
    CHECK_CONTAINS(fake_io_output(), "force: jumper check skipped\n");
}

static void test_pattern_refused_with_ctrl_data_jumper(void) {
    setup();
    fake_jumper(1, 6);
    fake_jumper(14, 7);
    fake_io_push("\r");
    fake_contention_reset();
    const char *err = exercise_pattern(false);
    CHECK(err != NULL && strstr(err, "control -> data link seen: /STROBE (pin 1) -> D4 (pin 6)") != NULL);
    CHECK(!fake_data_driven);
    CHECK_LACKS(fake_io_output(), "pattern running");
    CHECK(fake_max_contention_us() <= 10 && fake_max_contention_irq_on_us() == 0);
    check_safe();
}

int main(void) {
    RUN(test_disconnect_while_waiting_for_enter);
    RUN(test_disconnect_mid_walk);
    RUN(test_disconnect_mid_pattern);
    RUN(test_walk_refused_with_ctrl_data_jumper);
    RUN(test_pattern_refused_with_ctrl_data_jumper);
    RUN(test_walk_force_skips_check);
    RUN(test_force_still_needs_enter);
    RUN(test_pattern_force_skips_check);
    RUN(test_walk_one_hot);
    RUN(test_walk_step_time);
    RUN(test_walk_cancel);
    RUN(test_walk_key_stops);
    RUN(test_pattern_frequencies);
    RUN(test_pattern_cancel);
    RUN(test_walk_write_led);
    RUN(test_walk_poll_spacing);
    RUN(test_pattern_poll_spacing_and_led);
    RUN(test_walk_fault_stops);
    RUN(test_walk_fault_later_step);
    RUN(test_pattern_fault_stops);
    RUN(test_pattern_fault_after_put);
    return test_report();
}
