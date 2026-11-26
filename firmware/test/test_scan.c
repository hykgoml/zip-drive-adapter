#include "board.h"
#include "bus.h"
#include "console.h"
#include "fake_board.h"
#include "fake_io.h"
#include "leds.h"
#include "scan.h"
#include "test.h"

int test_failures;

/* Fresh board, apply wiring, run scan, return the output. */
static const char *scan_with(void (*wire)(void)) {
    fake_reset(1);
    fake_io_reset();
    leds_init();
    bus_init();
    console_init();
    if (wire) wire();
    fake_log_clear();
    fake_io_clear_output();
    fake_contention_reset();
    fake_data_driven = false;
    CHECK(scan_run() == NULL);
    return fake_io_output();
}

static void check_common(void) {
    CHECK(fake_max_contention_us() <= 20);
    CHECK(fake_fights == 0);
    CHECK(!fake_log_overflow);
    CHECK(!fake_u1_enabled() && fake_u23_enabled()); /* bus_safe() state */
    CHECK(!bus_data_is_out() && bus_ctrl_get() == 0x0F);
}

/* An unwired data input may show either "floating?" or "stuck". */
static void check_floating_or_stuck(const char *out, const char *who) {
    char a[48], b[48];
    snprintf(a, sizeof a, "%s floating?\n", who);
    snprintf(b, sizeof b, "%s stuck ", who);
    if (!strstr(out, a) && !strstr(out, b)) {
        fprintf(stderr, "    no floating/stuck line for %s:\n%s\n", who, out);
        test_failures++;
    }
}

static void no_wiring(void) {}
static void pass_a(void) { fake_jumper(2, 10); fake_jumper(3, 11); fake_jumper(4, 12); fake_jumper(5, 13); fake_jumper(6, 15); }
static void pass_b(void) { fake_jumper(7, 10); fake_jumper(8, 11); fake_jumper(9, 12); fake_jumper(1, 13); fake_jumper(14, 15); }
static void pass_c(void) {
    fake_jumper(16, 10); fake_jumper(17, 11);
    fake_jumper(1, 2); fake_jumper(14, 3); fake_jumper(16, 4); fake_jumper(17, 5);
}
static void pass_d(void) { fake_jumper(1, 6); fake_jumper(14, 7); fake_jumper(16, 8); fake_jumper(17, 9); }
static void short_d3_d4(void) { fake_jumper(5, 13); fake_jumper(6, 13); }
static void weak_d3(void) { fake_jumper(5, 13); fake_flaky(13); }
static void ack_stuck_low(void) { fake_stuck(10, 0); }

static void test_rules_printed(void) {
    const char *out = scan_with(no_wiring);
    CHECK_CONTAINS(out, "Jumper rules");
    CHECK_CONTAINS(out, "At most one jumper per input pin");
}

static void test_no_wiring(void) {
    const char *out = scan_with(no_wiring);
    check_common();
    CHECK(count_of(out, ") -> ") == 0);
    CHECK_CONTAINS(out, "/ACK (pin 10) stuck high\n");
    CHECK_CONTAINS(out, "BUSY (pin 11) stuck high\n");
    CHECK_CONTAINS(out, "PE (pin 12) stuck high\n");
    CHECK_CONTAINS(out, "SELECT (pin 13) stuck high\n");
    CHECK_CONTAINS(out, "/ERROR (pin 15) stuck high\n");
    check_floating_or_stuck(out, "D0 (pin 2)");
    check_floating_or_stuck(out, "D7 (pin 9)");
    CHECK_LACKS(out, "possible short:");
    CHECK_LACKS(out, "weak");
    CHECK_LACKS(out, "phase 2 skipped");
    CHECK_CONTAINS(out, "scan: 0 links, 0 possible shorts, ");
}

static void test_pass_a(void) {
    const char *out = scan_with(pass_a);
    check_common();
    CHECK(count_of(out, ") -> ") == 5);
    CHECK_CONTAINS(out, "D0 (pin 2) -> /ACK (pin 10)\n");
    CHECK_CONTAINS(out, "D1 (pin 3) -> BUSY (pin 11)\n");
    CHECK_CONTAINS(out, "D2 (pin 4) -> PE (pin 12)\n");
    CHECK_CONTAINS(out, "D3 (pin 5) -> SELECT (pin 13)\n");
    CHECK_CONTAINS(out, "D4 (pin 6) -> /ERROR (pin 15)\n");
    CHECK_CONTAINS(out, "D0 (pin 2) stuck high\n"); /* jumpered to a pulled-up status pin */
    CHECK_CONTAINS(out, "D4 (pin 6) stuck high\n");
    check_floating_or_stuck(out, "D5 (pin 7)");
    check_floating_or_stuck(out, "D6 (pin 8)");
    check_floating_or_stuck(out, "D7 (pin 9)");
    CHECK_LACKS(out, "possible short:");
    CHECK_LACKS(out, "weak");
    CHECK_LACKS(out, "(pin 10) stuck");
    CHECK_CONTAINS(out, "scan: 5 links, 0 possible shorts, ");
}

static void test_pass_b(void) {
    const char *out = scan_with(pass_b);
    check_common();
    CHECK(count_of(out, ") -> ") == 5); /* control -> status found in both phases, shown once */
    CHECK_CONTAINS(out, "D5 (pin 7) -> /ACK (pin 10)\n");
    CHECK_CONTAINS(out, "D6 (pin 8) -> BUSY (pin 11)\n");
    CHECK_CONTAINS(out, "D7 (pin 9) -> PE (pin 12)\n");
    CHECK_CONTAINS(out, "/STROBE (pin 1) -> SELECT (pin 13)\n");
    CHECK_CONTAINS(out, "/AUTOFD (pin 14) -> /ERROR (pin 15)\n");
    CHECK_CONTAINS(out, "D5 (pin 7) stuck high\n");
    check_floating_or_stuck(out, "D0 (pin 2)");
    CHECK_LACKS(out, "possible short:");
    CHECK_CONTAINS(out, "scan: 5 links, 0 possible shorts, ");
}

static void test_pass_c(void) {
    const char *out = scan_with(pass_c);
    check_common();
    CHECK(!fake_data_driven); /* phase 2 skipped: data never driven */
    CHECK(count_of(out, ") -> ") == 6);
    CHECK_CONTAINS(out, "/STROBE (pin 1) -> D0 (pin 2)\n");
    CHECK_CONTAINS(out, "/AUTOFD (pin 14) -> D1 (pin 3)\n");
    CHECK_CONTAINS(out, "/INIT (pin 16) -> D2 (pin 4)\n");
    CHECK_CONTAINS(out, "/INIT (pin 16) -> /ACK (pin 10)\n");
    CHECK_CONTAINS(out, "/SELIN (pin 17) -> D3 (pin 5)\n");
    CHECK_CONTAINS(out, "/SELIN (pin 17) -> BUSY (pin 11)\n");
    CHECK_CONTAINS(out, "PE (pin 12) stuck high\n");
    CHECK_CONTAINS(out, "SELECT (pin 13) stuck high\n");
    CHECK_CONTAINS(out, "/ERROR (pin 15) stuck high\n");
    check_floating_or_stuck(out, "D4 (pin 6)");
    check_floating_or_stuck(out, "D7 (pin 9)");
    CHECK_CONTAINS(out, "phase 2 skipped: control -> data jumpers present\n");
    CHECK_CONTAINS(out, "scan: 6 links, 0 possible shorts, ");
}

static void test_pass_d(void) {
    const char *out = scan_with(pass_d);
    check_common();
    CHECK(!fake_data_driven);
    CHECK(count_of(out, ") -> ") == 4);
    CHECK_CONTAINS(out, "/STROBE (pin 1) -> D4 (pin 6)\n");
    CHECK_CONTAINS(out, "/AUTOFD (pin 14) -> D5 (pin 7)\n");
    CHECK_CONTAINS(out, "/INIT (pin 16) -> D6 (pin 8)\n");
    CHECK_CONTAINS(out, "/SELIN (pin 17) -> D7 (pin 9)\n");
    CHECK_CONTAINS(out, "/ACK (pin 10) stuck high\n");
    CHECK_CONTAINS(out, "/ERROR (pin 15) stuck high\n");
    check_floating_or_stuck(out, "D0 (pin 2)");
    check_floating_or_stuck(out, "D3 (pin 5)");
    CHECK_CONTAINS(out, "phase 2 skipped: control -> data jumpers present\n");
    CHECK_CONTAINS(out, "scan: 4 links, 0 possible shorts, ");
}

static void test_possible_short(void) {
    const char *out = scan_with(short_d3_d4);
    check_common();
    CHECK_CONTAINS(out, "possible short: D3 / D4 -> SELECT (pin 13)\n");
    CHECK_LACKS(out, "D3 (pin 5) -> SELECT");
    CHECK_CONTAINS(out, "scan: 0 links, 1 possible shorts, ");
}

static void test_weak_link(void) {
    const char *out = scan_with(weak_d3);
    check_common();
    CHECK_CONTAINS(out, "weak or intermittent: D3 -> SELECT?\n");
    CHECK_LACKS(out, "D3 (pin 5) -> SELECT");
    CHECK_LACKS(out, "possible short:");
}

static void test_stuck_low(void) {
    const char *out = scan_with(ack_stuck_low);
    check_common();
    CHECK_CONTAINS(out, "/ACK (pin 10) stuck low\n");
}

static void test_floating_never_short_or_weak(void) {
    for (uint32_t seed = 1; seed <= 20; seed++) {
        fake_reset(seed);
        fake_io_reset();
        leds_init();
        bus_init();
        console_init();
        pass_c(); /* four floating data lines */
        fake_io_clear_output();
        scan_run();
        const char *out = fake_io_output();
        if (strstr(out, "possible short:") || strstr(out, "weak") || count_of(out, ") -> ") != 6) {
            fprintf(stderr, "    seed %u:\n%s\n", (unsigned)seed, out);
            test_failures++;
        }
    }
}

int main(void) {
    RUN(test_rules_printed);
    RUN(test_no_wiring);
    RUN(test_pass_a);
    RUN(test_pass_b);
    RUN(test_pass_c);
    RUN(test_pass_d);
    RUN(test_possible_short);
    RUN(test_weak_link);
    RUN(test_stuck_low);
    RUN(test_floating_never_short_or_weak);
    return test_report();
}
