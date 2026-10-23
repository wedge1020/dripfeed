/*
 * feeder.h - the drip-feed engine.
 *
 * Two phases, so problems are found BEFORE anything reaches the machine:
 *
 *   1. feeder_scan() reads the whole file, prepares every line exactly as it
 *      will be sent, and reports any unusable line (non-ASCII, unterminated
 *      comment, longer than the profile allows) with its line number.
 *   2. feeder_run() then streams the lines: pause/abort checkpoints, flow
 *      control, optional per-line acknowledge, pacing, and progress output.
 *
 * feeder_run() with a dry-run job does everything except touch a serial
 * port; it prints what would be sent to stdout.
 */
#ifndef DRIPFEED_FEEDER_H
#define DRIPFEED_FEEDER_H

#include "profile.h"

typedef struct {
    const char      *path;         /* G-code file                           */
    const char      *device;       /* serial device (for messages)          */
    const profile_t *profile;
    long             start_line;   /* 1-based file line to start from       */
    int              dry_run;      /* report only, never open/write a port  */
} feed_job_t;

/* Results of the pre-flight scan. */
typedef struct {
    long lines_in_file;
    long lines_to_send;     /* from start_line on, incl. framing lines       */
    long lines_skipped;     /* blank / comment-only lines dropped            */
    long bytes_to_send;     /* including line endings                        */
    int  longest_line;      /* longest prepared block, in characters         */
    long longest_line_no;
    int  has_lead_percent;  /* file already starts with %                    */
    int  has_trail_percent; /* file already ends with %                      */
    long problems;          /* number of unusable lines found                */
} feed_scan_t;

/*
 * Scan the file. Problems are logged with line numbers and counted in
 * scan->problems. Returns DF_OK if the file could be read (even with
 * problems - check scan->problems), DF_ERROR if it could not be opened.
 */
int feeder_scan(const feed_job_t *job, feed_scan_t *scan);

/*
 * Send (or, for dry-run, report) the file. `fd` is an open serial port, or
 * -1 for dry-run. Returns a DF_* exit code (common.h).
 */
int feeder_run(const feed_job_t *job, const feed_scan_t *scan, int fd);

/* Estimated wire time in seconds for the scan (ignores machine response
 * time when an acknowledge is required). */
double feeder_estimate_seconds(const profile_t *p, const feed_scan_t *scan);

#endif /* DRIPFEED_FEEDER_H */
