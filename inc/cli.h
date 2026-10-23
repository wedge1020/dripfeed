/*
 * cli.h - command-line parsing.
 *
 * Settings options (baud, flow, ...) are not interpreted here. They are
 * collected, in order, as key/value overrides and later applied to the chosen
 * profile with profile_set_field(), so the command line and config files
 * share one parser and one set of error messages.
 */
#ifndef DRIPFEED_CLI_H
#define DRIPFEED_CLI_H

#include <stdio.h>

#define CLI_MAX_OVERRIDES 32

typedef struct {
    const char *key;
    const char *value;
} cli_override_t;

typedef struct {
    const char *file;           /* G-code file to send                      */
    const char *device;         /* -d                                       */
    const char *profile;        /* -p                                       */
    const char *config;         /* -C                                       */
    long        start_line;     /* -s (default 1)                           */
    int         dry_run;        /* -n                                       */
    int         verbosity;      /* number of -v                             */
    int         list_profiles;  /* -L                                       */
    int         show_profile;   /* --show-profile                           */
    int         help;           /* -h                                       */
    int         version;        /* -V / --version                           */

    cli_override_t overrides[CLI_MAX_OVERRIDES];
    int            n_overrides;
} cli_opts_t;

/* Parse argv. Returns 0, or -1 on a usage error (message already logged). */
int cli_parse(int argc, char **argv, cli_opts_t *o);

void cli_usage(FILE *out);
void cli_version(FILE *out);

#endif /* DRIPFEED_CLI_H */
