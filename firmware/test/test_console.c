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
    CHECK_CONTAINS(out, "walk [ms]");
    CHECK_CONTAINS(out, "pattern");
    CHECK_CONTAINS(out, "scan");
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

static const char *const BAD[] = {
    "ctrl", "ctrl strobe", "ctrl strobe 2", "ctrl foo 1", "ctrl strobe 1 1", "ctrl strobe 1 1 1",
    "oe", "oe data", "oe data maybe", "oe disk on",
    "dir", "dir sideways", "dir in out",
    "data 1ff", "data zz", "data 0x", "data 12 34",
    "status x", "info x", "leds x", "help x y", "help x", "scan x",
};

static void check_bad_list(bool risky) {
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        setup();
        if (risky) {
            run("oe data on");
            run("dir out");
            fake_log_clear();
        }
        const char *out = run(BAD[i]);
        if (!strstr(out, "error: ") || !strstr(out, "usage: ") || fake_log_len != 0) {
            fprintf(stderr, "    '%s' was not rejected cleanly:\n%s\n", BAD[i], out);
            test_failures++;
        }
    }
}

static void test_bad_arguments_change_nothing(void) { check_bad_list(false); }
static void test_bad_arguments_from_risky_state(void) { check_bad_list(true); }

static void test_scan_command(void) {
    setup();
    CHECK_CONTAINS(run("scan"), "scan: 0 links");
    CHECK_CONTAINS(run("help scan"), "At most one jumper per input pin");
    CHECK_CONTAINS(run("scan now"), "error: too many arguments");
}

static void test_too_many_tokens_usage(void) {
    setup();
    const char *out = run("ctrl strobe 1 1 1");
    CHECK_CONTAINS(out, "error: too many arguments");
    CHECK_CONTAINS(out, "usage: ctrl <strobe");
    out = run("frob a b c d e");
    CHECK_CONTAINS(out, "unknown command 'frob'");
    CHECK_CONTAINS(out, "usage: help");
}

static void test_blank_lines(void) {
    setup();
    const char *out = run("");
    CHECK_LACKS(out, "ok");
    CHECK_LACKS(out, "error");
    CHECK_CONTAINS(out, "> ");
    out = run("   \t ");
    CHECK_LACKS(out, "ok");
    CHECK_LACKS(out, "error");
    CHECK(fake_log_len == 0);
}

static void test_surrounding_spaces(void) {
    setup();
    CHECK_CONTAINS(run("  status   "), "/ACK=1");
}

static void test_hex_prefix_uppercase(void) {
    setup();
    run("oe data on");
    run("dir out");
    CHECK_CONTAINS(run("data 0X5A"), "ok\n");
    CHECK(data_cable() == 0x5A);
}

static void test_dir_same_direction_is_noop(void) {
    setup();
    run("oe data on");
    fake_log_clear();
    CHECK_CONTAINS(run("dir in"), "ok\n");
    CHECK(fake_log_len == 0);
    run("dir out");
    fake_log_clear();
    CHECK_CONTAINS(run("dir out"), "ok\n");
    CHECK(fake_log_len == 0);
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
    CHECK(fake_io_output()[0] == 0); /* the handler only records */
    CHECK(leds_ready_mode() == LEDS_READY_FAULT);
    console_service();
    CHECK_CONTAINS(fake_io_output(), "FAULT:");
    fake_log_clear();
    CHECK_CONTAINS(run("oe data on"), "error: fault");
    CHECK_CONTAINS(run("ctrl strobe 0"), "error: fault");
    CHECK_CONTAINS(run("dir out"), "error: fault");
    CHECK_CONTAINS(run("dir in"), "error: fault");
    CHECK_CONTAINS(run("data 5a"), "error: fault");
    CHECK(fake_log_len == 0); /* before any read: reads flash LEDs */
    CHECK_CONTAINS(run("status"), "ok\n"); /* reads still work */
    CHECK_CONTAINS(run("info"), "FAULT:");
    CHECK_CONTAINS(run("data"), "data: 0x");
    CHECK_CONTAINS(run("data"), "ok\n");
}

static void test_fault_service_prints_once(void) {
    setup();
    fake_io_clear_output();
    bus_data_write(0x01); /* direction is in: fault outside any command */
    CHECK(fake_io_output()[0] == 0);
    console_service();
    const char *out = fake_io_output();
    CHECK(count_of(out, "FAULT:") == 1);
    CHECK_CONTAINS(out, "\nFAULT: ");
    CHECK_CONTAINS(out, "All buffers off; unplug to reset.\n");
    CHECK_CONTAINS(out, bus_fault_reason());
    fake_io_clear_output();
    console_service();
    CHECK(fake_io_output()[0] == 0);
    /* a later command does not repeat it either */
    CHECK_LACKS(run("ctrl strobe 0"), "FAULT:");
}

static void test_banner_shows_fault(void) {
    setup();
    bus_data_write(0x01); /* fault */
    console_service();
    fake_io_clear_output();
    console_banner();
    CHECK_CONTAINS(fake_io_output(), "FAULT:");
    CHECK_CONTAINS(fake_io_output(), bus_fault_reason());
}

/* The hook fires after the first PUT of the command and faults the bus by
 * writing data while the direction is in. It clears itself first, so the
 * fault path's own hal calls do not re-enter it. */
static void fault_hook(const fake_call_t *call) {
    if (call->op != FAKE_PUT) return;
    fake_on_op = 0;
    bus_data_write(1);
}

static void test_fault_during_command_is_not_ok(void) {
    setup();
    fake_io_clear_output();
    fake_on_op = fault_hook;
    const char *out = run("ctrl strobe 0");
    fake_on_op = 0;
    CHECK(count_of(out, "FAULT:") == 1);
    CHECK_CONTAINS(out, "error: fault (");
    CHECK_LACKS(out, "ok\n");
    CHECK(bus_faulted());
    const char *f = strstr(out, "FAULT:"), *e = strstr(out, "error: fault");
    CHECK(f && e && f < e); /* FAULT line first, then the error */
    fake_io_clear_output();
    console_service();
    CHECK(fake_io_output()[0] == 0);
}

static void test_banner_resets_line_editor(void) {
    setup();
    feed("sta");      /* half-typed line from before the disconnect */
    feed("\x1b[");    /* and a half-received escape sequence */
    console_banner();
    fake_io_clear_output();
    CHECK_CONTAINS(run("info"), "zipbridge bring-up"); /* not "stainfo" */
    setup();
    for (int i = 0; i < 100; i++) console_feed('x'); /* overflowed line */
    console_banner();
    CHECK_CONTAINS(run("info"), "ok\n");
}

static const char *const CTRL_DATA_ERR = "control -> data jumpers present";

static void test_dir_out_refused_with_ctrl_data_jumper(void) {
    setup();
    fake_jumper(1, 2); /* pass C-style: /STROBE -> D0 */
    fake_jumper(14, 3);
    run("oe data on");
    fake_contention_reset();
    CHECK_CONTAINS(run("dir out"), CTRL_DATA_ERR);
    CHECK(!fake_data_driven);
    CHECK(!bus_data_is_out());
    CHECK(bus_oe_data_on() && fake_u1_enabled());
    CHECK(bus_ctrl_get() == 0x0F);
    CHECK(fake_max_contention_us() <= 10 && fake_max_contention_irq_on_us() == 0);
    CHECK(fake_fights == 0);
}

static void test_dir_out_without_jumpers_works(void) {
    setup();
    run("oe data on");
    CHECK_CONTAINS(run("dir out"), "ok\n");
    CHECK(bus_data_is_out() && fake_data_driven);
    CHECK_CONTAINS(run("data a5"), "ok\n");
    CHECK(fake_fights == 0);
}

static void test_walk_pattern_without_jumpers_still_work(void) {
    setup();
    fake_io_push("\r");
    CHECK_CONTAINS(run("walk 100"), "ok\n");
    CHECK(fake_data_driven && fake_fights == 0);
}

/* Seeded random command stream, no jumpers: never two drivers on a wire, and
 * every scan ends in the bus_safe state. */
static void test_random_commands(void) {
    static const char *const CTRLS[] = {"strobe", "autofd", "init", "selin"};
    uint32_t rng = 12345;
    setup();
    char buf[48];
    for (int n = 0; n < 200; n++) {
        rng = rng * 1664525u + 1013904223u;
        unsigned pick = (rng >> 16) % 9, r2 = (rng >> 8) & 0xFF;
        bool is_scan = false;
        switch (pick) {
        case 0: snprintf(buf, sizeof buf, "oe data %s", r2 & 1 ? "on" : "off"); break;
        case 1: snprintf(buf, sizeof buf, "oe ctrl %s", r2 & 1 ? "on" : "off"); break;
        case 2: snprintf(buf, sizeof buf, "dir %s", r2 & 1 ? "in" : "out"); break;
        case 3: case 4: snprintf(buf, sizeof buf, "data %02x", r2); break;
        case 5: case 6: snprintf(buf, sizeof buf, "ctrl %s %u", CTRLS[r2 & 3], (r2 >> 2) & 1u); break;
        default: snprintf(buf, sizeof buf, "scan"); is_scan = true; break;
        }
        run(buf);
        if (fake_fights != 0 || bus_faulted()) {
            fprintf(stderr, "    after command %d '%s': fights=%d faulted=%d\n", n, buf, fake_fights, bus_faulted());
            test_failures++;
            return;
        }
        if (is_scan) {
            CHECK(!fake_u1_enabled() && fake_u23_enabled());
            CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
        }
    }
}

static void test_poll_key_and_wait_enter(void) {
    setup();
    fake_io_set_idle_limit(1000000);
    feed("x\r");
    fake_io_push("\n\r");
    CHECK(console_wait_enter()); /* LF skipped, CR is Enter */
    setup();
    fake_io_set_idle_limit(1000000);
    fake_io_push("\n");
    CHECK(console_wait_enter()); /* lone LF is Enter */
    setup();
    fake_io_set_idle_limit(1000000);
    feed("\r");
    fake_io_push("x");
    CHECK(console_poll_key() == 'x');
    setup();
    fake_io_set_idle_limit(1000000);
    fake_io_push("q");
    CHECK(!console_wait_enter());
    setup();
    fake_io_set_idle_limit(1000000);
    CHECK(console_poll_key() == -1);
    /* LF right after CR is skipped, so the next real key is read. */
    setup();
    fake_io_set_idle_limit(1000000);
    feed("x\r");
    fake_io_push("\nq");
    CHECK(!console_wait_enter()); /* q, not Enter */
    setup();
    fake_io_set_idle_limit(1000000);
    feed("x\r");
    fake_io_push("\nx");
    CHECK(console_poll_key() == 'x');
    /* reverse hand-off: poll_key took the CR, feed must skip the LF */
    setup();
    fake_io_set_idle_limit(1000000);
    fake_io_push("\r");
    CHECK(console_poll_key() == '\r');
    fake_io_clear_output();
    console_feed('\n');
    CHECK(count_of(fake_io_output(), "> ") == 0);
}

static void test_poll_key_esc_shares_state(void) {
    setup();
    fake_io_set_idle_limit(1000000);
    fake_io_push("\x1b[A");
    CHECK(console_poll_key() == 0x1B);
    fake_io_clear_output();
    console_feed('[');
    console_feed('A');
    feed("status\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=");
    CHECK_LACKS(fake_io_output(), "unknown");
}

static void test_control_char_ends_escape(void) {
    setup();
    fake_io_clear_output();
    feed("\x1b[\r");
    CHECK_LACKS(fake_io_output(), "ok");
    CHECK_LACKS(fake_io_output(), "error");
    CHECK_CONTAINS(fake_io_output(), "> ");
    CHECK(fake_log_len == 0);
    fake_io_clear_output();
    feed("\x1b[status\r"); /* s is the final byte and is swallowed */
    CHECK_CONTAINS(fake_io_output(), "error: unknown command 'tatus'");
    fake_io_clear_output();
    feed("\x1b[junk\x03status\r"); /* ^C ends the sequence and is handled */
    CHECK_CONTAINS(fake_io_output(), "^C\n> ");
    CHECK_CONTAINS(fake_io_output(), "/ACK=");
    /* a sequence swallows at most 16 bytes after the introducer */
    fake_io_clear_output();
    feed("\x1b[");
    for (int i = 0; i < 16; i++) feed("0");
    feed("status\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=");
    fake_io_clear_output();
    feed("\x1b[");
    for (int i = 0; i < 17; i++) feed("0");
    feed("status\r");
    CHECK_CONTAINS(fake_io_output(), "unknown command '0status'");
}

static void test_line_boundary(void) {
    setup();
    char line[128];
    memset(line, ' ', 80);
    memcpy(line, "status", 6);
    line[80] = 0;
    const char *out = run(line);
    CHECK_CONTAINS(out, "/ACK=1");
    CHECK_LACKS(out, "too long");
    line[80] = ' ';
    line[81] = 0;
    out = run(line);
    CHECK_CONTAINS(out, "error: line too long");
    CHECK_CONTAINS(out, "\a");
}

static void test_ctrl_u_and_ctrl_c(void) {
    setup();
    char line[101];
    memset(line, 'a', 100);
    line[100] = 0;
    fake_io_clear_output();
    feed(line);
    feed("\x15");
    CHECK_CONTAINS(fake_io_output(), "\n> ");
    fake_io_clear_output();
    feed("status\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
    CHECK_LACKS(fake_io_output(), "too long");
    fake_io_clear_output();
    feed("junk\x03");
    CHECK_CONTAINS(fake_io_output(), "^C\n> ");
    fake_io_clear_output();
    feed("status\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
    CHECK_LACKS(fake_io_output(), "unknown");
}

static void test_escape_sequences_swallowed(void) {
    setup();
    fake_io_clear_output();
    feed("\x1b[A\x1b[D\x1bOBstatus\r");
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
    CHECK_LACKS(fake_io_output(), "unknown");
    fake_io_clear_output();
    feed("\x1bstatus\r"); /* lone ESC dropped, rest handled */
    CHECK_CONTAINS(fake_io_output(), "/ACK=1");
}

static void test_out_printf_truncation(void) {
    setup();
    char big[300];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = 0;
    fake_io_clear_output();
    out_printf("%s", big);
    const char *o = fake_io_output();
    size_t n = strlen(o);
    CHECK(n < 160);
    CHECK(n >= 4 && strcmp(o + n - 4, "...\n") == 0);
}

static void test_walk_needs_enter(void) {
    setup();
    fake_io_push("x");
    CHECK_CONTAINS(run("walk 100"), "error: cancelled");
    CHECK(!fake_data_driven);
}

static void test_walk_crlf_then_enter(void) {
    setup();
    fake_io_push("\n\r\n"); /* LF of the command's CR LF skipped; Enter as CR LF, its LF skipped too */
    const char *out = run("walk 100");
    CHECK_CONTAINS(out, "/SELIN = pin 17 high\n");
    CHECK_CONTAINS(out, "ok\n");
    CHECK_LACKS(out, "stopped");
    CHECK(!fake_u1_enabled() && fake_u23_enabled());
}

static void test_walk_and_pattern_bad_args(void) {
    static const char *const bad[] = {"walk 50", "walk 20000", "walk abc", "walk 100 2", "pattern now"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        setup();
        const char *out = run(bad[i]);
        if (!strstr(out, "error: ") || !strstr(out, "usage: ") || fake_log_len != 0) {
            fprintf(stderr, "    '%s' was not rejected cleanly:\n%s\n", bad[i], out);
            test_failures++;
        }
    }
}

static void test_pattern_needs_enter(void) {
    setup();
    fake_io_push("q");
    CHECK_CONTAINS(run("pattern"), "error: cancelled");
    CHECK(!fake_data_driven);
}

static void fault_on_first_put(const fake_call_t *c) {
    if (c->op != FAKE_PUT || c->mask != (DATA_MASK | CTRL_MASK)) return;
    fake_on_op = 0;
    bus_data_dir_in();
    bus_data_write(1);
}

static void test_walk_fault_midrun(void) {
    setup();
    fake_io_push("\n\r");
    fake_on_op = fault_on_first_put;
    const char *out = run("walk 100");
    fake_on_op = 0;
    CHECK_CONTAINS(out, "error: fault");
    CHECK_LACKS(out, "ok\n");
    CHECK_LACKS(out, " high\n");
    CHECK(!fake_u1_enabled() && !fake_u23_enabled());
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
    RUN(test_fault_service_prints_once);
    RUN(test_banner_shows_fault);
    RUN(test_bad_arguments_from_risky_state);
    RUN(test_too_many_tokens_usage);
    RUN(test_blank_lines);
    RUN(test_surrounding_spaces);
    RUN(test_hex_prefix_uppercase);
    RUN(test_dir_same_direction_is_noop);
    RUN(test_fault_during_command_is_not_ok);
    RUN(test_banner_resets_line_editor);
    RUN(test_dir_out_refused_with_ctrl_data_jumper);
    RUN(test_dir_out_without_jumpers_works);
    RUN(test_walk_pattern_without_jumpers_still_work);
    RUN(test_random_commands);
    RUN(test_poll_key_and_wait_enter);
    RUN(test_poll_key_esc_shares_state);
    RUN(test_control_char_ends_escape);
    RUN(test_line_boundary);
    RUN(test_ctrl_u_and_ctrl_c);
    RUN(test_escape_sequences_swallowed);
    RUN(test_out_printf_truncation);
    RUN(test_walk_needs_enter);
    RUN(test_walk_crlf_then_enter);
    RUN(test_walk_and_pattern_bad_args);
    RUN(test_pattern_needs_enter);
    RUN(test_walk_fault_midrun);
    RUN(test_scan_command);
    return test_report();
}
