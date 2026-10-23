/*
 * control.h - operator control: signals, keyboard, and hardware events.
 *
 * Funnels every way of saying "pause", "resume" or "abort" into two flags the
 * feeder checks between lines:
 *
 *   keyboard (when stdin is a terminal)   p / space = toggle pause,  q = abort
 *   SIGUSR1                               pause   (idempotent)
 *   SIGUSR2                               resume  (idempotent)
 *   SIGINT / SIGTERM / SIGHUP             abort
 *   hardware inputs (hw.h)                pause / resume / abort
 *
 * NOTE: pausing stops *new lines* being sent. The machine keeps executing
 * whatever is already in its own input buffer, which may be several blocks.
 * It is not a substitute for the machine's own feed hold or e-stop.
 */
#ifndef DRIPFEED_CONTROL_H
#define DRIPFEED_CONTROL_H

/* Install signal handlers, put the keyboard in raw mode, init hardware. */
void control_init(int interactive_keys);

/* Undo control_init (restores the terminal). Safe to call twice. */
void control_shutdown(void);

/* Sample keyboard and hardware. Call between lines and while paused. */
void control_poll(void);

int  control_abort_requested(void);
int  control_paused(void);
void control_set_paused(int paused);
void control_request_abort(void);

#endif /* DRIPFEED_CONTROL_H */
