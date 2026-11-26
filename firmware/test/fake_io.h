#ifndef FAKE_IO_H
#define FAKE_IO_H

/* Simulated serial port behind io.h. Input is a queue the test fills.
 * When the queue is empty, io_getc returns -1, except that after `limit`
 * empty reads in a row it returns ESC (0x1B) once, so loops waiting for a
 * key always end. Output collects into one string. */
void fake_io_reset(void);              /* empty queue and output, limit 1000 */
void fake_io_push(const char *s);
void fake_io_set_idle_limit(int limit);
const char *fake_io_output(void);
void fake_io_clear_output(void);

#endif
