/*
 * control.c - pause/resume/abort plumbing (see control.h).
 */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "control.h"
#include "hw.h"

/* Flags written from signal handlers, so sig_atomic_t. */
static volatile sig_atomic_t g_abort;
static volatile sig_atomic_t g_pause;

static struct termios g_saved_tty;
static int            g_tty_raw;
static int            g_hw_ok;

/* ---- signal handlers: only set flags, nothing else is async-signal-safe -- */
static void on_abort(int sig)  { (void)sig; g_abort = 1; }
static void on_pause(int sig)  { (void)sig; g_pause = 1; }
static void on_resume(int sig) { (void)sig; g_pause = 0; }

void control_init(int interactive_keys)
{
    /* No SA_RESTART on purpose: we want blocking calls (poll, tcdrain) to
     * return EINTR promptly so an abort is noticed. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = on_abort;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sa.sa_handler = on_pause;
    sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = on_resume;
    sigaction(SIGUSR2, &sa, NULL);

    /* Keyboard: single keypresses without Enter, only if it's a terminal. */
    if (interactive_keys && isatty(STDIN_FILENO) &&
        tcgetattr(STDIN_FILENO, &g_saved_tty) == 0) {
        struct termios t = g_saved_tty;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN]  = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        g_tty_raw = 1;
    }

    g_hw_ok = (hw_init() == 0);
}

void control_shutdown(void)
{
    if (g_tty_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tty);
        g_tty_raw = 0;
    }
    if (g_hw_ok) {
        hw_shutdown();
        g_hw_ok = 0;
    }
}

void control_poll(void)
{
    if (g_tty_raw) {
        char ch;
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (ch == 'p' || ch == 'P' || ch == ' ') g_pause = !g_pause;
            else if (ch == 'q' || ch == 'Q')         g_abort = 1;
        }
    }
    if (g_hw_ok) {
        switch (hw_poll()) {
        case HW_EVT_PAUSE:  g_pause = 1; break;
        case HW_EVT_RESUME: g_pause = 0; break;
        case HW_EVT_ABORT:  g_abort = 1; break;
        case HW_EVT_NONE:   break;
        }
    }
}

int  control_abort_requested(void) { return g_abort; }
int  control_paused(void)          { return g_pause; }
void control_set_paused(int p)     { g_pause = p ? 1 : 0; }
void control_request_abort(void)   { g_abort = 1; }
