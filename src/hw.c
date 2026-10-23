/*
 * hw.c - hardware integration stub (see hw.h).
 *
 * Replace these bodies to connect real inputs/outputs. A sketch using
 * libgpiod (v2) is in docs/ARCHITECTURE.md. Keep everything non-blocking.
 */
#include "hw.h"

int hw_init(void)
{
    /* TODO: open the GPIO chip, request input lines (buttons, interlocks)
     * and output lines (LEDs). Return -1 on failure. */
    return 0;
}

void hw_shutdown(void)
{
    /* TODO: release GPIO lines; leave outputs in a safe state. */
}

hw_event_t hw_poll(void)
{
    /* TODO: read inputs, debounce (e.g. require 20-50 ms stable), and map to
     * events. Example mapping:
     *   feed-hold button pressed   -> HW_EVT_PAUSE
     *   cycle-start button pressed -> HW_EVT_RESUME
     *   e-stop mirror input active -> HW_EVT_ABORT
     */
    return HW_EVT_NONE;
}

void hw_set_state(hw_state_t state)
{
    /* TODO: e.g. green LED while RUNNING, amber while PAUSED, red on ERROR. */
    (void)state;
}
