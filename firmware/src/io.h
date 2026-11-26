#ifndef IO_H
#define IO_H

int io_getc(void);            /* next received character, or -1 if none */
void io_write(const char *s); /* send a string */

#endif
