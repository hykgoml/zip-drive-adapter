#include "bus.h"

#include "board.h"
#include "gpio_hal.h"

static bool oe_data, oe_ctrl, data_out, faulted;
static const char *fault_why;
static bus_fault_fn fault_fn;
static uint8_t ctrl_level;

static void fault(const char *why) {
    if (faulted) return;
    faulted = true;
    fault_why = why;
    hal_put_masked(U1_OE_MASK | U23_OE_MASK, U1_OE_MASK | U23_OE_MASK); /* all buffers off */
    hal_set_output(DATA_MASK, false);
    hal_put_masked(U1_DIR_MASK, 0);
    oe_data = oe_ctrl = data_out = false;
    if (fault_fn) fault_fn(why);
}

void bus_set_fault_handler(bus_fault_fn fn) { fault_fn = fn; }
bool bus_faulted(void) { return faulted; }
const char *bus_fault_reason(void) { return faulted ? (fault_why ? fault_why : "?") : ""; }

void bus_init(void) {
    oe_data = oe_ctrl = data_out = faulted = false;
    fault_why = 0;
    hal_init(BUS_MASK); /* all inputs: R14/R15 keep the buffers off */

    /* 1. Buffer enables become outputs, driven high: still off. */
    hal_put_masked(U1_OE_MASK | U23_OE_MASK, U1_OE_MASK | U23_OE_MASK);
    hal_set_output(U1_OE_MASK | U23_OE_MASK, true);

    /* 2. Data direction in; data pins Pico inputs with pull-downs. */
    hal_put_masked(U1_DIR_MASK, 0);
    hal_set_output(U1_DIR_MASK, true);
    hal_set_output(DATA_MASK, false);
    /* Pull-ups, not pull-downs: RP2350 erratum E9 can latch a pull-down input
     * near 2V after an external driver releases it high. */
    hal_set_pulls(DATA_MASK, true, false);
    hal_set_pulls(STATUS_MASK, true, false);

    /* 3. Control lines at idle (all 1). */
    ctrl_level = 0x0F;
    hal_put_masked(CTRL_MASK, CTRL_MASK);
    hal_set_output(CTRL_MASK, true);

    /* 4. Enable U2/U3. U1 stays off. */
    hal_put_masked(U23_OE_MASK, 0);
    oe_ctrl = true;
}

void bus_safe(void) {
    if (faulted) return;
    bus_oe_data(false);
    bus_ctrl_write(0x0F);
    bus_oe_ctrl(true);
}

uint8_t bus_status_read(void) {
    return (uint8_t)((hal_get_all() >> PIN_STATUS_BASE) & 0x1Fu);
}

void bus_ctrl_write(uint8_t levels) {
    if (faulted) return;
    ctrl_level = levels & 0x0Fu;
    hal_put_masked(CTRL_MASK, (uint32_t)ctrl_level << PIN_CTRL_BASE);
}

uint8_t bus_ctrl_get(void) { return ctrl_level; }

void bus_data_dir_out(uint8_t preload) {
    if (faulted) return;
    bool gated = oe_data;
    if (gated) hal_put_masked(U1_OE_MASK, U1_OE_MASK); /* cable sees nothing while switching */
    hal_put_masked(DATA_MASK, (uint32_t)preload << PIN_DATA_BASE);
    hal_put_masked(U1_DIR_MASK, U1_DIR_MASK); /* U1 stops driving the Pico side */
    hal_set_output(DATA_MASK, true);          /* only now does the Pico drive */
    if (gated) hal_put_masked(U1_OE_MASK, 0);
    data_out = true;
}

void bus_data_dir_in(void) {
    if (faulted) return;
    bool gated = oe_data;
    if (gated) hal_put_masked(U1_OE_MASK, U1_OE_MASK);
    hal_set_output(DATA_MASK, false); /* Pico lets go first */
    hal_put_masked(U1_DIR_MASK, 0);   /* then U1 drives the Pico side */
    if (gated) hal_put_masked(U1_OE_MASK, 0);
    data_out = false;
}

void bus_data_write(uint8_t v) {
    if (faulted) return;
    if (!data_out) {
        fault("data write while the data direction is in");
        return;
    }
    hal_put_masked(DATA_MASK, (uint32_t)v << PIN_DATA_BASE);
}

uint8_t bus_data_read(void) { return (uint8_t)(hal_get_all() >> PIN_DATA_BASE); }

uint16_t bus_inputs_read(void) {
    uint32_t v = hal_get_all();
    return (uint16_t)(((v >> PIN_DATA_BASE) & 0xFFu) | (((v >> PIN_STATUS_BASE) & 0x1Fu) << DATA_COUNT));
}

void bus_out_write(uint8_t data, uint8_t ctrl) {
    if (faulted) return;
    if (!data_out) {
        fault("data write while the data direction is in");
        return;
    }
    ctrl_level = ctrl & 0x0Fu;
    hal_put_masked(DATA_MASK | CTRL_MASK,
                   ((uint32_t)data << PIN_DATA_BASE) | ((uint32_t)ctrl_level << PIN_CTRL_BASE));
}

void bus_oe_data(bool on) {
    if (faulted) return;
    if (on) {
        hal_put_masked(U1_OE_MASK, 0);
        oe_data = true;
    } else {
        hal_put_masked(U1_OE_MASK, U1_OE_MASK);
        oe_data = false;
        bus_data_dir_in();
    }
}

void bus_oe_ctrl(bool on) {
    if (faulted) return;
    hal_put_masked(U23_OE_MASK, on ? 0 : U23_OE_MASK);
    oe_ctrl = on;
}

bool bus_oe_data_on(void) { return oe_data; }
bool bus_oe_ctrl_on(void) { return oe_ctrl; }
bool bus_data_is_out(void) { return data_out; }

void bus_wait_us(uint32_t us) { hal_delay_us(us); }

uint32_t bus_irq_save(void) { return hal_irq_save(); }
void bus_irq_restore(uint32_t state) { hal_irq_restore(state); }
