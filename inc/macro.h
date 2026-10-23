/*
 * macro.h - advisory scan for Fanuc Macro B constructs.
 *
 * IMPORTANT: this module never alters what is sent. Macro B syntax
 * (#100=#101+1, IF [#1 EQ 0] GOTO 10, WHILE [..] DO1, G65 P9001 ...) is plain
 * printable ASCII and dripfeed passes it through verbatim. What this module
 * does is *warn*, before anything is sent, about constructs whose behaviour
 * depends on the control being able to see more of the program than a
 * drip-feed stream gives it:
 *
 *   - backward GOTO / a GOTO whose target label doesn't exist
 *   - computed GOTO (GOTO #n), which can't be checked in advance
 *   - WHILE..DO loops (they re-read earlier blocks)
 *   - calls to other programs (G65 / G66 / M98 / M198 P....) - those programs
 *     must already be in the control's memory; they can't be fed from here
 *   - more than one O-number in the file (only the main program streams)
 *   - comment stripping on a program whose #3006 / (MSG..) text would be lost
 *
 * These are cautions, not errors: exactly what a control accepts differs by
 * model and option set. The exit code and the bytes sent are unaffected.
 */
#ifndef DRIPFEED_MACRO_H
#define DRIPFEED_MACRO_H

#include "profile.h"

typedef struct { long label; long line; } macro_label_t;
typedef struct { long target; long line; int computed; } macro_goto_t;

typedef struct {
    macro_label_t *labels;   size_t n_labels,  cap_labels;
    macro_goto_t  *gotos;    size_t n_gotos,   cap_gotos;
    long   while_count, first_while_line;
    long   macro_blocks;                 /* blocks using #, IF, WHILE, GOTO...   */
    long   lost_msg_blocks;              /* #3006/(MSG) text removed by stripping */
    long   first_lost_msg_line;
    char   calls[16][32];  size_t n_calls;      /* "G65 P9001" (unique)          */
    long   call_first_line[16];
    long   prog_no[8];     long prog_line[8];  size_t n_progs;  /* O-numbers      */
    long   extra_progs;                  /* O-numbers beyond what we record      */
} macro_ctx_t;

void macro_init(macro_ctx_t *m);
void macro_free(macro_ctx_t *m);

/*
 * Examine one block. `prepared` is the text that will be sent; `raw` is the
 * file's original line (used only to notice comment text that stripping
 * removed). Call for every non-blank block, in file order.
 */
void macro_scan_block(macro_ctx_t *m, const char *prepared, const char *raw,
                      long lineno, const profile_t *p);

/* Log the findings (warnings on stderr). Call once after the last block. */
void macro_report(const macro_ctx_t *m, const profile_t *p);

#endif /* DRIPFEED_MACRO_H */
