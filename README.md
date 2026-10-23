# dripfeed

A small, dependency-free **G-code drip  feeder** written in C. It streams
a  G-code program  line by  line over  a serial  (RS-232) link  to a  CNC
control that can't hold the whole program in memory — and it's built to
run  on a  **Raspberry  Pi** that  can also  drive  buttons, sensors  and
indicator LEDs.

Current target: a CNC lathe. Designed to grow to other controls.

> **Status:  early (v0.2).**  The engine  is tested  against a  simulated
> machine  (pseudo-terminal), but  **not yet  proven on  real hardware**.
> Built-in  controller  settings  are  researched  starting  points,  not
> guarantees — see  [docs/PROFILES.md](docs/PROFILES.md). Prove out any
> new setup with the chuck empty and feed override low.

## Features

- **Drip feeding with real flow control** — XON/XOFF, RTS/CTS, or both.
  Blocks are  handed to the UART  one at a  time, so a machine  that says
  "stop" really stops the stream at the next line.

- **Machine profiles**  for common  controls (Fanuc, Haas,  Mazak, Okuma,
  Siemens,  grbl)  plus   your  own,  via  config   files  —  `dripfeed
  --list-profiles`.

- **Dry-run  / demo  mode**  (`-n`)  — validates  the  file and  prints
  exactly what  would be  sent, with timing  estimate, without  opening a
  serial port.

- **Pre-flight validation**  — the whole  file is checked  *before* the
  first  byte  is  sent:  non-ASCII  characters,  unterminated  comments,
  over-long blocks.

- **Optional per-line acknowledge** — wait  for a byte (`char:3e`) or a
  grbl-style `ok`; stop on `error`/`ALARM`.

- **Pause  /  resume /  abort**  from  the  keyboard, signals,  or  (hook
  provided) GPIO  buttons and interlocks;  **resume from any  line** with
  `--start-line`.

- Clean, commented, multi-file C11; no libraries beyond POSIX.

## Build and install

```sh
make              # builds bin/dripfeed
make test         # runs the test suite (needs python3)
sudo make install # installs to /usr/local/bin
make clean
make archive      # zips the repository to dripfeed-<version>.zip
```

Requires a C compiler and `make`. Tested with GCC on Linux. Serial access
normally needs your user in the `dialout` group.

## Quick start

```sh
dripfeed --list-profiles                    # what machines are built in?
dripfeed -n -p haas part.nc                 # rehearse: nothing is sent
dripfeed -p haas -d /dev/ttyUSB0 part.nc    # do it for real
```

While  running: **`p`**/space  pauses or  resumes, **`q`**  aborts. If  a
transfer stops, dripfeed prints the exact `--start-line` to resume from.

See   **[USAGE.md](USAGE.md)**  for   walkthroughs,   config  files   and
scripting.

## Documentation

| File | Contents |
|------|----------|
| [USAGE.md](USAGE.md) | Everyday use cases and recipes (growing) |
| [docs/PROFILES.md](docs/PROFILES.md) | Built-in machine settings, sources, confidence |
| [docs/WIRING.md](docs/WIRING.md) | Raspberry Pi serial hardware and cabling |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Code layout, GPIO integration |
| [docs/dripfeed.conf.example](docs/dripfeed.conf.example) | Annotated config file |

## Repository layout

```
src/    C sources            inc/    headers
docs/   documentation        tests/  test suite (pty-based fake machine)
bin/    built binary         Makefile, README.md, USAGE.md
```

## Safety

Drip feeding runs a machine from  a program it hasn't fully seen. Pausing
the feeder only  stops *new* lines; the control  keeps executing whatever
is already  in its buffer. **dripfeed  is not a safety  device** — keep
the machine's own  feed hold and e-stop within reach,  and never use this
tool's pause/abort as an emergency stop.

## License

BSD 3-Clause License, see `LICENSE` in project root.
