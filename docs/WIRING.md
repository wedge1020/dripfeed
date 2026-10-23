# Serial hardware and cabling

## Getting RS-232 out of a Raspberry Pi

The Pi's UART pins (GPIO14 TXD / GPIO15 RXD) are **3.3 V logic**, not RS-232
(which swings roughly ±5…±15 V). Connecting them straight to a machine can
damage the Pi. Choose one:

1. **USB-to-RS-232 adapter** (FTDI- or Prolific-type with real RS-232 levels,
   not a USB-TTL cable). Simplest and most robust; appears as `/dev/ttyUSB0`.
   Industrial adapters with isolation are worth it near a machine.
2. **MAX3232 (or similar) level-shifter board** on the Pi's UART. Use the
   `/dev/serial0` alias. Disable the login console on the serial port
   (`raspi-config` → Interface Options → Serial Port: login shell *No*,
   hardware serial *Yes*). Supports RTS/CTS only if you also wire those pins
   and enable them in `config.txt`.
3. **Isolated RS-232 HAT** — same idea, in one board.

Add your user to `dialout`: `sudo usermod -aG dialout $USER` (log in again).

## Cable

Wiring depends on the control: confirm with its manual. Typical DB-25 RS-232
signals on the machine side:

| DB-25 pin | Signal |
|:---------:|--------|
| 2 | TXD (from machine) / RXD at the machine depending on DTE/DCE |
| 3 | RXD |
| 4 | RTS |
| 5 | CTS |
| 6 | DSR |
| 7 | Signal ground |
| 20 | DTR |

- Most setups use a **null-modem** arrangement (TX↔RX crossed). Controls vary
  between DTE and DCE; a wrong choice shows up as "no data either way".
- **XON/XOFF** needs only TX, RX and ground (3 wires).
- **RTS/CTS** needs RTS and CTS wired as well; some controls also want DTR/DSR
  tied to their partner or looped back. Check the control's manual.
- Very old controls may use a **20 mA current loop** rather than RS-232 and
  need a converter.
- Keep serial cables away from servo/motor cabling; use shielded cable,
  shield grounded at one end.

## Bench test without a machine

- Loopback: jumper TXD to RXD on the adapter, run a tiny program with
  `-f none` and watch the bytes with `-vv`.
- Two adapters back to back with a null-modem cable and a terminal program on
  the second to see what the control would receive.
- No hardware at all: `dripfeed -n ...` (dry run).
- The project's test suite uses a pseudo-terminal as a fake machine
  (`make test`).
