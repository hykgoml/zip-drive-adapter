#include "console.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "bus.h"
#include "exercise.h"
#include "gpio_hal.h"
#include "io.h"
#include "leds.h"
#include "scan.h"

#define CONSOLE_LINE_MAX 80
#define MAX_ARGS 4
#define PROMPT "> "

static char line[CONSOLE_LINE_MAX + 1];
static size_t len;
static bool overflow;
static int prev_char;
static int esc_state; /* 0 none, 1 after ESC, 2 inside [ or O sequence */
static int esc_len;   /* bytes swallowed in state 2 */
#define ESC_MAX 16
static uint8_t last_data; /* last `data <hex>` value, preloaded by `dir out` */
static char errbuf[96];

/* Output longer than 159 characters is cut off: send long fixed text with io_write. */
void out_printf(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof buf) strcpy(buf + sizeof buf - 5, "...\n");
    io_write(buf);
}

/* The bus calls this possibly with interrupts off (scan samples), so it only
 * records. console_service / console_exec print the report later. */
static bool fault_pending;

static void on_fault(const char *why) {
    (void)why; /* bus_fault_reason() keeps it */
    leds_set_ready(LEDS_READY_FAULT);
    fault_pending = true;
}

void console_service(void) {
    if (!fault_pending) return;
    fault_pending = false;
    out_printf("\nFAULT: %s. All buffers off; unplug to reset.\n", bus_fault_reason());
}

static const char *fault_error(void) {
    snprintf(errbuf, sizeof errbuf, "fault (%s); unplug to reset", bus_fault_reason());
    return errbuf;
}

static bool parse_hex_byte(const char *s, uint8_t *v) {
    if (s[0] == '0' && s[1] == 'x') s += 2;
    size_t n = strlen(s);
    if (n < 1 || n > 2) return false;
    unsigned val = 0;
    for (; *s; s++) {
        int d = isdigit((unsigned char)*s) ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : -1;
        if (d < 0) return false;
        val = val * 16u + (unsigned)d;
    }
    *v = (uint8_t)val;
    return true;
}

/* ---- commands: each returns NULL (ok) or an error message ---- */

typedef const char *(*cmd_fn)(int argc, char **argv);
typedef struct {
    const char *name;
    const char *usage;
    cmd_fn fn;
    bool changes_pins; /* refused while faulted */
} cmd_t;

static const char *cmd_help(int argc, char **argv);
static const char *cmd_info(int argc, char **argv);
static const char *cmd_leds(int argc, char **argv);
static const char *cmd_status(int argc, char **argv);
static const char *cmd_ctrl(int argc, char **argv);
static const char *cmd_oe(int argc, char **argv);
static const char *cmd_dir(int argc, char **argv);
static const char *cmd_data(int argc, char **argv);
static const char *cmd_walk(int argc, char **argv);
static const char *cmd_pattern(int argc, char **argv);
static const char *cmd_scan(int argc, char **argv);

static const cmd_t CMDS[] = {
    {"help", "help [scan]", cmd_help, false},
    {"info", "info", cmd_info, false},
    {"leds", "leds", cmd_leds, false},
    {"status", "status", cmd_status, false},
    {"ctrl", "ctrl <strobe|autofd|init|selin> <0|1>", cmd_ctrl, true},
    {"oe", "oe <data|ctrl> <on|off>", cmd_oe, true},
    {"dir", "dir <in|out>", cmd_dir, true},
    {"data", "data [<hex>]", cmd_data, false}, /* write form checks the fault itself */
    {"walk", "walk [ms]  (100-10000, default 1000; no jumpers)", cmd_walk, true},
    {"pattern", "pattern  (no jumpers)", cmd_pattern, true},
    {"scan", "scan  (loopback mapper; see 'help scan')", cmd_scan, true},
};
#define N_CMDS (sizeof CMDS / sizeof CMDS[0])

static const char *cmd_help(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "scan") == 0) {
        io_write(SCAN_RULES); /* longer than out_printf's buffer */
        return NULL;
    }
    if (argc != 1) return "usage is 'help' or 'help scan'";
    out_printf("commands (levels are pin levels, 0/1):\n");
    for (size_t i = 0; i < N_CMDS; i++) out_printf("  %s\n", CMDS[i].usage);
    return NULL;
}

static const char *cmd_info(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    uint8_t c = bus_ctrl_get();
    out_printf("zipbridge bring-up %s, built %s %s, chip %s\n", FW_VERSION, __DATE__, __TIME__,
               hal_chip_name());
    out_printf("oe data=%s ctrl=%s, dir=%s\n", bus_oe_data_on() ? "on" : "off",
               bus_oe_ctrl_on() ? "on" : "off", bus_data_is_out() ? "out" : "in");
    out_printf("ctrl:");
    for (unsigned k = 0; k < CTRL_COUNT; k++) out_printf(" %s=%u", CTRL_NAME[k], (c >> k) & 1u);
    out_printf("\ndata: 0x%02X\n", bus_data_read());
    if (bus_faulted()) out_printf("FAULT: %s\n", bus_fault_reason());
    return NULL;
}

static const char *cmd_leds(int argc, char **argv) {
    static const int order[3] = {LED_READY, LED_READ, LED_WRITE};
    static const char *const name[3] = {"READY", "READ", "WRITE"};
    (void)argv;
    if (argc != 1) return "too many arguments";
    for (int i = 0; i < 3; i++) {
        out_printf("%s\n", name[i]);
        leds_override(order[i]);
        hal_delay_us(500000);
    }
    leds_override(LED_NONE);
    return NULL;
}

static const char *cmd_status(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    uint8_t s = bus_status_read();
    leds_flash_read();
    for (unsigned k = 0; k < STATUS_COUNT; k++)
        out_printf("%s%s=%u", k ? " " : "", STATUS_NAME[k], (s >> k) & 1u);
    out_printf("\n");
    return NULL;
}

static const char *cmd_ctrl(int argc, char **argv) {
    static const char *const names[CTRL_COUNT] = {"strobe", "autofd", "init", "selin"};
    if (argc != 3) return argc < 3 ? "missing arguments" : "too many arguments";
    int k = -1;
    for (int i = 0; i < (int)CTRL_COUNT; i++)
        if (strcmp(argv[1], names[i]) == 0) k = i;
    if (k < 0) return "unknown control line";
    if (strcmp(argv[2], "0") != 0 && strcmp(argv[2], "1") != 0) return "level must be 0 or 1";
    uint8_t c = bus_ctrl_get();
    c = argv[2][0] == '1' ? (uint8_t)(c | (1u << k)) : (uint8_t)(c & ~(1u << k));
    bus_ctrl_write(c);
    leds_flash_write();
    return NULL;
}

static const char *cmd_oe(int argc, char **argv) {
    if (argc != 3) return argc < 3 ? "missing arguments" : "too many arguments";
    bool on;
    if (strcmp(argv[2], "on") == 0) on = true;
    else if (strcmp(argv[2], "off") == 0) on = false;
    else return "state must be on or off";
    if (strcmp(argv[1], "data") == 0) bus_oe_data(on);
    else if (strcmp(argv[1], "ctrl") == 0) bus_oe_ctrl(on);
    else return "buffer must be data or ctrl";
    return NULL;
}

static const char *cmd_dir(int argc, char **argv) {
    if (argc != 2) return argc < 2 ? "missing argument" : "too many arguments";
    if (strcmp(argv[1], "in") == 0) {
        if (bus_data_is_out()) bus_data_dir_in();
        return NULL;
    }
    if (strcmp(argv[1], "out") != 0) return "direction must be in or out";
    if (bus_data_is_out()) return NULL; /* already out */
    if (!bus_oe_data_on()) return "oe data is off; run 'oe data on' first";
    /* The check briefly samples the control lines (they end at their old values);
     * nothing else changes if it refuses. */
    if (scan_ctrl_to_data_present()) return bus_faulted() ? fault_error() : SCAN_CTRL_DATA_ERROR;
    bus_data_dir_out(last_data);
    return NULL;
}

static const char *cmd_data(int argc, char **argv) {
    if (argc == 1) {
        uint8_t v = bus_data_read();
        leds_flash_read();
        out_printf("data: 0x%02X\n", v);
        return NULL;
    }
    if (argc > 2) return "too many arguments";
    uint8_t v;
    if (!parse_hex_byte(argv[1], &v)) return "not a hex byte (00-ff)";
    if (bus_faulted()) return fault_error();
    if (!bus_data_is_out()) return "direction is in; run 'dir out' first";
    bus_data_write(v);
    last_data = v;
    leds_flash_write();
    return NULL;
}

static bool parse_uint(const char *s, uint32_t *v) {
    if (!*s) return false;
    uint32_t x = 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s) || x > 100000u) return false;
        x = x * 10u + (uint32_t)(*s - '0');
    }
    *v = x;
    return true;
}

static const char *cmd_walk(int argc, char **argv) {
    uint32_t ms = 1000;
    if (argc > 2) return "too many arguments";
    if (argc == 2 && (!parse_uint(argv[1], &ms) || ms < 100 || ms > 10000)) return "ms must be 100-10000";
    return exercise_walk(ms);
}

static const char *cmd_pattern(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    return exercise_pattern();
}

static const char *cmd_scan(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return "too many arguments";
    return scan_run();
}

/* ---- line handling ---- */

void console_init(void) {
    len = 0;
    overflow = false;
    prev_char = 0;
    esc_state = 0;
    last_data = 0;
    fault_pending = false;
    bus_set_fault_handler(on_fault);
}

void console_banner(void) {
    len = 0; /* drop a half-typed line from before a disconnect */
    overflow = false;
    prev_char = 0;
    esc_state = 0;
    out_printf("\nzipbridge bring-up %s. Type 'help'.\n", FW_VERSION);
    /* A fault report printed while no terminal was open is lost; repeat it. */
    if (bus_faulted())
        out_printf("FAULT: %s. All buffers off; unplug to reset.\n", bus_fault_reason());
    out_printf("%s", PROMPT);
}

void console_exec(char *s) {
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    char *argv[MAX_ARGS];
    /* argc may exceed MAX_ARGS: only argv[0..MAX_ARGS-1] are valid and argv is not
     * NULL-terminated, so handlers must check argc before indexing argv. */
    int argc = 0; /* counts every token; only the first MAX_ARGS are stored */
    char *save;
    for (char *t = strtok_r(s, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
        if (argc < MAX_ARGS) argv[argc] = t;
        argc++;
    }
    if (argc == 0) return;
    const cmd_t *c = NULL;
    for (size_t i = 0; i < N_CMDS; i++)
        if (strcmp(argv[0], CMDS[i].name) == 0) c = &CMDS[i];
    if (!c) {
        out_printf("error: unknown command '%s'\nusage: help\n", argv[0]);
        return;
    }
    bool was_faulted = bus_faulted();
    const char *err = (c->changes_pins && was_faulted) ? fault_error() : c->fn(argc, argv);
    if (!err && !was_faulted && bus_faulted()) err = fault_error(); /* faulted mid-command */
    console_service(); /* the FAULT report comes before the error line */
    if (err) out_printf("error: %s\nusage: %s\n", err, c->usage);
    else out_printf("ok\n");
}

void console_feed(int c) {
    bool lf_after_cr = (c == '\n' && prev_char == '\r');
    prev_char = c;
    if (lf_after_cr) return;
    if (esc_state == 2) {
        if (c >= 0x20) {
            if ((c >= 0x40 && c <= 0x7E) || ++esc_len >= ESC_MAX) esc_state = 0;
            return;
        }
        esc_state = 0; /* a control byte ends the sequence and is handled below */
    }
    if (esc_state == 1) {
        esc_state = 0;
        if (c == '[' || c == 'O') {
            esc_state = 2;
            esc_len = 0;
            return;
        }
    }
    if (c == 0x1B) {
        esc_state = 1;
        return;
    }
    if (c == 0x15 || c == 0x03) {
        len = 0;
        overflow = false;
        io_write(c == 0x03 ? "^C\n" : "\n");
        io_write(PROMPT);
        return;
    }
    if (c == '\r' || c == '\n') {
        io_write("\n");
        if (overflow) {
            out_printf("error: line too long (max %d characters)\n", CONSOLE_LINE_MAX);
        } else {
            line[len] = 0;
            console_exec(line);
        }
        len = 0;
        overflow = false;
        io_write(PROMPT);
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (len > 0) {
            len--;
            io_write("\b \b");
        }
        return;
    }
    if (c < 0x20 || c > 0x7E) return;
    if (len < CONSOLE_LINE_MAX) {
        char echo[2] = {(char)c, 0};
        line[len++] = (char)c;
        io_write(echo);
    } else {
        overflow = true;
        io_write("\a");
    }
}

int console_poll_key(void) {
    for (;;) {
        int c = io_getc();
        if (c < 0) return -1;
        bool lf_after_cr = (c == '\n' && prev_char == '\r');
        prev_char = c;
        if (!lf_after_cr) {
            if (c == 0x1B) esc_state = 1; /* console_feed swallows a following [A */
            return c;
        }
    }
}

bool console_wait_enter(void) {
    for (;;) {
        int c = console_poll_key();
        if (c == '\r' || c == '\n') return true;
        if (c >= 0) return false;
        if (!io_connected()) return false; /* terminal gone: Enter would come from a later session */
        leds_poll();
        hal_delay_us(1000);
    }
}
