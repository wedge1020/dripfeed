/*
 * profile.c - built-in machine profiles, the profile registry, and the
 * field parser shared by config files and command-line options.
 *
 * ---------------------------------------------------------------------------
 * ABOUT THE BUILT-IN VALUES
 * ---------------------------------------------------------------------------
 * These are *starting points*, compiled from vendor/integrator documentation
 * and DNC-software setup guides (see docs/PROFILES.md for sources and
 * confidence). They describe the settings on the PC/host side. Controls are
 * configurable and often differ machine to machine: ALWAYS confirm against the
 * parameters of the actual control, and prove out a new setup with the
 * chuck empty before cutting. Override anything with a config file or options.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "profile.h"
#include "serial.h"

#define MAX_PROFILES 64

static profile_t g_profiles[MAX_PROFILES];
static int       g_count;

/* ---------------------------------------------------------------------------
 * Built-in table. Fields not named take the value from profile_defaults()
 * (see below) because every entry is built by overlaying on those defaults.
 * ------------------------------------------------------------------------- */
typedef struct {
    const char *name, *desc;
    int baud, databits; char parity; int stopbits; flow_t flow;
    eol_t eol;
    int percent, uppercase, max_line;
    ack_mode_t ack;
    long ack_timeout_ms;
} builtin_t;

static const builtin_t BUILTINS[] = {
    /* name            description                                          baud  db par sb  flow          eol      %  UP maxl ack       timeout */
    { "generic",       "Neutral 9600 8N1, XON/XOFF, no reformatting",       9600, 8, 'N', 1, FLOW_XONXOFF, EOL_CRLF, 0, 0,  0, ACK_NONE, 0 },

    /* Fanuc: PC-side settings per DNC setup guides; 7E2 is the classic ISO
     * setting. Hardware handshake listed there; two-wire XON/XOFF is a common
     * alternative (-f xonxoff). Baud must match the control's channel
     * parameters (e.g. 0i: #0020 selects channel, #0101/#0102/#0103). */
    { "fanuc-0i",      "Fanuc 0i-M/T (9600 7E2, RTS/CTS, LF, % framing)",   9600, 7, 'E', 2, FLOW_RTSCTS,  EOL_LF,   1, 1,  0, ACK_NONE, 0 },
    { "fanuc-0m",      "Fanuc 0-M/T, 0-C/D (2400 7E2, RTS/CTS, LF)",        2400, 7, 'E', 2, FLOW_RTSCTS,  EOL_LF,   1, 1,  0, ACK_NONE, 0 },
    { "fanuc-10-15",   "Fanuc 10/11/12/15 (4800 7E2, RTS/CTS, LF)",         4800, 7, 'E', 2, FLOW_RTSCTS,  EOL_LF,   1, 1,  0, ACK_NONE, 0 },

    /* Haas: Settings 11 (baud) 12 (parity) 13 (stop bits) 14 (sync)
     * 37 (data bits) on the control. Values below are the commonly
     * documented 9600 8N1 XON/XOFF; Haas output block end is CR LF. */
    { "haas",          "Haas (Settings 11-14,37): 9600 8N1, XON/XOFF, CRLF",9600, 8, 'N', 1, FLOW_XONXOFF, EOL_CRLF, 1, 0,  0, ACK_NONE, 0 },

    /* Mazak: T-series programs carry no % framing per the setup guides. */
    { "mazak-m32",     "Mazak M32 / M32 Plus (4800 7E2, RTS/CTS, CRLF)",    4800, 7, 'E', 2, FLOW_RTSCTS,  EOL_CRLF, 1, 1,  0, ACK_NONE, 0 },
    { "mazak-t32",     "Mazak T32 M2 (4800 7E2, XON/XOFF+RTS/CTS, CRLF)",   4800, 7, 'E', 2, FLOW_BOTH,    EOL_CRLF, 0, 1,  0, ACK_NONE, 0 },

    /* Okuma OSP */
    { "okuma-osp5000", "Okuma OSP 5000/5020 (4800 7E1, both, LF, %)",       4800, 7, 'E', 1, FLOW_BOTH,    EOL_LF,   1, 1,  0, ACK_NONE, 0 },
    { "okuma-ospu100", "Okuma OSP-U100 (9600 7E2, RTS/CTS, CRLF, %)",       9600, 7, 'E', 2, FLOW_RTSCTS,  EOL_CRLF, 1, 1,  0, ACK_NONE, 0 },

    /* Siemens 840D/802D: 8N1 is typical; 9600 or 19200 depending on
     * machine. Siemens usually also wants a "%_N_NAME_MPF" header block in
     * the file itself, which dripfeed passes through unchanged. */
    { "siemens-840d",  "Siemens 840D/802D (9600 8N1, XON/XOFF, CRLF)",      9600, 8, 'N', 1, FLOW_XONXOFF, EOL_CRLF, 0, 1,  0, ACK_NONE, 0 },

    /* grbl-style: strict send-response. Comments must be stripped because
     * the controller's line buffer is small. */
    { "grbl",          "grbl 1.x over USB serial (115200 8N1, wait for ok)",115200,8, 'N', 1, FLOW_NONE,    EOL_LF,   0, 1, 80, ACK_OK,   120000 },
};

/* ---------------------------------------------------------------------------
 * Defaults
 * ------------------------------------------------------------------------- */
void profile_defaults(profile_t *p)
{
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "custom");
    p->baud = 9600; p->databits = 8; p->parity = 'N'; p->stopbits = 1;
    p->flow = FLOW_XONXOFF;
    p->eol = EOL_CRLF;
    p->ack = ACK_NONE; p->ack_char = -1; p->ack_timeout_ms = 30000;
    p->strip_blank = 1;     /* blank lines carry no information; save buffer */
}

void profile_registry_init(void)
{
    g_count = 0;
    for (size_t i = 0; i < sizeof BUILTINS / sizeof BUILTINS[0]; i++) {
        const builtin_t *b = &BUILTINS[i];
        profile_t p;
        profile_defaults(&p);
        snprintf(p.name, sizeof p.name, "%s", b->name);
        snprintf(p.description, sizeof p.description, "%s", b->desc);
        p.baud = b->baud; p.databits = b->databits; p.parity = b->parity;
        p.stopbits = b->stopbits; p.flow = b->flow; p.eol = b->eol;
        p.percent_frame = b->percent; p.uppercase = b->uppercase;
        p.max_line_len = b->max_line;
        p.ack = b->ack;
        if (b->ack_timeout_ms) p.ack_timeout_ms = b->ack_timeout_ms;
        if (p.ack == ACK_OK)   p.strip_comments = 1;   /* see grbl note above */
        profile_add(&p);
    }
}

/* ---------------------------------------------------------------------------
 * Registry
 * ------------------------------------------------------------------------- */
const profile_t *profile_find(const char *name)
{
    for (int i = 0; i < g_count; i++)
        if (!strcasecmp(g_profiles[i].name, name)) return &g_profiles[i];
    return NULL;
}

int profile_add(const profile_t *p)
{
    for (int i = 0; i < g_count; i++) {
        if (!strcasecmp(g_profiles[i].name, p->name)) {
            g_profiles[i] = *p;               /* user config replaces built-in */
            return 0;
        }
    }
    if (g_count >= MAX_PROFILES) return -1;
    g_profiles[g_count++] = *p;
    return 0;
}

void profile_list(FILE *out)
{
    for (int i = 0; i < g_count; i++)
        fprintf(out, "  %-15s %s\n", g_profiles[i].name, g_profiles[i].description);
}

/* ---------------------------------------------------------------------------
 * Enum <-> text
 * ------------------------------------------------------------------------- */
const char *flow_name(flow_t f)
{
    switch (f) {
    case FLOW_NONE:    return "none";
    case FLOW_XONXOFF: return "xonxoff";
    case FLOW_RTSCTS:  return "rtscts";
    case FLOW_BOTH:    return "both";
    }
    return "?";
}

const char *eol_name(eol_t e)
{
    return e == EOL_CRLF ? "crlf" : e == EOL_LF ? "lf" : "cr";
}
const char *eol_bytes(eol_t e)
{
    return e == EOL_CRLF ? "\r\n" : e == EOL_LF ? "\n" : "\r";
}
size_t eol_len(eol_t e) { return e == EOL_CRLF ? 2 : 1; }
const char *eol_label(eol_t e)
{
    return e == EOL_CRLF ? "<CR><LF>" : e == EOL_LF ? "<LF>" : "<CR>";
}

const char *ack_name(const profile_t *p)
{
    static char buf[16];
    switch (p->ack) {
    case ACK_NONE: return "none";
    case ACK_OK:   return "ok";
    case ACK_CHAR: snprintf(buf, sizeof buf, "char:0x%02x", p->ack_char & 0xff);
                   return buf;
    }
    return "?";
}

int profile_bits_per_char(const profile_t *p)
{
    return 1 + p->databits + (p->parity != 'N') + p->stopbits;
}

/* ---------------------------------------------------------------------------
 * Field parsing
 * ------------------------------------------------------------------------- */
static void seterr(char *err, size_t n, const char *fmt, const char *a, const char *b)
{
    if (err && n) snprintf(err, n, fmt, a, b);
}

/* Accepts yes/no, true/false, on/off, 1/0. */
static int parse_bool(const char *s, int *out)
{
    static const char *yes[] = { "1", "yes", "true", "on" };
    static const char *no[]  = { "0", "no",  "false", "off" };
    for (int i = 0; i < 4; i++) {
        if (!strcasecmp(s, yes[i])) { *out = 1; return 0; }
        if (!strcasecmp(s, no[i]))  { *out = 0; return 0; }
    }
    return -1;
}

/* Strict integer parse: whole string must be a number (base 10 or 0x..). */
static int parse_long(const char *s, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end) return -1;
    *out = v;
    return 0;
}

/* "none" | "ok" | "char:HEX"  (HEX with or without 0x) */
static int parse_ack(profile_t *p, const char *v)
{
    if (!strcasecmp(v, "none")) { p->ack = ACK_NONE; return 0; }
    if (!strcasecmp(v, "ok"))   { p->ack = ACK_OK;   return 0; }
    if (!strncasecmp(v, "char:", 5)) {
        char *end;
        long c = strtol(v + 5, &end, 16);
        if (end == v + 5 || *end || c < 0 || c > 255) return -1;
        p->ack = ACK_CHAR; p->ack_char = (int)c;
        return 0;
    }
    return -1;
}

int profile_set_field(profile_t *p, const char *key, const char *value,
                      char *err, size_t errlen)
{
    long n; int b;

    /* ---- text fields ---- */
    if (!strcasecmp(key, "description")) {
        snprintf(p->description, sizeof p->description, "%s", value);
        return 0;
    }

    /* ---- numeric fields ---- */
#define NUM(field, name, lo, hi)                                              \
    if (!strcasecmp(key, name)) {                                             \
        if (parse_long(value, &n) || n < (lo) || n > (hi)) {                  \
            seterr(err, errlen, "%s: invalid value '%s'", name, value);       \
            return -1;                                                        \
        }                                                                     \
        p->field = (__typeof__(p->field))n;                                   \
        return 0;                                                             \
    }
    NUM(baud,           "baud",           50, 4000000)
    NUM(databits,       "databits",       5,  8)
    NUM(stopbits,       "stopbits",       1,  2)
    NUM(max_line_len,   "max_line_len",   0,  100000)
    NUM(line_delay_ms,  "line_delay_ms",  0,  3600000)
    NUM(char_delay_us,  "char_delay_us",  0,  10000000)
    NUM(ack_timeout_ms, "ack_timeout_ms", 1,  86400000)
#undef NUM

    /* ---- boolean fields ---- */
#define BOOL(field, name)                                                     \
    if (!strcasecmp(key, name)) {                                             \
        if (parse_bool(value, &b)) {                                          \
            seterr(err, errlen, "%s: expected yes/no, got '%s'", name, value);\
            return -1;                                                        \
        }                                                                     \
        p->field = b;                                                         \
        return 0;                                                             \
    }
    BOOL(percent_frame,  "percent_frame")
    BOOL(strip_comments, "strip_comments")
    BOOL(strip_blank,    "strip_blank")
    BOOL(uppercase,      "uppercase")
#undef BOOL

    /* ---- enumerations ---- */
    if (!strcasecmp(key, "parity")) {
        char c = (char)toupper((unsigned char)value[0]);
        if (value[1] || (c != 'N' && c != 'E' && c != 'O')) {
            seterr(err, errlen, "%s: expected N, E or O, got '%s'", "parity", value);
            return -1;
        }
        p->parity = c;
        return 0;
    }
    if (!strcasecmp(key, "flow")) {
        if      (!strcasecmp(value, "none"))    p->flow = FLOW_NONE;
        else if (!strcasecmp(value, "xonxoff")) p->flow = FLOW_XONXOFF;
        else if (!strcasecmp(value, "rtscts"))  p->flow = FLOW_RTSCTS;
        else if (!strcasecmp(value, "both"))    p->flow = FLOW_BOTH;
        else { seterr(err, errlen, "%s: expected none|xonxoff|rtscts|both, got '%s'", "flow", value); return -1; }
        return 0;
    }
    if (!strcasecmp(key, "eol")) {
        if      (!strcasecmp(value, "crlf")) p->eol = EOL_CRLF;
        else if (!strcasecmp(value, "lf"))   p->eol = EOL_LF;
        else if (!strcasecmp(value, "cr"))   p->eol = EOL_CR;
        else { seterr(err, errlen, "%s: expected crlf|lf|cr, got '%s'", "eol", value); return -1; }
        return 0;
    }
    if (!strcasecmp(key, "ack")) {
        if (parse_ack(p, value)) {
            seterr(err, errlen, "%s: expected none|ok|char:HEX, got '%s'", "ack", value);
            return -1;
        }
        return 0;
    }

    seterr(err, errlen, "unknown setting '%s'%s", key, "");
    return -1;
}

/* ---------------------------------------------------------------------------
 * Validation and display
 * ------------------------------------------------------------------------- */
int profile_validate(const profile_t *p, char *err, size_t errlen)
{
    if (!serial_baud_supported(p->baud)) {
        snprintf(err, errlen, "baud rate %d is not supported", p->baud);
        return -1;
    }
    if (p->databits < 5 || p->databits > 8) {
        snprintf(err, errlen, "databits must be 5-8 (got %d)", p->databits);
        return -1;
    }
    if (p->ack == ACK_CHAR && (p->ack_char < 0 || p->ack_char > 255)) {
        snprintf(err, errlen, "ack char is out of range");
        return -1;
    }
    return 0;
}

void profile_print(FILE *out, const profile_t *p)
{
    fprintf(out, "Profile: %s - %s\n", p->name, p->description);
    fprintf(out, "  serial        %d baud, %d%c%d, flow=%s\n",
            p->baud, p->databits, p->parity, p->stopbits, flow_name(p->flow));
    fprintf(out, "  line end      %s\n", eol_label(p->eol));
    fprintf(out, "  %% framing     %s\n", p->percent_frame  ? "yes" : "no");
    fprintf(out, "  strip comments%s\n", p->strip_comments ? " yes" : " no");
    fprintf(out, "  strip blank   %s\n", p->strip_blank    ? "yes" : "no");
    fprintf(out, "  upper case    %s\n", p->uppercase      ? "yes" : "no");
    fprintf(out, "  max line len  %d%s\n", p->max_line_len, p->max_line_len ? "" : " (unlimited)");
    fprintf(out, "  line delay    %ld ms\n", p->line_delay_ms);
    fprintf(out, "  char delay    %ld us\n", p->char_delay_us);
    fprintf(out, "  acknowledge   %s", ack_name(p));
    if (p->ack != ACK_NONE) fprintf(out, " (timeout %ld ms)", p->ack_timeout_ms);
    fputc('\n', out);
}
