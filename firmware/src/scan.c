#include "scan.h"

#include <string.h>

#include "bus.h"
#include "console.h"
#include "io.h"

#define SAMPLES 8
#define DATA_IN_MASK   0x00FFu
#define STATUS_IN_MASK 0x1F00u

const char SCAN_RULES[] =
    "Jumper rules: wire an output to an input only (data to status, control to status,\n"
    "control to data). Never join two control pins or two data pins.\n"
    "At most one jumper per input pin. Remove all jumpers before walk and pattern.\n";

/* Irregular, so a floating input picking up crosstalk is unlikely to match. */
static const uint8_t SEQ[SAMPLES] = {0, 1, 1, 0, 1, 0, 0, 1};

static const char *out_name(unsigned o) { return o < DATA_COUNT ? DATA_NAME[o] : CTRL_NAME[o - DATA_COUNT]; }
static unsigned out_pin(unsigned o) { return o < DATA_COUNT ? DATA_DB25[o] : CTRL_DB25[o - DATA_COUNT]; }
static const char *in_name(unsigned i) { return i < DATA_COUNT ? DATA_NAME[i] : STATUS_NAME[i - DATA_COUNT]; }
static unsigned in_pin(unsigned i) { return i < DATA_COUNT ? DATA_DB25[i] : STATUS_DB25[i - DATA_COUNT]; }

/* Every output at rest (1) except output o at `level`. */
static void drive(bool phase2, unsigned o, unsigned level) {
    uint8_t d = 0xFF, c = 0x0F;
    if (!level) {
        if (o < DATA_COUNT) d = (uint8_t)(d & ~(1u << o));
        else c = (uint8_t)(c & ~(1u << (o - DATA_COUNT)));
    }
    if (phase2) bus_out_write(d, c);
    else bus_ctrl_write(c);
}

static void test_output(scan_result_t *r, bool phase2, unsigned o, uint16_t in_mask) {
    uint8_t match[SCAN_INPUTS] = {0};
    uint16_t first = 0, changed = 0;
    for (unsigned k = 0; k < SAMPLES; k++) {
        /* Interrupts off from the moment the output leaves rest until it is back,
         * so an interrupt can't stretch the time two outputs disagree. */
        uint32_t irq = bus_irq_save();
        drive(phase2, o, SEQ[k]);
        bus_wait_us(5);
        uint16_t in = bus_inputs_read();
        drive(phase2, o, 1); /* back to rest at once */
        bus_irq_restore(irq);
        if (bus_faulted()) { /* the sample is meaningless: drop it and the rest of this output */
            r->aborted = true;
            return;
        }
        in &= in_mask;
        if (k == 0) first = in;
        else changed |= (uint16_t)(in ^ first);
        r->seen1 |= in;
        r->seen0 |= (uint16_t)(~in & in_mask);
        for (unsigned i = 0; i < SCAN_INPUTS; i++)
            if (((in_mask >> i) & 1u) && ((in >> i) & 1u) == SEQ[k]) match[i]++;
    }
    r->sampled |= in_mask;
    for (unsigned i = 0; i < SCAN_INPUTS; i++) {
        if (!((in_mask >> i) & 1u)) continue;
        if (match[i] == SAMPLES) r->link[o][i] = true;
        else if ((changed >> i) & 1u) r->unclear[o][i] = true;
    }
}

void scan_measure(scan_result_t *r) {
    memset(r, 0, sizeof *r);
    bus_safe();

    /* Phase 1: control -> data and status. Data buffer on, direction in. */
    bus_oe_data(true);
    for (unsigned o = DATA_COUNT; o < SCAN_OUTPUTS && !r->aborted; o++) {
        test_output(r, false, o, DATA_IN_MASK | STATUS_IN_MASK);
    }

    bool ctrl_to_data = false;
    for (unsigned o = DATA_COUNT; o < SCAN_OUTPUTS; o++)
        for (unsigned i = 0; i < DATA_COUNT; i++)
            if (r->link[o][i]) ctrl_to_data = true;

    /* Phase 2: every output -> status. Data driven, all at rest (1). */
    if (ctrl_to_data) {
        r->phase2_skipped = true;
    } else if (!r->aborted) {
        bus_data_dir_out(0xFF);
        for (unsigned o = 0; o < SCAN_OUTPUTS && !r->aborted; o++) {
            test_output(r, true, o, STATUS_IN_MASK);
            if (bus_faulted()) r->aborted = true;
        }
    }
    bus_safe();
}

void scan_report(const scan_result_t *r) {
    unsigned links = 0, shorts = 0, stuck = 0;
    bool is_short[SCAN_INPUTS] = {false};

    /* An input with two or more links, or a link plus something unclear, has
     * several outputs on it: a short (a contended wire settles at a steady mid
     * level, so each output can look like a link). A status input unclear for
     * two or more outputs is also a short. A data input unclear for several
     * outputs with no link is just floating. */
    if (!r->aborted) {
        for (unsigned i = 0; i < SCAN_INPUTS; i++) {
            unsigned nl = 0, nu = 0;
            for (unsigned o = 0; o < SCAN_OUTPUTS; o++) {
                if (r->link[o][i]) nl++;
                else if (r->unclear[o][i]) nu++;
            }
            is_short[i] = nl >= 2 || (nl >= 1 && nu >= 1) || (i >= DATA_COUNT && nu >= 2);
        }
    }

    for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
        for (unsigned i = 0; i < SCAN_INPUTS; i++)
            if (r->link[o][i] && !is_short[i]) {
                out_printf("%s (pin %u) -> %s (pin %u)\n", out_name(o), out_pin(o), in_name(i), in_pin(i));
                links++;
            }

    if (r->aborted) { /* partial: only the links found so far */
        out_printf("scan aborted (partial)\n");
        return;
    }

    for (unsigned i = 0; i < SCAN_INPUTS; i++) {
        if (!is_short[i]) continue;
        out_printf("possible short: ");
        bool first = true;
        for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
            if (r->link[o][i] || r->unclear[o][i]) {
                out_printf("%s%s", first ? "" : " / ", out_name(o));
                first = false;
            }
        out_printf(" -> %s (pin %u)\n", in_name(i), in_pin(i));
        shorts++;
    }

    for (unsigned i = DATA_COUNT; i < SCAN_INPUTS; i++) { /* status inputs: one unclear output */
        if (is_short[i]) continue;
        unsigned n = 0, only = 0;
        for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
            if (r->unclear[o][i] && !r->link[o][i]) { n++; only = o; }
        if (n == 1) out_printf("weak or intermittent: %s -> %s?\n", out_name(only), in_name(i));
    }

    for (unsigned i = 0; i < SCAN_INPUTS; i++) {
        if (!((r->sampled >> i) & 1u) || is_short[i]) continue;
        bool changed = ((r->seen0 & r->seen1) >> i) & 1u;
        bool has_link = false;
        for (unsigned o = 0; o < SCAN_OUTPUTS; o++)
            if (r->link[o][i]) has_link = true;
        if (!changed) {
            out_printf("%s (pin %u) stuck %s\n", in_name(i), in_pin(i), ((r->seen1 >> i) & 1u) ? "high" : "low");
            stuck++;
        } else if (i < DATA_COUNT && !has_link) {
            out_printf("%s (pin %u) floating?\n", in_name(i), in_pin(i));
        }
    }

    if (r->phase2_skipped) out_printf("phase 2 skipped: control -> data jumpers present\n");
    out_printf("scan: %u links, %u possible shorts, %u stuck\n", links, shorts, stuck);
}

const char *scan_run(void) {
    scan_result_t r;
    io_write(SCAN_RULES); /* longer than out_printf's 160-character buffer */
    scan_measure(&r);
    scan_report(&r);
    return r.aborted ? "fault during scan; unplug to reset" : NULL;
}
