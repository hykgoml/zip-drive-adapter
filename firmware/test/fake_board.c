#include "fake_board.h"

#include <assert.h>

#include "board.h"
#include "gpio_hal.h"

fake_call_t fake_log[FAKE_LOG_MAX];
int fake_log_len;
bool fake_log_overflow;
void (*fake_on_op)(const fake_call_t *call);
int fake_fights;
bool fake_data_driven;

#define NPINS 26 /* DB25 pins 1..25; index 0 unused */
#define LEVEL_CONTENDED 2

static uint32_t oe, out, pull_up, pull_down; /* Pico: output enables, output latch, pulls */
static uint64_t now;
static int parent[NPINS];      /* jumper nets (union-find) */
static int stuck[NPINS];       /* -1 = not stuck */
static int float_level[NPINS]; /* per net (indexed by root): level of a floating wire */
static unsigned contend_count[NPINS]; /* per net: reads of a contended wire so far */
static unsigned epoch;         /* incremented once per hal_get_all */
static unsigned net_epoch[NPINS]; /* per net: epoch of the cached value */
static int net_value[NPINS];      /* per net: value read in that epoch */
static bool flaky[NPINS];
static unsigned flaky_count[NPINS];
static uint32_t rng;
static bool in_cont;
static uint64_t cont_start, cont_max;

static uint32_t rng_next(void) { /* xorshift32 */
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int find(int p) {
    while (parent[p] != p) p = parent[p];
    return p;
}

static bool is_output(unsigned g) { return (oe >> g) & 1u; }

/* What the Pico itself puts on GPIO g: its output, else its pull-up (1) or
 * pull-down (0); -1 if it floats (no pull). */
static int pico_level(unsigned g) {
    if (is_output(g)) return (int)((out >> g) & 1u);
    if ((pull_up >> g) & 1u) return 1;
    if ((pull_down >> g) & 1u) return 0;
    return -1;
}

bool fake_u1_enabled(void) { return is_output(PIN_U1_OE) && pico_level(PIN_U1_OE) == 0; }
bool fake_u23_enabled(void) { return is_output(PIN_U23_OE) && pico_level(PIN_U23_OE) == 0; }
static bool u1_dir_out(void) { return is_output(PIN_U1_DIR) && pico_level(PIN_U1_DIR) == 1; }

/* Level a buffer drives onto DB25 pin p, or -1 if nothing drives it. */
static int driven(int p) {
    for (unsigned k = 0; k < CTRL_COUNT; k++)
        if (CTRL_DB25[k] == p) return fake_u23_enabled() ? pico_level(PIN_CTRL_BASE + k) : -1;
    for (unsigned k = 0; k < DATA_COUNT; k++)
        if (DATA_DB25[k] == p)
            return (fake_u1_enabled() && u1_dir_out()) ? pico_level(PIN_DATA_BASE + k) : -1;
    /* a floating buffer input (pico_level -1) drives nothing defined */
    return -1;
}

static bool is_status_pin(int p) {
    for (unsigned k = 0; k < STATUS_COUNT; k++)
        if (STATUS_DB25[k] == p) return true;
    return false;
}

/* 0, 1, -1 = floating, LEVEL_CONTENDED = drivers disagree. */
static int net_level(int p) {
    int root = find(p), lv = -1;
    bool pulled = false;
    for (int q = 1; q < NPINS; q++) {
        if (find(q) != root) continue;
        if (stuck[q] >= 0) return stuck[q];
        if (is_status_pin(q)) pulled = true;
        int d = driven(q);
        if (d < 0) continue;
        if (lv < 0) lv = d;
        else if (lv != d) return LEVEL_CONTENDED;
    }
    if (lv >= 0) return lv;
    return pulled ? 1 : -1;
}

/* What a buffer input on DB25 pin p reads. Contended and floating wires get
 * one value per wire per hal_get_all, so every pin on the wire agrees. */
static int read_cable(int p) {
    int lv = net_level(p);
    if (lv == LEVEL_CONTENDED || lv < 0) {
        int root = find(p);
        if (net_epoch[root] != epoch) {
            net_epoch[root] = epoch;
            if (lv == LEVEL_CONTENDED) {
                net_value[root] = (int)(contend_count[root]++ & 1u); /* mid-level: alternates */
            } else {
                if ((rng_next() & 7u) == 0) float_level[root] ^= 1; /* drifts now and then */
                net_value[root] = float_level[root];
            }
        }
        lv = net_value[root];
    }
    if (flaky[p] && (flaky_count[p]++ & 1u) == 0) lv = 1; /* contact open: pull-up */
    return lv;
}

static void after_op(fake_op_t op, uint32_t mask, uint32_t value) {
    fake_call_t call = {op, mask, value, now};
    if (fake_log_len < FAKE_LOG_MAX) fake_log[fake_log_len++] = call;
    else fake_log_overflow = true;

    if (fake_u1_enabled() && !u1_dir_out() && (oe & DATA_MASK)) fake_fights++;
    if (fake_u23_enabled() && (oe & STATUS_MASK)) fake_fights++;
    if (fake_u1_enabled() && u1_dir_out()) fake_data_driven = true;

    bool any0 = false, any1 = false;
    for (unsigned k = 0; k < CTRL_COUNT + DATA_COUNT; k++) {
        int d = driven(k < CTRL_COUNT ? CTRL_DB25[k] : DATA_DB25[k - CTRL_COUNT]);
        if (d == 0) any0 = true;
        if (d == 1) any1 = true;
    }
    bool c = any0 && any1;
    if (c && !in_cont) {
        in_cont = true;
        cont_start = now;
    } else if (!c && in_cont) {
        in_cont = false;
        if (now - cont_start > cont_max) cont_max = now - cont_start;
    }
    if (fake_on_op) fake_on_op(&call);
}

void fake_reset(uint32_t seed) {
    oe = out = pull_up = 0; /* RP2350 reset: inputs with pull-downs */
    pull_down = 0xFFFFFFFFu;
    epoch = 0;
    now = 0;
    rng = seed ? seed : 1;
    for (int p = 0; p < NPINS; p++) {
        parent[p] = p;
        stuck[p] = -1;
        flaky[p] = false;
        flaky_count[p] = 0;
        contend_count[p] = 0;
        net_epoch[p] = 0;
        net_value[p] = 0;
        float_level[p] = (int)(rng_next() & 1u);
    }
    in_cont = false;
    cont_start = cont_max = 0;
    fake_log_clear();
    fake_on_op = 0;
    fake_fights = 0;
    fake_data_driven = false;
}

void fake_log_clear(void) {
    fake_log_len = 0;
    fake_log_overflow = false;
}

void fake_jumper(int a, int b) {
    assert(a >= 1 && a <= 25 && b >= 1 && b <= 25);
    parent[find(a)] = find(b);
}

void fake_stuck(int p, int level) {
    assert(p >= 1 && p <= 25);
    assert(level == 0 || level == 1);
    stuck[p] = level;
}

void fake_flaky(int p) {
    assert(p >= 1 && p <= 25);
    assert(is_status_pin(p));
    flaky[p] = true;
}

int fake_cable_level(int p) {
    assert(p >= 1 && p <= 25);
    int lv = net_level(p);
    return lv == LEVEL_CONTENDED ? -2 : lv;
}

uint32_t fake_out_value(void) { return out; }
uint64_t fake_now(void) { return now; }
uint32_t fake_out_enable(void) { return oe; }

uint64_t fake_max_contention_us(void) {
    uint64_t m = cont_max;
    if (in_cont && now - cont_start > m) m = now - cont_start;
    return m;
}

void fake_contention_reset(void) {
    cont_max = 0;
    if (in_cont) cont_start = now;
}

/* ---- gpio_hal ---- */

void hal_init(uint32_t mask) {
    now++;
    oe &= ~mask;
    out &= ~mask;
    after_op(FAKE_INIT, mask, 0);
}

void hal_set_output(uint32_t mask, bool o) {
    now++;
    if (o) oe |= mask; else oe &= ~mask;
    after_op(FAKE_SET_OUTPUT, mask, o ? 1u : 0u);
}

void hal_set_pulls(uint32_t mask, bool up, bool down) {
    now++;
    if (up) pull_up |= mask; else pull_up &= ~mask;
    if (down) pull_down |= mask; else pull_down &= ~mask;
    after_op(FAKE_SET_PULLS, mask, (up ? 1u : 0u) | (down ? 2u : 0u));
}

void hal_put_masked(uint32_t mask, uint32_t value) {
    now++;
    out = (out & ~mask) | (value & mask);
    after_op(FAKE_PUT, mask, value & mask);
}

uint32_t hal_get_all(void) {
    now++;
    epoch++;
    uint32_t v = 0;
    for (unsigned g = 0; g < 30; g++) {
        int lv = pico_level(g);
        if (!is_output(g)) {
            if (g < PIN_STATUS_BASE + STATUS_COUNT) { /* PIN_STATUS_BASE is 0 */
                if (fake_u23_enabled()) lv = read_cable(STATUS_DB25[g - PIN_STATUS_BASE]);
            } else if (g >= PIN_DATA_BASE && g < PIN_DATA_BASE + DATA_COUNT) {
                if (fake_u1_enabled() && !u1_dir_out()) lv = read_cable(DATA_DB25[g - PIN_DATA_BASE]);
            } else if (g == PIN_U1_OE || g == PIN_U23_OE) {
                lv = 1; /* R14 / R15 */
            }
        }
        if (lv > 0) v |= 1u << g; /* floating (-1) reads as 0 */
    }
    return v;
}

void hal_delay_us(uint32_t us) {
    now += us;
    after_op(FAKE_DELAY, 0, us);
}

uint64_t hal_time_us(void) { return now++; }

const char *hal_chip_name(void) { return "fake RP2350"; }
