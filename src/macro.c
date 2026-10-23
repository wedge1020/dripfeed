/*
 * macro.c - advisory Fanuc Macro B scan (see macro.h).
 *
 * This is a deliberately small, forgiving scanner, not a parser. It looks at
 * each block for a handful of keywords and records what it sees. It ignores
 * text inside (parenthesised comments) so words in messages don't confuse it.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "macro.h"

#define MAX_REPORTED 8          /* cap per-category warnings to keep output readable */

void macro_init(macro_ctx_t *m) { memset(m, 0, sizeof *m); }

void macro_free(macro_ctx_t *m)
{
    free(m->labels);
    free(m->gotos);
    memset(m, 0, sizeof *m);
}

/* ---- growable arrays ------------------------------------------------- */
static void add_label(macro_ctx_t *m, long label, long line)
{
    if (m->n_labels == m->cap_labels) {
        size_t nc = m->cap_labels ? m->cap_labels * 2 : 64;
        macro_label_t *p = realloc(m->labels, nc * sizeof *p);
        if (!p) return;                         /* advisory only: give up quietly */
        m->labels = p; m->cap_labels = nc;
    }
    m->labels[m->n_labels].label = label;
    m->labels[m->n_labels].line  = line;
    m->n_labels++;
}

static void add_goto(macro_ctx_t *m, long target, long line, int computed)
{
    if (m->n_gotos == m->cap_gotos) {
        size_t nc = m->cap_gotos ? m->cap_gotos * 2 : 32;
        macro_goto_t *p = realloc(m->gotos, nc * sizeof *p);
        if (!p) return;
        m->gotos = p; m->cap_gotos = nc;
    }
    m->gotos[m->n_gotos].target = target;
    m->gotos[m->n_gotos].line = line;
    m->gotos[m->n_gotos].computed = computed;
    m->n_gotos++;
}

static void add_call(macro_ctx_t *m, const char *what, const char *digits, long line)
{
    char key[32];
    snprintf(key, sizeof key, "%s P%s", what, digits);
    for (size_t i = 0; i < m->n_calls; i++)
        if (!strcmp(m->calls[i], key)) return;          /* already noted */
    if (m->n_calls < sizeof m->calls / sizeof m->calls[0]) {
        snprintf(m->calls[m->n_calls], sizeof m->calls[0], "%s", key);
        m->call_first_line[m->n_calls] = line;
        m->n_calls++;
    }
}

/* ---- small lexical helpers ------------------------------------------ */
/* Case-insensitive: does `s` start with keyword `kw`? */
static int starts_with_ci(const char *s, const char *kw)
{
    for (; *kw; s++, kw++)
        if (toupper((unsigned char)*s) != *kw) return 0;
    return 1;
}

static const char *skip_sp(const char *s) { while (*s == ' ') s++; return s; }

/* Read up to n digits into buf; returns pointer after them. */
static const char *take_digits(const char *s, char *buf, size_t n)
{
    size_t k = 0;
    while (isdigit((unsigned char)*s)) { if (k + 1 < n) buf[k++] = *s; s++; }
    buf[k] = '\0';
    return s;
}

/* ---------------------------------------------------------------------------
 * Scan one block
 * ------------------------------------------------------------------------- */
void macro_scan_block(macro_ctx_t *m, const char *s, const char *raw,
                      long lineno, const profile_t *p)
{
    char digits[16];

    /* --- block-start items: optional "/" or "/n", then N label or O number --- */
    const char *h = skip_sp(s);
    if (*h == '/') { h++; if (isdigit((unsigned char)*h)) h++; h = skip_sp(h); }

    if ((*h == 'N' || *h == 'n') && isdigit((unsigned char)h[1])) {
        take_digits(h + 1, digits, sizeof digits);
        add_label(m, atol(digits), lineno);
    } else if ((*h == 'O' || *h == 'o') && isdigit((unsigned char)h[1])) {
        take_digits(h + 1, digits, sizeof digits);
        if (m->n_progs < sizeof m->prog_no / sizeof m->prog_no[0]) {
            m->prog_no[m->n_progs]   = atol(digits);
            m->prog_line[m->n_progs] = lineno;
            m->n_progs++;
        } else {
            m->extra_progs++;
        }
    }

    /* --- walk the block for keywords, skipping (comments) --- */
    int used_macro = 0, depth = 0;
    for (const char *c = s; *c; c++) {
        if (*c == '(') { depth = 1; continue; }
        if (depth)     { if (*c == ')') depth = 0; continue; }

        if (*c == '#') used_macro = 1;

        /* A keyword must not be the tail of a longer word, so it can't be
         * preceded by a letter. A preceding DIGIT is fine and common:
         * G-code packs words together ("N10G65P9001", "X1.M98P12"). */
        if (c > s && isalpha((unsigned char)c[-1])) continue;

        if (starts_with_ci(c, "GOTO")) {
            used_macro = 1;
            const char *a = skip_sp(c + 4);
            if (isdigit((unsigned char)*a)) {
                take_digits(a, digits, sizeof digits);
                add_goto(m, atol(digits), lineno, 0);
            } else {
                add_goto(m, 0, lineno, 1);              /* GOTO #n / GOTO [expr] */
            }
        } else if (starts_with_ci(c, "WHILE")) {
            used_macro = 1;
            if (!m->while_count++) m->first_while_line = lineno;
        } else if (starts_with_ci(c, "IF")) {
            used_macro = 1;
        } else {
            /* program calls: G65 / G66 / M98 / M198 followed by P<digits> */
            const char *what = NULL; size_t klen = 0;
            if      (starts_with_ci(c, "G65"))  { what = "G65";  klen = 3; }
            else if (starts_with_ci(c, "G66"))  { what = "G66";  klen = 3; }
            else if (starts_with_ci(c, "M198")) { what = "M198"; klen = 4; }
            else if (starts_with_ci(c, "M98"))  { what = "M98";  klen = 3; }
            if (what && !isdigit((unsigned char)c[klen])) {   /* not M980, G650 .. */
                if (*what == 'G') used_macro = 1;
                for (const char *q = c + klen; *q && *q != '('; q++) {
                    if ((*q == 'P' || *q == 'p') && isdigit((unsigned char)q[1])) {
                        take_digits(q + 1, digits, sizeof digits);
                        add_call(m, what, digits, lineno);
                        break;
                    }
                }
            }
        }
    }
    if (used_macro) m->macro_blocks++;

    /* --- comment stripping that would delete operator-message text --- */
    if (p->strip_comments && raw && strchr(raw, '(') &&
        (strstr(raw, "#3006") || strstr(raw, "(MSG") || strstr(raw, "(msg"))) {
        if (!m->lost_msg_blocks++) m->first_lost_msg_line = lineno;
    }
}

/* ---------------------------------------------------------------------------
 * Report
 * ------------------------------------------------------------------------- */

/* Line number of the first occurrence of `label`, or 0 if absent. */
static long find_label(const macro_ctx_t *m, long label)
{
    for (size_t i = 0; i < m->n_labels; i++)
        if (m->labels[i].label == label) return m->labels[i].line;
    return 0;
}

void macro_report(const macro_ctx_t *m, const profile_t *p)
{
    (void)p;
    if (m->macro_blocks)
        log_info("  macro B   %ld block(s) use variables/branches/calls - sent unchanged",
                 m->macro_blocks);

    int shown_back = 0, back = 0, computed = 0, missing = 0, forward = 0;
    for (size_t i = 0; i < m->n_gotos; i++) {
        const macro_goto_t *g = &m->gotos[i];
        if (g->computed) { computed++; continue; }
        long to = find_label(m, g->target);
        if (!to) {
            if (++missing <= MAX_REPORTED)
                log_warn("line %ld: GOTO %ld - no block N%ld in this file", g->line, g->target, g->target);
        } else if (to <= g->line) {
            back++;
            if (++shown_back <= MAX_REPORTED)
                log_warn("line %ld: GOTO %ld jumps BACK to line %ld. A stream the control "
                         "has already consumed generally can't be re-read, so this may "
                         "alarm or misbehave when drip-fed (verify in your control's manual)",
                         g->line, g->target, to);
        } else {
            forward++;
            log_verbose("line %ld: GOTO %ld jumps forward to line %ld (depends on the "
                        "control's buffering when drip-fed)", g->line, g->target, to);
        }
    }
    if (back > MAX_REPORTED)    log_warn("... and %d more backward GOTO(s)", back - MAX_REPORTED);
    if (missing > MAX_REPORTED) log_warn("... and %d more GOTO(s) with missing targets", missing - MAX_REPORTED);
    if (computed)
        log_warn("%d computed GOTO(s) (GOTO #n / GOTO [expr]) - targets can't be checked in advance",
                 computed);
    (void)forward;

    if (m->while_count)
        log_warn("%ld WHILE loop(s) (first at line %ld). Loops re-read earlier blocks, "
                 "which a streamed program may not allow - test with the chuck empty",
                 m->while_count, m->first_while_line);

    if (m->n_calls) {
        char list[256] = "";
        for (size_t i = 0; i < m->n_calls; i++) {
            size_t used = strlen(list);
            if (used + 40 < sizeof list)
                snprintf(list + used, sizeof list - used, "%s%s", i ? ", " : "", m->calls[i]);
        }
        log_warn("program calls: %s. Called programs must already be stored in the "
                 "control's memory - they cannot be fed from here", list);
    }

    if (m->n_progs + (size_t)m->extra_progs > 1) {
        log_warn("file contains %zu program number(s) (O%ld at line %ld, then O%ld at line %ld ...). "
                 "Only one program streams as the running program; subprograms later in the "
                 "file will not be available to M98/G65 unless loaded into memory separately",
                 m->n_progs + (size_t)m->extra_progs, m->prog_no[0], m->prog_line[0],
                 m->prog_no[1], m->prog_line[1]);
    }

    if (m->lost_msg_blocks)
        log_warn("%ld block(s) have message text in () (#3006 / MSG, first at line %ld) which "
                 "comment stripping removes - use --no-strip-comments to keep it",
                 m->lost_msg_blocks, m->first_lost_msg_line);
}
