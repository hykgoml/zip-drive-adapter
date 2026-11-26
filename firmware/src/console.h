#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

#define FW_VERSION "0.1"

void console_init(void);     /* also installs the bus fault handler */
void console_banner(void);   /* on terminal connect: banner and prompt */
void console_feed(int c);    /* one received character */
void console_exec(char *line); /* run one command line (modified in place) */
void console_service(void);  /* main loop, every iteration: prints a pending FAULT report once */

/* For long-running commands. A LF right after a CR (one Enter key on many
 * terminals) is skipped. */
int console_poll_key(void);    /* -1 if no key */
bool console_wait_enter(void); /* true = Enter, false = any other key */

void out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
