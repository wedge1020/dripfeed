/*
 * serial.h - thin POSIX termios layer.
 *
 * Opens a serial device from a profile, writes with flow-control awareness,
 * and offers non-blocking reads. Nothing here knows about G-code.
 */
#ifndef DRIPFEED_SERIAL_H
#define DRIPFEED_SERIAL_H

#include <stddef.h>
#include <sys/types.h>

#include "profile.h"

/* Is this baud rate one we can configure? */
int serial_baud_supported(int baud);

/*
 * Open and configure `device` (e.g. /dev/ttyUSB0, /dev/serial0) according to
 * the profile. The port is opened exclusively (TIOCEXCL) so another process
 * can't interleave bytes into the program. Returns an fd, or -1 (message
 * logged).
 */
int serial_open(const char *device, const profile_t *p);

void serial_close(int fd);

/*
 * Write n bytes. Blocks (politely: polls in 100 ms slices) while the machine
 * holds us off with XOFF / CTS. Sleeps char_delay_us after each byte if > 0.
 * Returns 0 when all bytes are queued, -1 on error or operator abort.
 */
int serial_write(int fd, const char *buf, size_t n, long char_delay_us);

/*
 * Wait until everything written so far has physically left the UART, i.e.
 * until the machine has "accepted" it. Responsive to abort. 0 or -1.
 */
int serial_wait_sent(int fd);

/* Throw away anything queued for transmission (used on abort, so a held-off
 * kernel buffer is not delivered later when the machine sends XON). */
void serial_flush_output(int fd);

/* Non-blocking read of whatever has arrived. Returns bytes read (0 = none). */
ssize_t serial_read(int fd, unsigned char *buf, size_t n);

/* Wait up to timeout_ms for incoming data. 1 = readable, 0 = timeout. */
int serial_wait_readable(int fd, int timeout_ms);

#endif /* DRIPFEED_SERIAL_H */
