/*
 * config.c - INI-style configuration file parser (see config.h).
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "log.h"

/* Trim leading/trailing whitespace in place; returns pointer into s. */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

/*
 * Reduce a raw value to its content:
 *   "quoted text"   -> quoted text      (anything after the closing quote,
 *                                        e.g. a trailing comment, is ignored)
 *   plain # note    -> plain            (a '#' or ';' preceded by whitespace
 *                                        starts an inline comment)
 * Because '#' only counts after whitespace, values such as "char:3e" or
 * "/dev/ttyUSB0" are unaffected; quote the value if it needs " #" in it.
 */
static char *clean_value(char *s)
{
    if (*s == '"') {
        char *close = strchr(s + 1, '"');
        if (close) { *close = '\0'; return s + 1; }
        return s;                               /* unmatched quote: keep as is */
    }
    for (char *p = s; *p; p++) {
        if ((*p == '#' || *p == ';') && p > s && isspace((unsigned char)p[-1])) {
            *p = '\0';
            break;
        }
    }
    return trim(s);
}

/* Profile names: letters, digits, '.', '_', '-'. */
static int valid_name(const char *s)
{
    if (!*s || strlen(s) >= PROFILE_NAME_MAX) return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-') return 0;
    return 1;
}

int config_load(const char *path, config_globals_t *g, int required)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT && !required) return 1;
        log_error("cannot read config %s: %s", path, strerror(errno));
        return -1;
    }
    log_verbose("loading config %s", path);

    enum { SEC_NONE, SEC_GLOBAL, SEC_PROFILE } section = SEC_NONE;
    profile_t cur;                  /* profile being assembled            */
    int  cur_keys = 0;              /* keys seen so far in this section   */
    int  errors = 0;
    int  lineno = 0;
    char *buf = NULL; size_t cap = 0;
    char err[160];

/* Finish the current profile section: validate and register it. */
#define FLUSH_PROFILE()                                                       \
    do {                                                                      \
        if (section == SEC_PROFILE) {                                         \
            if (profile_validate(&cur, err, sizeof err)) {                    \
                log_error("%s: [%s]: %s", path, cur.name, err); errors++;     \
            } else if (profile_add(&cur)) {                                   \
                log_error("%s: too many profiles", path); errors++;           \
            }                                                                 \
        }                                                                     \
    } while (0)

    while (getline(&buf, &cap, fp) != -1) {
        lineno++;
        char *s = trim(buf);
        if (!*s || *s == '#' || *s == ';') continue;

        /* ---- [section] ---- */
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close || *trim(close + 1)) {
                log_error("%s:%d: malformed section header", path, lineno); errors++;
                continue;
            }
            *close = '\0';
            char *name = trim(s + 1);

            FLUSH_PROFILE();
            cur_keys = 0;
            if (!strcasecmp(name, "dripfeed")) {
                section = SEC_GLOBAL;
            } else if (valid_name(name)) {
                section = SEC_PROFILE;
                profile_defaults(&cur);
                snprintf(cur.name, sizeof cur.name, "%s", name);
                snprintf(cur.description, sizeof cur.description, "User profile from %s", path);
            } else {
                log_error("%s:%d: invalid profile name '%s'", path, lineno, name);
                errors++;
                section = SEC_NONE;
            }
            continue;
        }

        /* ---- key = value ---- */
        char *eq = strchr(s, '=');
        if (!eq) { log_error("%s:%d: expected 'key = value'", path, lineno); errors++; continue; }
        *eq = '\0';
        char *key = trim(s);
        char *val = clean_value(trim(eq + 1));

        if (section == SEC_NONE) {
            log_error("%s:%d: setting outside any [section]", path, lineno); errors++;
            continue;
        }

        if (section == SEC_GLOBAL) {
            if (!strcasecmp(key, "device"))
                snprintf(g->device, sizeof g->device, "%s", val);
            else if (!strcasecmp(key, "profile"))
                snprintf(g->profile, sizeof g->profile, "%s", val);
            else { log_error("%s:%d: unknown [dripfeed] key '%s'", path, lineno, key); errors++; }
            continue;
        }

        /* profile section */
        if (!strcasecmp(key, "base")) {
            const profile_t *b = profile_find(val);
            if (!b)       { log_error("%s:%d: unknown base profile '%s'", path, lineno, val); errors++; }
            else if (cur_keys) { log_error("%s:%d: 'base' must be the first key in the section", path, lineno); errors++; }
            else {
                char keep[PROFILE_NAME_MAX];
                snprintf(keep, sizeof keep, "%s", cur.name);
                cur = *b;                                  /* inherit everything */
                snprintf(cur.name, sizeof cur.name, "%s", keep);
                snprintf(cur.description, sizeof cur.description, "Based on %s (%s)", b->name, path);
            }
        } else if (profile_set_field(&cur, key, val, err, sizeof err)) {
            log_error("%s:%d: %s", path, lineno, err); errors++;
        }
        cur_keys++;
    }
    FLUSH_PROFILE();
#undef FLUSH_PROFILE

    free(buf);
    fclose(fp);
    return errors ? -1 : 0;
}
