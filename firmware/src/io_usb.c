#include <stdio.h>

#include "io.h"
#include "pico/stdlib.h"

int io_getc(void) {
    int c = getchar_timeout_us(0); /* PICO_ERROR_TIMEOUT (-2) when nothing waits */
    return c < 0 ? -1 : c;
}

void io_write(const char *s) {
    fputs(s, stdout); /* stdio turns \n into \r\n */
    fflush(stdout);
}
