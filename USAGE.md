# Using dripfeed

Practical recipes for everyday jobs. *This  file is meant to grow — add
cases as you meet them.* For every option, run `dripfeed --help`.

Contents
1. [Before you start](#1-before-you-start)
2. [Rehearse with a dry run](#2-rehearse-with-a-dry-run)
3. [Send a program](#3-send-a-program)
4. [Pause, resume, abort — and resume after a stop](#4-pause-resume-abort--and-resume-after-a-stop)
5. [Adjust a built-in profile from the command line](#5-adjust-a-built-in-profile-from-the-command-line)
6. [Define your own machine in a config file](#6-define-your-own-machine-in-a-config-file)
7. [Handshaking: acknowledge bytes and grbl `ok`](#7-handshaking-acknowledge-bytes-and-grbl-ok)
8. [Slow controls: pacing](#8-slow-controls-pacing)
9. [Verbose output and troubleshooting](#9-verbose-output-and-troubleshooting)
10. [Scripting and the Raspberry Pi](#10-scripting-and-the-raspberry-pi)
11. [Exit codes](#11-exit-codes)
12. [To be expanded](#12-to-be-expanded)

---

## 1. Before you start

1. **Cable  and  levels.**   A  Pi's  own  UART  is  3.3   V  logic,  not
   RS-232.  Use a  USB-RS232  adapter  or a  MAX3232  level shifter.  See
   [docs/WIRING.md](docs/WIRING.md).

2. **Permissions.** `sudo usermod -aG dialout $USER`, then log in again.

3. **Match the  control.** Baud,  data bits, parity,  stop bits  and flow
   control  must be  the same  on both  ends. Pick  the nearest  built-in
   profile (`--list-profiles`),  then check it against  the control's own
   parameters ([docs/PROFILES.md](docs/PROFILES.md)).

4. **Put the control in its receive / DNC mode** before starting the feed
   (the name varies — check the manual).

## 2. Rehearse with a dry run

`-n` /  `--dry-run` never opens the  serial port. It validates  the file,
prints every block  that *would* be sent (to stdout),  and shows size and
time estimates (to stderr). No device is needed.

```sh
dripfeed -n -p fanuc-0i part.nc
dripfeed -n -v -p fanuc-0i --strip-comments part.nc    # also show skipped lines
dripfeed -n -p haas part.nc > what-would-be-sent.txt   # save the report
```

Example output:

```
[dry-run] (added)     -> "%" <LF>
[dry-run] line 2     -> "G0 X0 Z1.0" <LF>
[dry-run] line 4     -> "G1 X-1.0 F0.005" <LF>
[dry-run] (added)     -> "%" <LF>
```

Use it to check line endings,  `%` framing, comment handling, and whether
any line is rejected — *before* you're standing at the machine.

## 3. Send a program

```sh
dripfeed -p haas -d /dev/ttyUSB0 part.nc
```

You  will  see  a  summary   (file,  blocks,  profile,  serial  settings,
estimate), then a  progress line. The device can also  come from a config
file (`device =`).

Notes on what dripfeed does to each line:

- removes the file's own line ending and adds the profile's (`CRLF`, `LF`, `CR`)
- trims whitespace; turns tabs into spaces
- optionally strips comments, drops blank lines, forces upper case
- adds `%` start/end lines if the profile wants them *and* the file lacks them
- **refuses** (before sending anything) lines with non-ASCII bytes, an
  unterminated `(` comment, or blocks longer than the profile's limit

> `;` is treated as a comment start only when comment stripping is on. On
> some controls  (e.g. Fanuc) `;`  is the end-of-block character,  so the
> Fanuc profiles leave comment stripping **off**.

## 4. Pause, resume, abort — and resume after a stop

| Action | Keyboard | Signal |
|--------|----------|--------|
| Pause | `p` or space (toggles) | `kill -USR1 <pid>` |
| Resume | `p` or space | `kill -USR2 <pid>` |
| Abort | `q` | `Ctrl-C`, `kill <pid>` |

**Pause stops  new lines only.** The  machine continues with what  it has
buffered. Use the machine's feed hold for an immediate stop.

When a transfer stops (abort,  timeout, machine error) dripfeed tells you
where:

```
Stopped before confirming line 120. Resume with: --start-line 120
```

```sh
dripfeed -p fanuc-0i -d /dev/ttyUSB0 --start-line 120 part.nc
```

Resuming  mid-program means  the  control's modal  state (tool,  offsets,
spindle, feed,  coordinate mode) is  whatever it was —  re-establish it
first. No leading  `%` is sent on  a resume. If the message  says a block
"may have been  partially transmitted", reset the program  on the machine
before resuming.

## 5. Adjust a built-in profile from the command line

Any option overrides the profile for this run only:

```sh
dripfeed -p fanuc-0i -b 4800 -d /dev/ttyUSB0 part.nc     # different baud
dripfeed -p haas -f rtscts -d /dev/ttyUSB0 part.nc       # hardware handshake
dripfeed -p generic --databits 7 --parity E --stopbits 2 -f xonxoff \
         --eol lf --percent --upper --strip-comments -d /dev/ttyUSB0 part.nc
dripfeed -p haas --show-profile -b 19200                 # check what you'd get
```

## 6. Define your own machine in a config file

Config files  are searched in  this order (later wins;  command-line wins
over all):

1. `/etc/dripfeed.conf`
2. `~/.config/dripfeed/dripfeed.conf`
3. the file given with `-C FILE`

Minimal example (`~/.config/dripfeed/dripfeed.conf`):

```ini
[dripfeed]
device  = /dev/serial0      # default port
profile = shop-lathe        # default profile

[shop-lathe]
base        = fanuc-0i      # inherit, then change what differs
description = Shop lathe, Fanuc 0i-TD
baud        = 4800
strip_comments = yes
```

```sh
dripfeed part.nc            # uses device and profile from the file
dripfeed -p shop-lathe -b 9600 part.nc
dripfeed --list-profiles    # shows shop-lathe alongside the built-ins
```

Every            setting            is           documented            in
[docs/dripfeed.conf.example](docs/dripfeed.conf.example).  Mistakes in  a
config file  are reported with file  and line number; typos  in key names
are errors, not silently ignored.

## 7. Handshaking: acknowledge bytes and grbl `ok`

Classic controls need only flow control. Some devices answer each line:

```sh
dripfeed -a char:3e -T 10000 -d /dev/ttyUSB0 part.nc   # wait for '>' (0x3e)
dripfeed -p grbl -d /dev/ttyUSB0 part.nc               # wait for "ok"
```

- `-a char:HEX` — one specific byte, e.g. `char:06` for ACK.
- `-a ok` — a line starting `ok`. A line starting `error` or `ALARM` stops the
  transfer with exit code 4 and the line number to resume from.
- `-T` is how long to wait before giving up (ms).

grbl-style  streaming  is  send-and-wait   (one  line  in  flight).  That
is  safe  and   simple;  throughput  on  tiny  segments   is  lower  than
grbl's   character-counting   protocol   would   allow.   (See   [to   be
expanded](#12-to-be-expanded).)

## 8. Slow controls: pacing

If a control drops characters even with flow control enabled:

```sh
dripfeed -l 200 ...      # 200 ms pause after every line
dripfeed -t 5000 ...     # 5 ms pause after every character (very old controls)
```

Lowering the baud rate (`-b 2400`) is usually the better first fix.

## 9. Verbose output and troubleshooting

```sh
dripfeed -v  ...     # per-line detail, profile dump, machine replies
dripfeed -vv ...     # plus byte-level debugging
```

| Symptom | Things to check |
|---------|-----------------|
| Nothing happens / sits at the first line | Control not in receive mode; wrong flow control; cable (null-modem vs straight); try `-f none` briefly with a *tiny* program to see bytes arrive |
| Garbage characters at the machine | Baud/data bits/parity/stop bits mismatch |
| Control alarms on the first block | Missing/extra `%`; wrong line ending (`--eol`); lower case rejected (`--upper`) |
| Machine "stalls" mid-program | Buffer starvation: increase baud or enable hardware flow control; avoid per-line delays |
| `cannot open /dev/ttyUSB0` | Group `dialout`; another program (ModemManager, a terminal) holds the port |
| `timed out waiting for the machine to acknowledge` | Wrong ack mode/byte; machine busy; raise `-T` |

## 10. Scripting and the Raspberry Pi

dripfeed is  a plain  command, so anything  on the Pi  — a  Python GPIO
script, a button daemon, a web page — can start it, and control it with
signals.

```sh
# start in the background, remember the pid
dripfeed -p shop-lathe part.nc &
PID=$!

kill -USR1 $PID      # e.g. door opened -> pause
kill -USR2 $PID      # door closed + cycle start -> resume
kill -TERM $PID      # e.g. interlock tripped -> abort
wait $PID; echo "exit status $?"
```

For   tighter    integration   (buttons    and   LEDs    handled   inside
the    program),   see    the    `hw_*`   hooks    in   `src/hw.c`    and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## 11. Exit codes

| Code | Meaning |
|------|---------|
| 0 | Whole program delivered |
| 1 | Error: I/O failure, unreadable file, problem lines found, ack timeout |
| 2 | Usage / configuration error |
| 3 | Aborted by operator (key, signal, hardware input) |
| 4 | Machine reported an error/alarm line |

"Delivered" means the control has *accepted* every block into its buffer,
not that the part has finished machining.

## 12. To be expanded

Ideas and open items — add recipes here as they are proven:

- Real-hardware recipes per machine (confirmed cable + parameter sets)
- GPIO wiring recipes: cycle-start/feed-hold buttons, door interlock, status LEDs
- grbl character-counting streaming for higher throughput
- Running as a `systemd` service, triggered from a USB stick or web UI
- Logging a transfer (timestamped record of what was sent)
- Header handling for Siemens-style `%_N_…_MPF` programs
