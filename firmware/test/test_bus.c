#include "board.h"
#include "bus.h"
#include "fake_board.h"
#include "gpio_hal.h"
#include "test.h"

int test_failures;

static bool init_ok;

/* U1 must stay off during init; U2/U3 only once all control lines are idle outputs. */
static void init_invariant(const fake_call_t *c) {
    (void)c;
    if (fake_u1_enabled()) init_ok = false;
    if (fake_u23_enabled() &&
        ((fake_out_enable() & CTRL_MASK) != CTRL_MASK || (fake_out_value() & CTRL_MASK) != CTRL_MASK))
        init_ok = false;
}

static void test_init_order(void) {
    fake_reset(1);
    init_ok = true;
    fake_on_op = init_invariant;
    bus_init();
    fake_on_op = 0;
    CHECK(init_ok);
    CHECK(fake_u23_enabled());
    CHECK(!fake_u1_enabled());
    CHECK(fake_fights == 0);

    const fake_call_t want[] = {
        {FAKE_INIT, BUS_MASK, 0, 0},
        {FAKE_PUT, U1_OE_MASK | U23_OE_MASK, U1_OE_MASK | U23_OE_MASK, 0},
        {FAKE_SET_OUTPUT, U1_OE_MASK | U23_OE_MASK, 1, 0},
        {FAKE_PUT, U1_DIR_MASK, 0, 0},
        {FAKE_SET_OUTPUT, U1_DIR_MASK, 1, 0},
        {FAKE_SET_OUTPUT, DATA_MASK, 0, 0},
        {FAKE_SET_PULLS, DATA_MASK, 1, 0},
        {FAKE_SET_PULLS, STATUS_MASK, 1, 0},
        {FAKE_PUT, CTRL_MASK, CTRL_MASK, 0},
        {FAKE_SET_OUTPUT, CTRL_MASK, 1, 0},
        {FAKE_PUT, U23_OE_MASK, 0, 0},
    };
    int n = (int)(sizeof want / sizeof want[0]);
    CHECK(fake_log_len == n);
    for (int i = 0; i < n && i < fake_log_len; i++) {
        if (fake_log[i].op != want[i].op || fake_log[i].mask != want[i].mask ||
            fake_log[i].value != want[i].value) {
            fprintf(stderr, "    init call %d differs\n", i);
            test_failures++;
        }
    }
    CHECK(bus_status_read() == 0x1F); /* nothing connected: pull-ups */
    CHECK(bus_ctrl_get() == 0x0F);
    CHECK(!bus_oe_data_on() && bus_oe_ctrl_on() && !bus_data_is_out());
}

static uint8_t preload_expect;
static bool preload_ok;

/* While U1 is on, no data pin may ever show the wrong level on the cable. */
static void preload_invariant(const fake_call_t *c) {
    (void)c;
    for (unsigned k = 0; k < DATA_COUNT; k++) {
        int lv = fake_cable_level(DATA_DB25[k]);
        if (lv != -1 && lv != ((preload_expect >> k) & 1)) preload_ok = false;
    }
}

static void test_dir_out_order(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    fake_log_clear();
    preload_expect = 0xA5;
    preload_ok = true;
    fake_on_op = preload_invariant;
    bus_data_dir_out(0xA5);
    fake_on_op = 0;
    CHECK(preload_ok);
    CHECK(fake_fights == 0);
    CHECK(fake_log_len == 5);
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == U1_OE_MASK && fake_log[0].value == U1_OE_MASK);
    CHECK(fake_log[1].op == FAKE_PUT && fake_log[1].mask == DATA_MASK &&
          fake_log[1].value == (0xA5u << PIN_DATA_BASE));
    CHECK(fake_log[2].op == FAKE_PUT && fake_log[2].mask == U1_DIR_MASK && fake_log[2].value == U1_DIR_MASK);
    CHECK(fake_log[3].op == FAKE_SET_OUTPUT && fake_log[3].mask == DATA_MASK && fake_log[3].value == 1);
    CHECK(fake_log[4].op == FAKE_PUT && fake_log[4].mask == U1_OE_MASK && fake_log[4].value == 0);
    CHECK(fake_u1_enabled() && bus_oe_data_on());
    CHECK(fake_cable_level(2) == 1 && fake_cable_level(3) == 0 && fake_cable_level(9) == 1);
    CHECK(bus_data_is_out());
}

static void test_dir_in_order(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0x00);
    fake_log_clear();
    bus_data_dir_in();
    CHECK(fake_fights == 0);
    CHECK(fake_log_len == 4);
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == U1_OE_MASK && fake_log[0].value == U1_OE_MASK);
    CHECK(fake_log[1].op == FAKE_SET_OUTPUT && fake_log[1].mask == DATA_MASK && fake_log[1].value == 0);
    CHECK(fake_log[2].op == FAKE_PUT && fake_log[2].mask == U1_DIR_MASK && fake_log[2].value == 0);
    CHECK(fake_log[3].op == FAKE_PUT && fake_log[3].mask == U1_OE_MASK && fake_log[3].value == 0);
    CHECK(fake_u1_enabled() && !bus_data_is_out());
}

static void test_dir_changes_u1_off(void) {
    fake_reset(1);
    bus_init();
    fake_log_clear();
    bus_data_dir_out(0x5A);
    CHECK(fake_log_len == 3);
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == DATA_MASK);
    CHECK(fake_log[1].op == FAKE_PUT && fake_log[1].mask == U1_DIR_MASK && fake_log[1].value == U1_DIR_MASK);
    CHECK(fake_log[2].op == FAKE_SET_OUTPUT && fake_log[2].mask == DATA_MASK && fake_log[2].value == 1);
    CHECK(!fake_u1_enabled());
    fake_log_clear();
    bus_data_dir_in();
    CHECK(fake_log_len == 2);
    CHECK(fake_log[0].op == FAKE_SET_OUTPUT && fake_log[0].mask == DATA_MASK && fake_log[0].value == 0);
    CHECK(fake_log[1].op == FAKE_PUT && fake_log[1].mask == U1_DIR_MASK && fake_log[1].value == 0);
    CHECK(!fake_u1_enabled());
    CHECK(fake_fights == 0);
}

static void test_oe_data_off_resets_dir(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0xFF);
    fake_log_clear();
    bus_oe_data(false);
    CHECK(fake_log_len == 3); /* OE# high, data in, DIR low: OE# stays off */
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == U1_OE_MASK && fake_log[0].value == U1_OE_MASK);
    CHECK(!fake_u1_enabled());
    CHECK(!bus_data_is_out());
    CHECK((fake_out_enable() & DATA_MASK) == 0);
    bus_oe_data(true);                /* must not start driving the cable */
    CHECK(fake_cable_level(2) == -1);
    CHECK(fake_fights == 0);
}

static void test_ctrl_write_one_put(void) {
    fake_reset(1);
    bus_init();
    fake_log_clear();
    bus_ctrl_write(0x05); /* /STROBE 1, /AUTOFD 0, /INIT 1, /SELIN 0 */
    CHECK(fake_log_len == 1 && fake_log[0].mask == CTRL_MASK);
    CHECK(bus_ctrl_get() == 0x05);
    CHECK(fake_cable_level(1) == 1 && fake_cable_level(14) == 0);
    CHECK(fake_cable_level(16) == 1 && fake_cable_level(17) == 0);
}

static void test_status_read_bits(void) {
    fake_reset(1);
    bus_init();
    fake_stuck(11, 0); /* BUSY */
    CHECK(bus_status_read() == 0x1D);
}

static void test_inputs_read_one_call(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    for (unsigned k = 0; k < DATA_COUNT; k++) fake_stuck(DATA_DB25[k], (0xA5 >> k) & 1);
    fake_stuck(11, 0); /* BUSY */
    fake_stuck(15, 0); /* /ERROR */
    CHECK(bus_inputs_read() == (uint16_t)(0xA5 | (0x0Du << 8)));
}

static void test_data_read_in(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    for (unsigned k = 0; k < DATA_COUNT; k++) fake_stuck(DATA_DB25[k], (0x3C >> k) & 1);
    CHECK(bus_data_read() == 0x3C);
}

static void test_out_write_one_put(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0);
    fake_log_clear();
    bus_out_write(0x81, 0x02);
    CHECK(fake_log_len == 1 && fake_log[0].mask == (DATA_MASK | CTRL_MASK));
    CHECK(fake_cable_level(2) == 1 && fake_cable_level(3) == 0 && fake_cable_level(9) == 1);
    CHECK(fake_cable_level(1) == 0 && fake_cable_level(14) == 1);
    CHECK(bus_ctrl_get() == 0x02);
}

static int fault_calls;
static void on_fault(const char *why) { (void)why; fault_calls++; }

static void test_fault_on_write_while_in(void) {
    fake_reset(1);
    bus_init();
    fault_calls = 0;
    bus_set_fault_handler(on_fault);
    bus_oe_data(true);
    bus_data_write(0x55);
    CHECK(bus_faulted());
    CHECK(fault_calls == 1);
    CHECK(!fake_u1_enabled() && !fake_u23_enabled());
    CHECK(!bus_data_is_out());
    CHECK(strlen(bus_fault_reason()) > 0);
    bus_safe();                    /* stays off */
    bus_oe_ctrl(true);
    CHECK(!fake_u23_enabled());
    bus_data_write(0x55);          /* no second report */
    CHECK(fault_calls == 1);
    bus_set_fault_handler(0);
    bus_init();                    /* power cycle clears it */
    CHECK(!bus_faulted());
}

static bool safe_ok;
static int fights_before;
static void safe_invariant(const fake_call_t *c) {
    (void)c;
    if (fake_u23_enabled() && (fake_out_value() & CTRL_MASK) != CTRL_MASK) safe_ok = false;
    if (fake_fights != fights_before) safe_ok = false;
}

static void test_safe(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_dir_out(0xFF);
    bus_ctrl_write(0);
    bus_oe_ctrl(false);
    safe_ok = true;
    fights_before = fake_fights;
    fake_on_op = safe_invariant;
    bus_safe();
    fake_on_op = 0;
    CHECK(safe_ok);
    CHECK(!fake_u1_enabled() && fake_u23_enabled());
    CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
    CHECK(fake_cable_level(1) == 1 && fake_cable_level(17) == 1);
    CHECK(fake_fights == 0);
}

static void test_fault_on_out_write_while_in(void) {
    fake_reset(1);
    bus_init();
    fault_calls = 0;
    bus_set_fault_handler(on_fault);
    bus_oe_data(true);
    bus_out_write(0x12, 0x03);
    CHECK(bus_faulted());
    CHECK(fault_calls == 1);
    CHECK(!fake_u1_enabled() && !fake_u23_enabled());
    CHECK(!bus_data_is_out());
    bus_set_fault_handler(0);
}

static void test_faulted_calls_do_nothing(void) {
    fake_reset(1);
    bus_init();
    bus_oe_data(true);
    bus_data_write(1); /* faults: direction is in */
    CHECK(bus_faulted());
    int len = fake_log_len;
    uint32_t ctrl_latch = fake_out_value() & CTRL_MASK;
    bus_oe_data(true);
    bus_data_dir_out(0xFF);
    bus_ctrl_write(0);
    bus_data_write(1);
    bus_out_write(0, 0);
    CHECK(fake_log_len == len);
    CHECK(!fake_u1_enabled() && !fake_u23_enabled());
    CHECK((fake_out_enable() & DATA_MASK) == 0);
    CHECK((fake_out_value() & CTRL_MASK) == ctrl_latch);
    CHECK(fake_fights == 0);
    CHECK(bus_fault_reason() != 0 && strlen(bus_fault_reason()) > 0);
}

int main(void) {
    RUN(test_inputs_read_one_call);
    RUN(test_init_order);
    RUN(test_dir_out_order);
    RUN(test_dir_in_order);
    RUN(test_dir_changes_u1_off);
    RUN(test_oe_data_off_resets_dir);
    RUN(test_ctrl_write_one_put);
    RUN(test_status_read_bits);
    RUN(test_data_read_in);
    RUN(test_out_write_one_put);
    RUN(test_fault_on_write_while_in);
    RUN(test_safe);
    RUN(test_fault_on_out_write_while_in);
    RUN(test_faulted_calls_do_nothing);
    return test_report();
}
