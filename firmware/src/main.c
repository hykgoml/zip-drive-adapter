#include <stdbool.h>

#include "bus.h"
#include "console.h"
#include "leds.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

int main(void) {
    bus_init(); /* first: buffers stay off, control lines idle, before anything slow */
    leds_init();
    stdio_init_all();
    console_init();

    bool connected = false;
    for (;;) {
        bool now_connected = stdio_usb_connected();
        if (now_connected != connected) {
            connected = now_connected;
            leds_set_ready(connected ? LEDS_READY_ON : LEDS_READY_WAITING);
            if (connected) console_banner();
        }
        int c = getchar_timeout_us(1000);
        if (c >= 0) console_feed(c);
        console_service();
        leds_poll();
    }
}
