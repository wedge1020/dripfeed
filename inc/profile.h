/*
 * profile.h - machine/controller profiles.
 *
 * A "profile" is the complete set of serial-line and G-code-formatting
 * settings needed to talk to one kind of machine: baud/framing, flow control,
 * line endings, program framing (%), comment stripping, acknowledge handling,
 * pacing and so on.
 *
 * Profiles come from three places, later ones winning:
 *   1. the built-in table (src/profile.c)
 *   2. profile sections in config files (see config.h / docs/dripfeed.conf.example)
 *   3. individual command-line options, which override single fields
 *
 * All three go through profile_set_field(), so a setting means exactly the
 * same thing whether it is written in a config file or typed on the command
 * line.
 */
#ifndef DRIPFEED_PROFILE_H
#define DRIPFEED_PROFILE_H

#include <stddef.h>
#include <stdio.h>

#define PROFILE_NAME_MAX  32
#define PROFILE_DESC_MAX  96

/* Flow control. "both" = XON/XOFF *and* RTS/CTS (some controls want both). */
typedef enum { FLOW_NONE, FLOW_XONXOFF, FLOW_RTSCTS, FLOW_BOTH } flow_t;

/* Line terminator sent after every block. */
typedef enum { EOL_CRLF, EOL_LF, EOL_CR } eol_t;

/*
 * How we know the machine has accepted a block:
 *   ACK_NONE - we don't. Rely on flow control alone (classic DNC / drip feed).
 *   ACK_CHAR - wait for one specific byte after each line (e.g. 0x06 ACK, '>').
 *   ACK_OK   - wait for a response line starting "ok" (grbl-style). A line
 *              starting "error" or "ALARM" stops the transfer.
 */
typedef enum { ACK_NONE, ACK_CHAR, ACK_OK } ack_mode_t;

typedef struct {
    char        name[PROFILE_NAME_MAX];
    char        description[PROFILE_DESC_MAX];

    /* serial line */
    int         baud;
    int         databits;        /* 7 or 8                                   */
    char        parity;          /* 'N', 'E' or 'O'                          */
    int         stopbits;        /* 1 or 2                                   */
    flow_t      flow;

    /* G-code formatting */
    eol_t       eol;
    int         percent_frame;   /* wrap program in '%' lines if not already */
    int         strip_comments;  /* remove (..) and ;.. comments             */
    int         strip_blank;     /* don't send empty lines                   */
    int         uppercase;       /* force upper case                         */
    int         max_line_len;    /* longest block allowed, 0 = unlimited     */
    int         macro_check;     /* warn about Macro B constructs that may
                                    not work when drip-fed (never blocks)    */

    /* pacing / handshaking */
    long        line_delay_ms;   /* pause after each line                    */
    long        char_delay_us;   /* pause after each character               */
    ack_mode_t  ack;
    int         ack_char;        /* byte to wait for when ack == ACK_CHAR    */
    long        ack_timeout_ms;  /* give up waiting for an ack after this    */
} profile_t;

/* ---- registry --------------------------------------------------------- */

/* Load the built-in profiles. Call once at start-up. */
void profile_registry_init(void);

/* Case-insensitive lookup. Returns NULL if unknown. */
const profile_t *profile_find(const char *name);

/* Add a profile, replacing any existing one of the same name. 0 on success. */
int profile_add(const profile_t *p);

/* Print "name  description" for every known profile. */
void profile_list(FILE *out);

/* ---- single profile operations ---------------------------------------- */

/* Fill *p with safe, generic defaults (9600 8N1, XON/XOFF, CRLF). */
void profile_defaults(profile_t *p);

/*
 * Set one field from text, e.g. ("baud","4800") or ("flow","xonxoff").
 * This is the single parser used by config files and the command line.
 * Returns 0 on success, -1 with a message in err on failure.
 * Unknown keys are an error so typos don't silently do nothing.
 */
int profile_set_field(profile_t *p, const char *key, const char *value,
                      char *err, size_t errlen);

/* Cross-field sanity checks (e.g. 8 data bits with even parity is allowed,
 * 9 data bits is not). 0 if OK, -1 with message in err. */
int profile_validate(const profile_t *p, char *err, size_t errlen);

/* Human-readable dump of every setting. */
void profile_print(FILE *out, const profile_t *p);

/* Bits on the wire per character: start + data + parity + stop. */
int profile_bits_per_char(const profile_t *p);

/* ---- enum <-> text helpers -------------------------------------------- */

const char *flow_name(flow_t f);
const char *eol_name(eol_t e);
const char *eol_bytes(eol_t e);     /* "\r\n", "\n" or "\r" */
size_t      eol_len(eol_t e);
const char *eol_label(eol_t e);     /* "<CR><LF>" etc, for reports */
const char *ack_name(const profile_t *p);   /* "none", "ok", "char:0x06" */

#endif /* DRIPFEED_PROFILE_H */
