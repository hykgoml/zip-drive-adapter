#include "board.h"
#include "fake_board.h"
#include "gpio_hal.h"
#include "test.h"

int test_failures;

/* Enable U1, U2, U3 by hand; data direction as given. */
static void enable_buffers(int data_out) {
    hal_put_masked(U1_OE_MASK | U23_OE_MASK | U1_DIR_MASK | CTRL_MASK,
                   (data_out ? U1_DIR_MASK : 0) | CTRL_MASK);
    hal_set_output(U1_OE_MASK | U23_OE_MASK | U1_DIR_MASK | CTRL_MASK, true);
}

static void test_power_on(void) {
    fake_reset(1);
    CHECK(!fake_u1_enabled());
    CHECK(!fake_u23_enabled());
    CHECK(fake_cable_level(1) == -1);  /* /STROBE not driven */
    CHECK(fake_cable_level(10) == 1);  /* /ACK has its pull-up */
    CHECK((hal_get_all() & STATUS_MASK) == 0); /* U3 off: Pico pull-downs */
    CHECK((hal_get_all() & (U1_OE_MASK | U23_OE_MASK)) == (U1_OE_MASK | U23_OE_MASK));
}

static void test_status_pullups(void) {
    fake_reset(1);
    enable_buffers(0);
    CHECK((hal_get_all() & STATUS_MASK) == STATUS_MASK);
}

static void test_ctrl_jumper_to_status(void) {
    fake_reset(1);
    fake_jumper(1, 10); /* /STROBE -> /ACK */
    enable_buffers(0);
    hal_put_masked(1u << PIN_CTRL_BASE, 0);
    CHECK(fake_cable_level(1) == 0);
    CHECK((hal_get_all() & 1u) == 0);
    hal_put_masked(1u << PIN_CTRL_BASE, 1u << PIN_CTRL_BASE);
    CHECK((hal_get_all() & 1u) == 1u);
}

static void test_data_out_to_status(void) {
    fake_reset(1);
    fake_jumper(2, 10); /* D0 -> /ACK */
    enable_buffers(1);
    hal_put_masked(DATA_MASK, 0);
    hal_set_output(DATA_MASK, true);
    CHECK(fake_cable_level(2) == 0);
    CHECK((hal_get_all() & 1u) == 0);
    CHECK(fake_data_driven);
    CHECK(fake_fights == 0);
}

static void test_ctrl_to_data_in(void) {
    fake_reset(1);
    fake_jumper(1, 2); /* /STROBE -> D0 */
    enable_buffers(0);
    hal_put_masked(1u << PIN_CTRL_BASE, 0);
    CHECK(((hal_get_all() >> PIN_DATA_BASE) & 1u) == 0);
    hal_put_masked(1u << PIN_CTRL_BASE, 1u << PIN_CTRL_BASE);
    CHECK(((hal_get_all() >> PIN_DATA_BASE) & 1u) == 1u);
}

static void test_floating_data_changes(void) {
    fake_reset(1);
    enable_buffers(0);
    int seen0 = 0, seen1 = 0;
    for (int i = 0; i < 200; i++) {
        if ((hal_get_all() >> PIN_DATA_BASE) & 1u) seen1 = 1; else seen0 = 1;
    }
    CHECK(seen0 && seen1);
}

static void test_fight_counted(void) {
    fake_reset(1);
    enable_buffers(0);               /* U1 drives the Pico side */
    hal_set_output(DATA_MASK, true); /* and so does the Pico: a fight */
    CHECK(fake_fights > 0);
}

static void test_contention_timed(void) {
    fake_reset(1);
    enable_buffers(0);
    hal_put_masked(CTRL_MASK, 0x0Eu << PIN_CTRL_BASE); /* /STROBE 0, others 1 */
    hal_delay_us(30);
    hal_put_masked(CTRL_MASK, CTRL_MASK);
    CHECK(fake_max_contention_us() >= 30);
    fake_contention_reset();
    CHECK(fake_max_contention_us() == 0);
}

static void test_stuck_pin(void) {
    fake_reset(1);
    fake_stuck(11, 0); /* BUSY */
    enable_buffers(0);
    CHECK((hal_get_all() & STATUS_MASK) == 0x1Du);
}

static void test_two_drivers_alternate(void) {
    fake_reset(1);
    fake_jumper(1, 10);
    fake_jumper(14, 10); /* /STROBE and /AUTOFD both on /ACK */
    enable_buffers(0);
    hal_put_masked(CTRL_MASK, 0x0Eu << PIN_CTRL_BASE); /* they disagree */
    CHECK(fake_cable_level(10) == -2);
    uint32_t a = hal_get_all() & 1u, b = hal_get_all() & 1u;
    CHECK(a != b);
}

static void test_flaky_alternates(void) {
    fake_reset(1);
    fake_jumper(1, 10);
    fake_flaky(10);
    enable_buffers(0);
    hal_put_masked(1u << PIN_CTRL_BASE, 0);
    uint32_t a = hal_get_all() & 1u, b = hal_get_all() & 1u;
    CHECK(a != b);
}

static void test_log_records_order(void) {
    fake_reset(1);
    hal_put_masked(CTRL_MASK, CTRL_MASK);
    hal_delay_us(7);
    CHECK(fake_log_len == 2);
    CHECK(fake_log[0].op == FAKE_PUT && fake_log[0].mask == CTRL_MASK && fake_log[0].value == CTRL_MASK);
    CHECK(fake_log[1].op == FAKE_DELAY && fake_log[1].value == 7);
    CHECK(fake_log[1].t - fake_log[0].t == 7);
}

static void test_contended_wire_reads_agree(void) {
    fake_reset(1);
    fake_jumper(1, 10);
    fake_jumper(14, 10);
    fake_jumper(11, 10); /* /STROBE, /AUTOFD, /ACK and BUSY on one wire */
    enable_buffers(0);
    hal_put_masked(CTRL_MASK, 0x0Eu << PIN_CTRL_BASE); /* /STROBE 0, /AUTOFD 1 */
    int seen0 = 0, seen1 = 0;
    for (int i = 0; i < 6; i++) {
        uint32_t v = hal_get_all();
        uint32_t ack = v & 1u, busy = (v >> 1) & 1u;
        CHECK(ack == busy);
        if (ack) seen1 = 1; else seen0 = 1;
    }
    CHECK(seen0 && seen1);
}

static void test_floating_wire_reads_agree(void) {
    fake_reset(1);
    fake_jumper(2, 3); /* D0 and D1 on one floating wire */
    enable_buffers(0);
    int seen0 = 0, seen1 = 0;
    for (int i = 0; i < 200; i++) {
        uint32_t v = hal_get_all() >> PIN_DATA_BASE;
        CHECK((v & 1u) == ((v >> 1) & 1u));
        if (v & 1u) seen1 = 1; else seen0 = 1;
    }
    CHECK(seen0 && seen1);
}

static void test_status_side_fight(void) {
    fake_reset(1);
    enable_buffers(0);
    CHECK(fake_fights == 0);
    hal_set_output(1u << PIN_STATUS_BASE, true); /* U3 drives GP0 and so does the Pico */
    CHECK(fake_fights > 0);
}

static void test_no_pull_floats(void) {
    fake_reset(1);
    enable_buffers(0);
    hal_set_output(1u << PIN_CTRL_BASE, false);
    hal_set_pulls(1u << PIN_CTRL_BASE, false, false);
    CHECK(fake_cable_level(1) == -1);
    hal_set_pulls(1u << PIN_CTRL_BASE, true, false);
    CHECK(fake_cable_level(1) == 1);
    hal_set_pulls(1u << PIN_CTRL_BASE, false, true);
    CHECK(fake_cable_level(1) == 0);
}

int main(void) {
    RUN(test_power_on);
    RUN(test_status_pullups);
    RUN(test_ctrl_jumper_to_status);
    RUN(test_data_out_to_status);
    RUN(test_ctrl_to_data_in);
    RUN(test_floating_data_changes);
    RUN(test_fight_counted);
    RUN(test_contention_timed);
    RUN(test_stuck_pin);
    RUN(test_two_drivers_alternate);
    RUN(test_flaky_alternates);
    RUN(test_log_records_order);
    RUN(test_contended_wire_reads_agree);
    RUN(test_floating_wire_reads_agree);
    RUN(test_status_side_fight);
    RUN(test_no_pull_floats);
    return test_report();
}
