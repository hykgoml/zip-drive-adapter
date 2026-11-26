#include "board.h"
#include "bus.h"
#include "console.h"
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

static void feed(const char *s) {
    for (; *s; s++) console_feed((unsigned char)*s);
}

/* Type one line (CR-terminated); returns everything printed. */
static const char *run(const char *line) {
    fake_io_clear_output();
    feed(line);
    console_feed('\r');
    return fake_io_output();
}

static int data_cable(void) {
    int v = 0;
    for (unsigned k = 0; k < DATA_COUNT; k++) {
        int lv = fake_cable_level(DATA_DB25[k]);
        if (lv < 0) return -1;
        v |= lv << k;
    }
    return v;
}

static void test_help_lists_commands(void) {
    setup();
    const char *out = run("help");
    CHECK_CONTAINS(out, "ctrl <strobe|autofd|init|selin> <0|1>");
    CHECK_CONTAINS(out, "oe <data|ctrl> <on|off>");
    CHECK_CONTAINS(out, "dir <in|out>");
    CHECK_CONTAINS(out, "data [<hex>]");
    CHECK_CONTAINS(out, "status");
    CHECK_CONTAINS(out, "info");
    CHECK_CONTAINS(out, "leds");
    CHECK_CONTAINS(out, "ok\n");
}

static void test_status_case_insensitive(void) {
    setup();
    const char *out = run("STATUS");
    CHECK_CONTAINS(out, "/ACK=1 BUSY=1 PE=1 SELECT=1 /ERROR=1\n");
    CHECK_CONTAINS(out, "ok\n");
}

static void test_status_shows_levels(void) {
    setup();
    fake_stuck(12, 0); /* PE */
    CHECK_CONTAINS(run("status"), "/ACK=1 BUSY=1 PE=0 SELECT=1 /ERROR=1\n");
}

static void test_unknown_command(void) {
    setup();
    const char *out = run("frob");
    CHECK_CONTAINS(out, "error: unknown command 'frob'");
    CHECK(fake_log_len == 0);
}

static void test_bad_arguments_change_nothing(void) {
    static const char *const bad[] = {
        "ctrl", "ctrl strobe", "ctrl strobe 2", "ctrl foo 1", "ctrl strobe 1 1",
        "oe", "oe data", "oe data maybe", "oe disk on",
        "dir", "dir sideways", "dir in out",
        "data 1ff", "data zz", "data 0x", "data 12 34",
        "status x", "info x", "leds x", "help x y",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        setup();
        const char *out = run(bad[i]);
        if (!strstr(out, "error: ") || !strstr(out, "usage: ") || fake_log_len != 0) {
            fprintf(stderr, "    '%s' was not rejected cleanly:\n%s\n", bad[i], out);
            test_failures++;
        }
    }
}

static void test_ctrl_sets_pin(void) {
    setup();
    CHECK_CONTAINS(run("ctrl strobe 0"), "ok\n");
    CHECK(fake_cable_level(1) == 0);
    CHECK_CONTAINS(run("ctrl SELIN 0"), "ok\n");
    CHECK(fake_cable_level(17) == 0);
    CHECK_CONTAINS(run("info"), "/STROBE=0 /AUTOFD=1 /INIT=1 /SELIN=0");
    run("ctrl strobe 1");
    CHECK(fake_cable_level(1) == 1);
}

static void test_dir_out_needs_oe(void) {
    setup();
    const char *out = run("dir out");
    CHECK_CONTAINS(out, "error: oe data is off");
    CHECK(fake_log_len == 0);
    CHECK(!fake_data_driven);
}

static void test_data_write_needs_dir_out(void) {
    setup();
    run("oe data on");
    fake_log_clear();
    CHECK_CONTAINS(run("data 5a"), "error: direction is in");
    CHECK(fake_log_len == 0);
}

static void test_data_path(void) {
    setup();
    CHECK_CONTAINS(run("oe data on"), "ok\n");
    CHECK_CONTAINS(run("dir out"), "ok\n");
    CHECK(data_cable() == 0x00); /* preload: last data, 0 after power-up */
    CHECK_CONTAINS(run("data 0x5A"), "ok\n");
    CHECK(data_cable() == 0x5A);
    CHECK_CONTAINS(run("data"), "data: 0x5A\n");
    run("dir in");
    run("dir out");
    CHECK(data_cable() == 0x5A); /* dir out preloads the last value */
    run("oe data off");
    CHECK(!bus_data_is_out());
    run("oe data on");
    CHECK(data_cable() == -1); /* not driving after re-enable */
    CHECK(fake_fights == 0);
}

static void test_data_read_in(void) {
    setup();
    run("oe data on");
    for (unsigned k = 0; k < DATA_COUNT; k++) fake_stuck(DATA_DB25[k], (0xC3 >> k) & 1);
    CHECK_CONTAINS(run("data"), "data: 0xC3\n");
}

static void test_oe_ctrl(void) {
    setup();
    run("oe ctrl off");
    CHECK(!fake_u23_enabled());
    CHECK_CONTAINS(run("info"), "oe data=off ctrl=off, dir=in");
    run("oe ctrl on");
    CHECK(fake_u23_enabled());
}

static void test_info(void) {
    setup();
    const char *out = run("info");
    CHECK_CONTAINS(out, "zipbridge bring-up " FW_VERSION);
    CHECK_CONTAINS(out, "fake RP2350");
    CHECK_CONTAINS(out, "oe data=off ctrl=on, dir=in");
    CHECK_CONTAINS(out, "/STROBE=1 /AUTOFD=1 /INIT=1 /SELIN=1");
    CHECK_CONTAINS(out, "data: 0x");
}

static void test_leds_command(void) {
    setup();
    const char *out = run("leds");
    CHECK_CONTAINS(out, "READY\nREAD\nWRITE\n");
    CHECK_CONTAINS(out, "ok\n");
    CHECK(((fake_out_value() >> PIN_LED_WRITE) & 1u) == 0); /* override cleared */
}

static void test_backspace(void) {
    setup();
    fake_io_clear_output();
    feed("statx\b");
    feed("us\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
    feed("x\x7f" "status\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
}

static void test_crlf_is_one_enter(void) {
    setup();
    fake_io_clear_output();
    feed("status\r\n");
    CHECK(count_of(fake_io_output(), "> ") == 1);
    CHECK(count_of(fake_io_output(), "ok\n") == 1);
}

static void test_lf_alone_works(void) {
    setup();
    fake_io_clear_output();
    feed("status\n");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
}

static void test_line_too_long(void) {
    setup();
    char line[101];
    memset(line, 'a', 100);
    line[100] = 0;
    CHECK_CONTAINS(run(line), "error: line too long");
    CHECK(fake_log_len == 0);
}

static void test_fault_reported_and_sticky(void) {
    setup();
    fake_io_clear_output();
    bus_oe_data(true);
    bus_data_write(0x01); /* direction is in: internal fault */
    CHECK_CONTAINS(fake_io_output(), "FAULT:");
    CHECK(leds_ready_mode() == LEDS_READY_FAULT);
    fake_log_clear();
    CHECK_CONTAINS(run("oe data on"), "error: fault");
    CHECK_CONTAINS(run("ctrl strobe 0"), "error: fault");
    CHECK(fake_log_len == 0);
    CHECK_CONTAINS(run("status"), "ok\n"); /* reads still work */
    CHECK_CONTAINS(run("info"), "FAULT:");
}

int main(void) {
    RUN(test_help_lists_commands);
    RUN(test_status_case_insensitive);
    RUN(test_status_shows_levels);
    RUN(test_unknown_command);
    RUN(test_bad_arguments_change_nothing);
    RUN(test_ctrl_sets_pin);
    RUN(test_dir_out_needs_oe);
    RUN(test_data_write_needs_dir_out);
    RUN(test_data_path);
    RUN(test_data_read_in);
    RUN(test_oe_ctrl);
    RUN(test_info);
    RUN(test_leds_command);
    RUN(test_backspace);
    RUN(test_crlf_is_one_enter);
    RUN(test_lf_alone_works);
    RUN(test_line_too_long);
    RUN(test_fault_reported_and_sticky);
    return test_report();
}
