/*
 * gcode.c - per-line G-code preparation (see gcode.h).
 */
#include <string.h>

#include "gcode.h"

int gcode_is_percent(const char *prepared, size_t len)
{
    return len == 1 && prepared[0] == '%';
}

void gcode_prepare(char *line, const profile_t *p, gcode_line_t *out)
{
    out->len = 0; out->skip = 0; out->skip_why = NULL; out->error = NULL;

    size_t w = 0;           /* write index (we compact in place)           */
    int    depth = 0;       /* inside a (parenthesised) comment?           */
    int    had_comment = 0; /* did we remove any comment text?             */

    for (size_t r = 0; line[r]; r++) {
        unsigned char ch = (unsigned char)line[r];

        /* The file's own line ending: stop here. Handles LF, CRLF and CR. */
        if (ch == '\r' || ch == '\n') break;

        if (p->strip_comments) {
            if (ch == '(') { depth = 1; had_comment = 1; continue; }
            if (depth) {
                if (ch == ')') depth = 0;
                continue;                       /* swallow comment text */
            }
            if (ch == ';') { had_comment = 1; break; }   /* rest of line */
        }

        /* Tabs become spaces; anything else outside printable ASCII is
         * refused. A stray UTF-8 byte (degree sign, smart quote, ...) would be
         * corrupted on a 7-bit link or choke the control. */
        if (ch == '\t') ch = ' ';
        if (ch < 0x20 || ch > 0x7e) {
            out->error = "contains a non-ASCII or control character "
                         "(strip comments, or fix the file)";
            return;
        }

        if (p->uppercase && ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
        line[w++] = (char)ch;
    }

    if (depth) {            /* "(" never closed: we'd be dropping real code */
        out->error = "unterminated '(' comment";
        return;
    }

    /* Trim surrounding whitespace. */
    while (w && line[w - 1] == ' ') w--;
    size_t st = 0;
    while (st < w && line[st] == ' ') st++;
    if (st) memmove(line, line + st, w - st);
    w -= st;
    line[w] = '\0';
    out->len = w;

    if (w == 0 && p->strip_blank) {
        out->skip = 1;
        out->skip_why = had_comment ? "comment only" : "blank";
    }
}
