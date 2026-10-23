/*
 * hw.h - hardware integration hooks (Raspberry Pi GPIO, sensors, LEDs).
 *
 * This is the seam between the portable feeder and the physical world. The
 * shipped hw.c is a deliberate no-op stub so the program builds and runs
 * anywhere. To wire real hardware, replace the bodies in src/hw.c (e.g. using
 * libgpiod) - nothing else in the program needs to change.
 *
 * Typical uses:
 *   - feed-hold / cycle-start push buttons       -> hw_poll() returns events
 *   - door interlock or e-stop mirror input      -> HW_EVT_ABORT
 *   - coolant-level / temperature sensor gating  -> HW_EVT_PAUSE until OK
 *   - status LEDs / beacon                       -> hw_set_state()
 *
 * Keep hw_poll() fast and non-blocking: it is called between lines and while
 * paused, several times per second.
 */
#ifndef DRIPFEED_HW_H
#define DRIPFEED_HW_H

/* Events the hardware can inject into the feeder. */
typedef enum {
    HW_EVT_NONE,
    HW_EVT_PAUSE,     /* stop sending new lines (idempotent)              */
    HW_EVT_RESUME,    /* continue sending (idempotent)                    */
    HW_EVT_ABORT      /* stop for good; the program can be resumed later  */
} hw_event_t;

/* What the feeder is doing, for indicator outputs. */
typedef enum {
    HW_STATE_IDLE,
    HW_STATE_RUNNING,
    HW_STATE_PAUSED,
    HW_STATE_DONE,
    HW_STATE_ERROR
} hw_state_t;

/* Set up GPIO etc. Returns 0 on success; failure is logged but not fatal. */
int  hw_init(void);
void hw_shutdown(void);

/* Non-blocking: return the next pending hardware event, or HW_EVT_NONE. */
hw_event_t hw_poll(void);

/* Drive indicators. Called on every state change. */
void hw_set_state(hw_state_t state);

#endif /* DRIPFEED_HW_H */
