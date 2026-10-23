/*
 * config.h - configuration files.
 *
 * Format: a small INI dialect.
 *
 *     # comment            (also ';' at the start of a line; a trailing
 *                           " # note" after a value is ignored too)
 *
 *     [dripfeed]           global defaults (both optional)
 *     device  = /dev/serial0
 *     profile = lathe
 *
 *     [lathe]              any other section defines (or replaces) a profile
 *     base        = fanuc-0i        # start from an existing profile (must be
 *     description = Shop lathe      # the first key in the section)
 *     baud        = 4800
 *     strip_comments = yes
 *
 * Keys are the same names understood by profile_set_field() - see
 * docs/dripfeed.conf.example for the full list.
 *
 * Search order (later wins; command-line options win over everything):
 *     /etc/dripfeed.conf
 *     ~/.config/dripfeed/dripfeed.conf
 *     file named with --config
 */
#ifndef DRIPFEED_CONFIG_H
#define DRIPFEED_CONFIG_H

#include "profile.h"

/* Values from the optional [dripfeed] section. Empty string = not set. */
typedef struct {
    char device[256];
    char profile[PROFILE_NAME_MAX];
} config_globals_t;

/*
 * Load one config file, registering its profiles and filling *g.
 * Returns  0 on success,
 *          1 if the file doesn't exist and `required` is 0 (not an error),
 *         -1 on any error (a message with file:line has been logged; all
 *            errors in the file are reported, not just the first).
 */
int config_load(const char *path, config_globals_t *g, int required);

#endif /* DRIPFEED_CONFIG_H */
