/*
 * gcode.h - turning one raw line of a G-code file into the block we transmit.
 *
 * Deliberately conservative: it never *changes* machine-meaningful content.
 * It only removes things the profile says to remove (comments, blank lines,
 * surrounding whitespace), upper-cases if asked, and REFUSES (rather than
 * guesses) when a line contains something that could be misread by the
 * control, such as a non-ASCII byte or an unterminated comment.
 */
#ifndef DRIPFEED_GCODE_H
#define DRIPFEED_GCODE_H

#include <stddef.h>

#include "profile.h"

typedef struct {
    size_t      len;      /* length of the prepared text in the line buffer   */
    int         skip;     /* 1 = don't transmit this line                     */
    const char *skip_why; /* "blank" / "comment only" (for verbose output)    */
    const char *error;    /* non-NULL = line is unusable; text says why       */
} gcode_line_t;

/*
 * Prepare `line` in place (NUL-terminated; any trailing CR/LF is removed).
 * The prepared text is left in `line`; details are returned in *out.
 */
void gcode_prepare(char *line, const profile_t *p, gcode_line_t *out);

/* Is this prepared line exactly the program-delimiter "%"? */
int gcode_is_percent(const char *prepared, size_t len);

#endif /* DRIPFEED_GCODE_H */
