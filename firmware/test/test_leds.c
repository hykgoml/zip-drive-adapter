#include "board.h"
#include "fake_board.h"
#include "gpio_hal.h"
#include "leds.h"
#include "test.h"

int test_failures;

static bool led(unsigned pin) { return (fake_out_value() >> pin) & 1u; }

static void setup(void) {
    fake_reset(1);
    leds_init();
}

static void test_init_outputs(void) {
    setup();
    CHECK((fake_out_enable() & LED_MASK) == LED_MASK);
    CHECK(!led(PIN_LED_READ) && !led(PIN_LED_WRITE));
}

static void test_ready_blinks_1hz_while_waiting(void) {
    setup();
    CHECK(led(PIN_LED_READY));
    hal_delay_us(600000);
    leds_poll();
    CHECK(!led(PIN_LED_READY));
    hal_delay_us(500000);
    leds_poll();
    CHECK(led(PIN_LED_READY));
}

static void test_ready_steady_when_connected(void) {
    setup();
    leds_set_ready(LEDS_READY_ON);
    for (int i = 0; i < 30; i++) {
        hal_delay_us(50000);
        leds_poll();
        CHECK(led(PIN_LED_READY));
    }
}

static void test_fault_blinks_5hz_and_sticks(void) {
    setup();
    leds_set_ready(LEDS_READY_FAULT);
    int changes = 0;
    bool last = led(PIN_LED_READY);
    for (int i = 0; i < 100; i++) { /* 1 s in 10 ms steps */
        hal_delay_us(10000);
        leds_poll();
        if (led(PIN_LED_READY) != last) { changes++; last = !last; }
    }
    CHECK(changes >= 9 && changes <= 11);
    leds_set_ready(LEDS_READY_ON);
    CHECK(leds_ready_mode() == LEDS_READY_FAULT);
}

static void test_flash_min_30ms(void) {
    setup();
    leds_flash_read();
    CHECK(led(PIN_LED_READ));
    hal_delay_us(29000);
    leds_poll();
    CHECK(led(PIN_LED_READ));
    hal_delay_us(2000);
    leds_poll();
    CHECK(!led(PIN_LED_READ));

    leds_flash_write();
    CHECK(led(PIN_LED_WRITE));
    hal_delay_us(29000);
    leds_poll();
    CHECK(led(PIN_LED_WRITE));
    hal_delay_us(2000);
    leds_poll();
    CHECK(!led(PIN_LED_WRITE));
}

static void test_override(void) {
    setup();
    leds_set_ready(LEDS_READY_ON);
    leds_override(LED_WRITE);
    CHECK(led(PIN_LED_WRITE) && !led(PIN_LED_READY) && !led(PIN_LED_READ));
    leds_override(LED_READ);
    CHECK(led(PIN_LED_READ) && !led(PIN_LED_READY) && !led(PIN_LED_WRITE));
    leds_override(LED_NONE);
    CHECK(led(PIN_LED_READY) && !led(PIN_LED_READ));
}

int main(void) {
    RUN(test_init_outputs);
    RUN(test_ready_blinks_1hz_while_waiting);
    RUN(test_ready_steady_when_connected);
    RUN(test_fault_blinks_5hz_and_sticks);
    RUN(test_flash_min_30ms);
    RUN(test_override);
    return test_report();
}
