/*
 * feeder.c - the drip-feed engine (see feeder.h).
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "control.h"
#include "feeder.h"
#include "gcode.h"
#include "hw.h"
#include "log.h"
#include "macro.h"
#include "serial.h"

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(long ms)
{
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

double feeder_estimate_seconds(const profile_t *p, const feed_scan_t *s)
{
    double wire = (double)s->bytes_to_send * profile_bits_per_char(p) / p->baud;
    double pace = (double)s->lines_to_send * p->line_delay_ms / 1000.0
                + (double)s->bytes_to_send * p->char_delay_us / 1e6;
    return wire + pace;
}

static void format_duration(double sec, char *buf, size_t n)
{
    long s = (long)(sec + 0.5);
    if (s >= 3600) snprintf(buf, n, "%ldh %02ldm %02lds", s / 3600, (s % 3600) / 60, s % 60);
    else if (s >= 60) snprintf(buf, n, "%ldm %02lds", s / 60, s % 60);
    else snprintf(buf, n, "%lds", s);
}

/* ---------------------------------------------------------------------------
 * Phase 1: pre-flight scan
 * ------------------------------------------------------------------------- */
int feeder_scan(const feed_job_t *job, feed_scan_t *scan)
{
    const profile_t *p = job->profile;
    memset(scan, 0, sizeof *scan);

    FILE *fp = fopen(job->path, "r");
    if (!fp) { log_error("cannot open %s: %s", job->path, strerror(errno)); return DF_ERROR; }

    char   *buf = NULL, *raw = NULL;
    size_t  cap = 0;
    ssize_t got;
    long    lineno = 0;
    long    first_sent = 0, last_sent = 0;       /* first/last line sent  */
    int     first_is_pct = 0, last_is_pct = 0;
    macro_ctx_t macro;
    macro_init(&macro);

    while ((got = getline(&buf, &cap, fp)) != -1) {
        lineno++;
        /* Keep an untouched copy: gcode_prepare edits in place, and the macro
         * scan wants to see comment text that stripping removes. */
        if (p->macro_check) {
            char *r = realloc(raw, (size_t)got + 1);
            if (r) { raw = r; memcpy(raw, buf, (size_t)got + 1); }
        }
        gcode_line_t g;
        gcode_prepare(buf, p, &g);

        if (g.error) {
            log_error("%s:%ld: %s", job->path, lineno, g.error);
            scan->problems++;
            continue;
        }
        if (g.skip) { if (lineno >= job->start_line) scan->lines_skipped++; continue; }

        if (p->max_line_len && (int)g.len > p->max_line_len) {
            log_error("%s:%ld: block is %zu characters; this profile allows %d",
                      job->path, lineno, g.len, p->max_line_len);
            scan->problems++;
            continue;
        }

        if (p->macro_check) macro_scan_block(&macro, buf, raw, lineno, p);

        if (!first_sent) { first_sent = lineno; first_is_pct = gcode_is_percent(buf, g.len); }
        last_sent = lineno;
        last_is_pct = gcode_is_percent(buf, g.len);

        if (lineno >= job->start_line) {
            scan->lines_to_send++;
            scan->bytes_to_send += (long)(g.len + eol_len(p->eol));
            if ((int)g.len > scan->longest_line) {
                scan->longest_line = (int)g.len;
                scan->longest_line_no = lineno;
            }
        }
    }
    (void)last_sent;
    scan->lines_in_file     = lineno;
    scan->has_lead_percent  = first_is_pct;
    scan->has_trail_percent = last_is_pct;
    free(buf);
    free(raw);
    fclose(fp);

    if (p->macro_check) macro_report(&macro, p);
    macro_free(&macro);

    /* Framing lines we will add ourselves count towards the totals. */
    if (p->percent_frame) {
        size_t pl = 1 + eol_len(p->eol);
        if (!scan->has_lead_percent && job->start_line <= 1) {
            scan->lines_to_send++; scan->bytes_to_send += (long)pl;
        }
        if (!scan->has_trail_percent) {
            scan->lines_to_send++; scan->bytes_to_send += (long)pl;
        }
    }
    return DF_OK;
}

/* ---------------------------------------------------------------------------
 * Phase 2 state
 * ------------------------------------------------------------------------- */
typedef struct {
    int              fd;
    int              dry;
    const profile_t *p;

    /* receive-side line assembler + pending acknowledgements */
    char             rx[256];
    size_t           rxlen;
    int              acks;           /* acknowledgements received, unconsumed */
    int              machine_error;  /* an error/alarm line has arrived       */
    char             error_text[256];
} feeder_t;

/* Interpret one complete line received from the machine. */
static void rx_line(feeder_t *f, const char *line)
{
    log_verbose("machine: %s", line);
    if (f->p->ack != ACK_OK) return;

    if (!strncasecmp(line, "ok", 2)) {
        f->acks++;
    } else if (!strncasecmp(line, "error", 5) || !strncasecmp(line, "alarm", 5)) {
        f->machine_error = 1;
        snprintf(f->error_text, sizeof f->error_text, "%s", line);
    }
    /* anything else (status reports, [MSG:...], banners) is informational */
}

/* Read whatever the machine has sent and update acks/error state. */
static void rx_pump(feeder_t *f)
{
    if (f->fd < 0) return;
    unsigned char b[128];
    ssize_t n = serial_read(f->fd, b, sizeof b);
    for (ssize_t i = 0; i < n; i++) {
        if (f->p->ack == ACK_CHAR && b[i] == (unsigned char)f->p->ack_char) {
            log_debug("ack byte 0x%02x", b[i]);
            f->acks++;
        }
        if (b[i] == '\n' || b[i] == '\r') {
            if (f->rxlen) { f->rx[f->rxlen] = '\0'; rx_line(f, f->rx); f->rxlen = 0; }
        } else if (b[i] >= 0x20 && b[i] < 0x7f && f->rxlen < sizeof f->rx - 1) {
            f->rx[f->rxlen++] = (char)b[i];
        }
    }
}

/* Wait for the acknowledge of the line just sent.
 * Returns DF_OK, DF_MACHINE (error line), DF_ERROR (timeout) or DF_ABORTED. */
static int wait_ack(feeder_t *f)
{
    long deadline = now_ms() + f->p->ack_timeout_ms;
    for (;;) {
        rx_pump(f);
        if (f->machine_error) return DF_MACHINE;
        if (f->acks > 0) { f->acks--; return DF_OK; }
        control_poll();
        if (control_abort_requested()) return DF_ABORTED;
        if (now_ms() > deadline) {
            log_error("timed out after %ld ms waiting for the machine to acknowledge",
                      f->p->ack_timeout_ms);
            return DF_ERROR;
        }
        serial_wait_readable(f->fd, 50);
    }
}

/* Pause/abort checkpoint before each line. Returns DF_OK to proceed or
 * DF_ABORTED. While paused we keep servicing the receive side. */
static int checkpoint(feeder_t *f, long lineno)
{
    control_poll();
    if (control_paused() && !control_abort_requested()) {
        log_info("\n[PAUSED] holding before line %ld (p/space = resume, q = abort)", lineno);
        hw_set_state(HW_STATE_PAUSED);
        while (control_paused() && !control_abort_requested()) {
            sleep_ms(50);
            rx_pump(f);
            control_poll();
        }
        if (!control_abort_requested()) {
            log_info("[RESUMED]");
            hw_set_state(HW_STATE_RUNNING);
        }
    }
    return control_abort_requested() ? DF_ABORTED : DF_OK;
}

/* ---------------------------------------------------------------------------
 * Sending one block
 * ------------------------------------------------------------------------- */
typedef struct {
    long lines_sent, bytes_sent;
    int  in_flight;             /* a block was being transmitted at failure */
} counters_t;

/* Transmit (or report) one prepared block. `text` has no line ending.
 * `src_line` is the file line number it came from (0 for framing lines).
 * Returns a DF_* code. */
static int send_block(feeder_t *f, counters_t *c, const char *text, size_t len,
                      long src_line)
{
    const profile_t *p = f->p;
    const char *eol = eol_bytes(p->eol);
    size_t eol_n = eol_len(p->eol);

    if (f->dry) {
        /* The "demo / test" report: exactly what would go out, per block. */
        if (src_line) printf("[dry-run] line %-5ld -> \"%s\" %s\n", src_line, text, eol_label(p->eol));
        else          printf("[dry-run] (added)     -> \"%s\" %s\n", text, eol_label(p->eol));
        c->lines_sent++; c->bytes_sent += (long)(len + eol_n);
        return DF_OK;
    }

    c->in_flight = 1;
    if (serial_write(f->fd, text, len, p->char_delay_us) < 0 ||
        serial_write(f->fd, eol, eol_n, p->char_delay_us) < 0)
        return control_abort_requested() ? DF_ABORTED : DF_ERROR;

    /* Wait until the bytes have actually left. This is what makes it a
     * "drip": with XOFF asserted by the machine we sit here, one block
     * ahead at most, instead of filling the kernel's 4 KB tty buffer. */
    if (serial_wait_sent(f->fd, 1000000L * profile_bits_per_char(p) / p->baud) < 0)
        return control_abort_requested() ? DF_ABORTED : DF_ERROR;

    c->in_flight = 0;
    c->lines_sent++; c->bytes_sent += (long)(len + eol_n);

    if (p->ack != ACK_NONE) {
        int rc = wait_ack(f);
        if (rc != DF_OK) return rc;
    } else {
        rx_pump(f);                         /* keep the receive side drained */
    }
    return DF_OK;
}

/* Single-line progress for interactive use. */
static void show_progress(long lineno, long total_lines, const char *text, int tty)
{
    if (!tty) return;
    double pct = total_lines ? 100.0 * (double)lineno / (double)total_lines : 100.0;
    fprintf(stderr, "\r[%5.1f%%] line %ld/%ld  %-40.40s", pct, lineno, total_lines, text);
    fflush(stderr);
}

/* ---------------------------------------------------------------------------
 * Phase 2: run
 * ------------------------------------------------------------------------- */
int feeder_run(const feed_job_t *job, const feed_scan_t *scan, int fd)
{
    const profile_t *p = job->profile;
    feeder_t f = { .fd = fd, .dry = job->dry_run, .p = p };
    counters_t c = { 0, 0, 0 };

    FILE *fp = fopen(job->path, "r");
    if (!fp) { log_error("cannot open %s: %s", job->path, strerror(errno)); return DF_ERROR; }

    int  progress_tty = isatty(STDERR_FILENO) && log_verbosity() == 0 && !f.dry;
    long t0 = now_ms();
    int  rc = DF_OK;
    long lineno = 0, next_unsent = job->start_line > 0 ? job->start_line : 1;
    char *buf = NULL; size_t cap = 0;

    hw_set_state(HW_STATE_RUNNING);

    /* Leading % (only on a fresh start, and only if the file lacks one). */
    if (p->percent_frame && !scan->has_lead_percent && job->start_line <= 1) {
        rc = send_block(&f, &c, "%", 1, 0);
    }

    while (rc == DF_OK && getline(&buf, &cap, fp) != -1) {
        lineno++;
        if (lineno < job->start_line) continue;

        gcode_line_t g;
        gcode_prepare(buf, p, &g);
        if (g.error) { rc = DF_ERROR; break; }          /* scan should have caught it */
        if (g.skip) {
            if (f.dry) log_verbose("line %ld skipped (%s)", lineno, g.skip_why);
            else       log_debug("line %ld skipped (%s)", lineno, g.skip_why);
            continue;
        }

        if ((rc = checkpoint(&f, lineno)) != DF_OK) { next_unsent = lineno; break; }

        log_verbose("line %ld: %s", lineno, buf);
        next_unsent = lineno;
        rc = send_block(&f, &c, buf, g.len, lineno);
        if (rc != DF_OK) break;
        next_unsent = lineno + 1;

        show_progress(lineno, scan->lines_in_file, buf, progress_tty);
        sleep_ms(f.dry ? 0 : p->line_delay_ms);
    }
    free(buf);
    fclose(fp);

    /* Trailing % */
    if (rc == DF_OK && p->percent_frame && !scan->has_trail_percent)
        rc = send_block(&f, &c, "%", 1, 0);

    if (progress_tty) fputc('\n', stderr);

    /* ---- wrap-up ---- */
    if (rc == DF_OK) {
        hw_set_state(HW_STATE_DONE);
    } else {
        hw_set_state(HW_STATE_ERROR);
        if (fd >= 0) serial_flush_output(fd);     /* don't deliver a held-off backlog later */

        if (rc == DF_ABORTED)
            log_error("aborted by operator");
        else if (rc == DF_MACHINE)
            log_error("machine reported: %s", f.error_text);

        if (c.in_flight)
            log_error("WARNING: a block may have been partially transmitted. "
                      "Reset/cancel the program on the machine before resuming.");
        log_info("Stopped before confirming line %ld. Resume with: --start-line %ld",
                 next_unsent, next_unsent);
        log_info("(Resuming mid-program: re-establish modal state - tool, offsets, "
                 "spindle, feed - before starting.)");
    }

    char dur[32];
    format_duration((double)(now_ms() - t0) / 1000.0, dur, sizeof dur);
    log_info("%s: %ld blocks (%ld bytes) %s in %s",
             rc == DF_OK ? (f.dry ? "DRY RUN COMPLETE" : "DONE") : "STOPPED",
             c.lines_sent, c.bytes_sent, f.dry ? "reported" : "sent", dur);
    return rc;
}
