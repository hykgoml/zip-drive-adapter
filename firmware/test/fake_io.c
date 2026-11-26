#include "fake_io.h"

#include <string.h>

#include "io.h"

static char in_buf[4096];
static size_t in_head, in_len;
static char out_buf[65536];
static size_t out_len;
static int idle_limit, idle_count;

void fake_io_reset(void) {
    in_head = in_len = 0;
    out_len = 0;
    out_buf[0] = 0;
    idle_limit = 1000;
    idle_count = 0;
}

void fake_io_push(const char *s) {
    size_t n = strlen(s);
    if (n > sizeof in_buf - in_len) n = sizeof in_buf - in_len;
    memcpy(in_buf + in_len, s, n);
    in_len += n;
}

void fake_io_set_idle_limit(int limit) {
    idle_limit = limit;
    idle_count = 0;
}

int io_getc(void) {
    if (in_head < in_len) {
        idle_count = 0;
        return (unsigned char)in_buf[in_head++];
    }
    if (++idle_count > idle_limit) {
        idle_count = 0;
        return 0x1B;
    }
    return -1;
}

void io_write(const char *s) {
    size_t n = strlen(s);
    if (n > sizeof out_buf - 1 - out_len) n = sizeof out_buf - 1 - out_len;
    memcpy(out_buf + out_len, s, n);
    out_len += n;
    out_buf[out_len] = 0;
}

const char *fake_io_output(void) { return out_buf; }

void fake_io_clear_output(void) {
    out_len = 0;
    out_buf[0] = 0;
}
