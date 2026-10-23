/*
 * log.h - tiny leveled logger. Everything goes to stderr so that stdout stays
 * free for machine-readable output (the --dry-run report, --list-profiles).
 */
#ifndef DRIPFEED_LOG_H
#define DRIPFEED_LOG_H

/* Verbosity: 0 = normal, 1 = -v (per-line detail), 2 = -vv (bytes on the wire). */
void log_set_verbosity(int level);
int  log_verbosity(void);

/* printf-style helpers. log_error always prints; the others honour verbosity. */
void log_error(const char *fmt, ...)   __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...)    __attribute__((format(printf, 1, 2)));
void log_info(const char *fmt, ...)    __attribute__((format(printf, 1, 2)));
void log_verbose(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_debug(const char *fmt, ...)   __attribute__((format(printf, 1, 2)));

#endif /* DRIPFEED_LOG_H */
