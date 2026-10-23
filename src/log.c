/*
 * log.c - tiny leveled logger (see log.h).
 */
#include <stdarg.h>
#include <stdio.h>

#include "log.h"

static int g_verbosity;

void log_set_verbosity(int level) { g_verbosity = level; }
int  log_verbosity(void)          { return g_verbosity; }

/* Shared formatter: prefix + printf-style body + newline, to stderr. */
static void vlog(const char *prefix, const char *fmt, va_list ap)
{
    fputs(prefix, stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("dripfeed: error: ", fmt, ap);
    va_end(ap);
}

/* Informational messages are always shown (they are few: banner, summary). */
void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("", fmt, ap);
    va_end(ap);
}

void log_verbose(const char *fmt, ...)
{
    if (g_verbosity < 1) return;
    va_list ap;
    va_start(ap, fmt);
    vlog("  ", fmt, ap);
    va_end(ap);
}

void log_debug(const char *fmt, ...)
{
    if (g_verbosity < 2) return;
    va_list ap;
    va_start(ap, fmt);
    vlog("    [dbg] ", fmt, ap);
    va_end(ap);
}
