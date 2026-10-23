# Architecture

## Layout

```
inc/common.h    version string, exit codes
inc/log.h       leveled logger → stderr                      src/log.c
inc/profile.h   profile type, built-in table, field parser    src/profile.c
inc/config.h    INI config file loader                        src/config.c
inc/cli.h       getopt_long front end                         src/cli.c
inc/gcode.h     per-line preparation and validation           src/gcode.c
inc/serial.h    termios layer                                 src/serial.c
inc/control.h   pause/resume/abort (keys, signals, hw)        src/control.c
inc/hw.h        GPIO/LED hook points (stub)                   src/hw.c
inc/feeder.h    scan + send engine                            src/feeder.c
                entry point and wiring                        src/main.c
```

## Data flow

```
 argv ──► cli ─┐
 *.conf ─► config ─► profile registry ─► resolved profile_t (+ CLI overrides)
                                              │
 G-code file ──► feeder_scan  ◄───────────────┤   pre-flight: validate everything
                    │                         │
                    ▼                         ▼
              feeder_run ── gcode_prepare ── serial_write / serial_wait_sent
                    │              (or printf for --dry-run)
                    ├── control_poll ◄── keyboard / signals / hw_poll()
                    └── rx_pump ◄── serial_read   (ack bytes, "ok"/"error")
```

## Key design decisions

- **One parser for settings.** `profile_set_field()` is used by config files
  *and* command-line options, so a setting means the same everywhere.
  Precedence: built-in < config file < command line.
- **Validate before sending.** `feeder_scan()` prepares every line exactly as
  `feeder_run()` will and refuses the whole job if any line is unusable. A
  failure in the middle of a cut is the expensive kind.
- **Genuine drip.** After each block we wait for it to leave the UART
  (`serial_wait_sent`). A machine holding XOFF/CTS therefore stalls the
  feeder at most one block ahead, instead of the kernel's ~4 KB tty buffer
  swallowing the program and making pause/abort meaningless.
- **Abort flushes the TX queue** so bytes held back by flow control are never
  delivered later; a block that may have been partly sent triggers an explicit
  warning.
- **Signal handlers only set flags.** All real work happens in the main loop.
- **Dry run shares the real code path** (scan, prepare, framing, pause
  checkpoints); only the final "write to port" is replaced by a printout.

## Adding a built-in profile

Add one row to `BUILTINS[]` in `src/profile.c`, run `make test`, and record the
source in `docs/PROFILES.md`.

## Adding a setting

1. Add a field to `profile_t` (`inc/profile.h`) and a default in
   `profile_defaults()`.
2. Parse it in `profile_set_field()` (this alone makes it available in config
   files).
3. If it deserves a command-line flag, add the option in `src/cli.c` mapping to
   `add_override("your_key", …)`.
4. Use it in `gcode.c` / `feeder.c` / `serial.c`; show it in `profile_print()`.

## Integrating Raspberry Pi GPIO

Everything hardware-specific is behind `inc/hw.h`; the shipped `src/hw.c` is a
no-op so the program builds anywhere. To wire it up:

1. Link a GPIO library (libgpiod is the current Linux interface; `sysfs` GPIO
   is deprecated). Add `LDLIBS += -lgpiod` to the Makefile.
2. `hw_init()`: open the chip, request input lines (feed-hold, cycle-start,
   door, e-stop mirror) with pull-ups/edge detection, and output lines (LEDs).
3. `hw_poll()`: non-blocking read of the inputs, **debounced** (require 20–50 ms
   of stable level), mapped to `HW_EVT_PAUSE`, `HW_EVT_RESUME`, `HW_EVT_ABORT`.
   It is called between lines and ~20×/s while paused.
4. `hw_set_state()`: drive indicators for `RUNNING`, `PAUSED`, `DONE`, `ERROR`.

Treat an interlock input as **fail-safe** (open circuit = abort/pause). Remember
that pausing the feeder does not stop a machine that already has blocks
buffered — a real safety function must act on the machine's own safety circuit.

Alternative with no C changes: run a small Python/shell GPIO script that starts
dripfeed and sends it `SIGUSR1` / `SIGUSR2` / `SIGTERM` (see
[USAGE.md](../USAGE.md#10-scripting-and-the-raspberry-pi)).

## Testing

`make test` runs `tests/run_tests.py`, which uses a pseudo-terminal as a fake
machine (including ack/`ok`/`error` replies) and checks the exact bytes on the
wire, exit codes, pause/resume/abort by signal, config handling and dry-run
output. `make DEBUG=1` builds with AddressSanitizer + UBSan.
