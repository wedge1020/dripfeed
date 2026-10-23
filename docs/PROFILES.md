# Built-in machine profiles

These values are **starting points**. They describe the *PC/host side* of the
link and were compiled from public DNC-software setup guides and vendor
pages (October 2026). Controls are configurable, machine builders change
defaults, and options differ between model years. Always confirm against the
parameters shown on your control. Anything here can be overridden — see
[../USAGE.md](../USAGE.md#6-define-your-own-machine-in-a-config-file).

Show any profile with `dripfeed -p NAME --show-profile`.

| Profile | Baud | Frame | Flow | Line end | `%` | Confidence |
|---------|-----:|:-----:|------|:--------:|:---:|:----------:|
| `generic` | 9600 | 8N1 | XON/XOFF | CRLF | no | n/a (neutral) |
| `fanuc-0i` | 9600 | 7E2 | RTS/CTS | LF | yes | medium |
| `fanuc-0m` | 2400 | 7E2 | RTS/CTS | LF | yes | medium |
| `fanuc-10-15` | 4800 | 7E2 | RTS/CTS | LF | yes | medium |
| `haas` | 9600 | 8N1 | XON/XOFF | CRLF | yes | medium |
| `mazak-m32` | 4800 | 7E2 | RTS/CTS | CRLF | yes | medium |
| `mazak-t32` | 4800 | 7E2 | both | CRLF | no | medium |
| `okuma-osp5000` | 4800 | 7E1 | both | LF | yes | medium |
| `okuma-ospu100` | 9600 | 7E2 | RTS/CTS | CRLF | yes | medium |
| `siemens-840d` | 9600 | 8N1 | XON/XOFF | CRLF | no | **low** |
| `grbl` | 115200 | 8N1 | none | LF | no | medium (see note) |

"Frame" = data bits, parity, stop bits. "both" = XON/XOFF and RTS/CTS.
Fanuc, Mazak and Okuma profiles also upper-case the program (ISO/EIA controls
generally expect it); the others don't.

## Per-controller notes

### Fanuc
- Source: DNC setup guide listing PC-side settings (7 data bits, even parity,
  2 stop bits, RTS/CTS handshake, XON = DC1/`0x11`, XOFF = DC3/`0x13`,
  `%` start/end, LF line end). Baud differs by series: 0i 9600, 0-M/T 2400,
  10/11/12/15 4800.
- Two-wire cabling with **XON/XOFF** only is also very common: use `-f xonxoff`.
- The control side is set by parameters (e.g. on 0i the I/O channel and
  baud parameters around `0020`, `0100`–`0103`). The baud *code* in the
  parameter is not the baud rate — check the manual's table. The guide notes
  a case where the PC is 9600 while parameter `0103 = 9` is shown for a
  different rate; the two ends must agree.
- Put the control in **TAPE/RMT (remote)** mode for drip feed (older models).
- Comment stripping is **off**: `;` is the end-of-block character on Fanuc.

### Haas
- Control settings: **11** baud, **12** parity, **13** stop bits,
  **14** synchronisation, **37** data bits. Commonly documented values:
  9600 / none / 1 / XON-XOFF / 8, with end-of-block pattern **CR LF**
  (setting 25).
- Drip-feeding on Haas is done in **DNC mode** (MDI/DNC); see the Haas
  operator manual for your control generation. Newer controls (NGC) may offer
  Ethernet/USB alternatives.
- Note: one web article reviewed while compiling this listed different
  setting numbers (86–91); those did not match the Haas manual numbering above
  or a second independent source, so they were not used.

### Mazak
- Two profiles cover the most-documented families. The T-series entry sends no
  `%` framing, per the setup guide. Mazak also exposes controller-specific
  quirks (e.g. program-name prefix `O` or `:`, and machine-side data bits
  differing from the host setting on some models). **Verify** on your model.

### Okuma
- OSP 5000-series: 4800 7E1, both handshakes, LF. OSP-U100: 9600 7E2, RTS/CTS,
  CRLF. Different OSP generations differ — verify.

### Siemens 840D / 802D — *low confidence*
- Only one source reviewed: 8N1, 9600 or 19200 baud, no flow or XON/XOFF.
  Siemens programs normally start with a `%_N_NAME_MPF` header block; dripfeed
  passes such lines through unchanged and does not generate them.

### grbl
- 115200 8N1, no flow control, `ok` / `error:N` responses, and a short line
  buffer (profile limit 80 characters; comments are stripped before the check).
  These come from general knowledge of grbl 1.x and were **not re-verified**
  against the grbl wiki during this session — check your firmware version.
  Other firmwares (Marlin, FluidNC, etc.) differ; start from `grbl` and
  override.

## Sources

- AggSoft DNC setup guides — [Fanuc](https://www.aggsoft.com/cnc-dnc/fanuc.htm),
  [Mazak](https://aggsoft.com/cnc-dnc/mazak.htm),
  [Okuma OSP 5000](https://aggsoft.com/cnc-dnc/okuma-okuma-osp-5000.htm)
- Chatter, [Haas Classic integration](https://support.chatter.dev/docs/integration/haas-classic)
  (Haas settings 11/12/13/14/37)
- Industrial Monitor Direct, [Siemens 840D RS-232 program transfer](https://industrialmonitordirect.com/blogs/knowledgebase/siemens-840d-rs-232-program-transfer-easydnc-setup)
  and [Haas RS-232 drip feeding](https://industrialmonitordirect.com/blogs/knowledgebase/haas-cnc-rs232-drip-feeding-solutions-and-configuration)
  (used for Haas buffer/handshake advice only)

## Adding or correcting a profile

Built-ins live in `src/profile.c` (one table row each). Prefer adding a profile
to a config file first; once a setup is proven on real hardware, promote it to
the table and record the evidence here.
