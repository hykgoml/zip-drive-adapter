#ifndef IO_H
#define IO_H

#include <stdbool.h>

int io_getc(void);            /* next received character, or -1 if none */
void io_write(const char *s); /* send a string */
bool io_connected(void);      /* a terminal is attached (USB serial open) */

#endif
