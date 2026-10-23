/*
 * serial.c - POSIX termios serial port handling (see serial.h).
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "control.h"
#include "log.h"
#include "serial.h"

/* ---------------------------------------------------------------------------
 * Baud rate table
 * ------------------------------------------------------------------------- */
static const struct { int baud; speed_t flag; } BAUDS[] = {
    {110, B110}, {300, B300}, {600, B600}, {1200, B1200}, {2400, B2400},
    {4800, B4800}, {9600, B9600}, {19200, B19200}, {38400, B38400},
    {57600, B57600}, {115200, B115200}, {230400, B230400},
};

static speed_t baud_flag(int baud)
{
    for (size_t i = 0; i < sizeof BAUDS / sizeof BAUDS[0]; i++)
        if (BAUDS[i].baud == baud) return BAUDS[i].flag;
    return 0;
}

int serial_baud_supported(int baud) { return baud_flag(baud) != 0; }

static void sleep_us(long us)
{
    if (us <= 0) return;
    struct timespec ts = { us / 1000000L, (us % 1000000L) * 1000L };
    nanosleep(&ts, NULL);
}

/* ---------------------------------------------------------------------------
 * Open / close
 * ------------------------------------------------------------------------- */
int serial_open(const char *device, const profile_t *p)
{
    speed_t speed = baud_flag(p->baud);
    if (!speed) { log_error("unsupported baud rate %d", p->baud); return -1; }

    /* O_NOCTTY: we never want a serial line to become our controlling tty.
     * O_NONBLOCK: don't hang on open waiting for carrier detect.          */
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { log_error("cannot open %s: %s", device, strerror(errno)); return -1; }

    /* Exclusive access: other processes get EBUSY. Best effort. */
    if (ioctl(fd, TIOCEXCL) < 0)
        log_debug("TIOCEXCL failed (%s); continuing", strerror(errno));

    struct termios t;
    if (tcgetattr(fd, &t) < 0) {
        log_error("%s is not a serial device (tcgetattr: %s)", device, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&t);                       /* no echo, no line editing, 8 bit clean */
    cfsetispeed(&t, speed);
    cfsetospeed(&t, speed);

    /* Character framing */
    t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    t.c_cflag |= CLOCAL | CREAD;         /* ignore modem-control lines, enable RX */
    switch (p->databits) {
    case 5:  t.c_cflag |= CS5; break;
    case 6:  t.c_cflag |= CS6; break;
    case 7:  t.c_cflag |= CS7; break;
    default: t.c_cflag |= CS8; break;
    }
    if (p->parity == 'E') t.c_cflag |= PARENB;
    if (p->parity == 'O') t.c_cflag |= PARENB | PARODD;
    if (p->stopbits == 2) t.c_cflag |= CSTOPB;

    /* Flow control.
     * IXON  = we obey XOFF/XON *from* the machine (what a drip feeder needs).
     * IXOFF (we send XOFF to throttle the machine) is deliberately left off:
     * we read only tiny replies and never need to throttle the control.     */
    t.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (p->flow == FLOW_XONXOFF || p->flow == FLOW_BOTH) t.c_iflag |= IXON;
    if (p->flow == FLOW_RTSCTS  || p->flow == FLOW_BOTH) t.c_cflag |= CRTSCTS;

    t.c_cc[VMIN]  = 0;                   /* reads return immediately ...    */
    t.c_cc[VTIME] = 0;                   /* ... with whatever is available. */

    if (tcsetattr(fd, TCSANOW, &t) < 0) {
        log_error("cannot configure %s: %s", device, strerror(errno));
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);              /* discard stale bytes from before we opened */
    return fd;
}

void serial_close(int fd)
{
    if (fd >= 0) close(fd);
}

/* ---------------------------------------------------------------------------
 * Writing
 * ------------------------------------------------------------------------- */
int serial_write(int fd, const char *buf, size_t n, long char_delay_us)
{
    size_t off = 0;
    while (off < n) {
        if (control_abort_requested()) return -1;

        /* With a per-character delay we must go one byte at a time. */
        size_t chunk = char_delay_us > 0 ? 1 : n - off;
        ssize_t w = write(fd, buf + off, chunk);
        if (w > 0) {
            off += (size_t)w;
            if (char_delay_us > 0) sleep_us(char_delay_us);
            continue;
        }
        if (w < 0 && errno != EAGAIN && errno != EINTR) {
            log_error("serial write failed: %s", strerror(errno));
            return -1;
        }
        /* Held off by the machine: wait for room, but wake often so that
         * abort and pause keys stay responsive. */
        struct pollfd pfd = { fd, POLLOUT, 0 };
        poll(&pfd, 1, 100);
    }
    return 0;
}

int serial_wait_sent(int fd)
{
    for (;;) {
        int pending = 0;
        if (ioctl(fd, TIOCOUTQ, &pending) < 0) {
            /* Not supported (some pseudo-terminals): fall back to the
             * blocking call, accepting that abort is slower to notice. */
            return tcdrain(fd);
        }
        if (pending == 0) {
            /* Queue empty; tcdrain waits for the final byte to leave the
             * shift register. Quick when nothing is held off. */
            return tcdrain(fd);
        }
        if (control_abort_requested()) return -1;
        sleep_us(5000);
    }
}

void serial_flush_output(int fd)
{
    tcflush(fd, TCOFLUSH);
}

/* ---------------------------------------------------------------------------
 * Reading
 * ------------------------------------------------------------------------- */
ssize_t serial_read(int fd, unsigned char *buf, size_t n)
{
    ssize_t r = read(fd, buf, n);
    if (r < 0) return 0;                 /* EAGAIN / EINTR: nothing available */
    return r;
}

int serial_wait_readable(int fd, int timeout_ms)
{
    struct pollfd pfd = { fd, POLLIN, 0 };
    return poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}
