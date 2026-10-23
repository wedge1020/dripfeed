#!/usr/bin/env python3
"""
Test suite for dripfeed.  Usage:  python3 -I tests/run_tests.py bin/dripfeed

No hardware needed: the "machine" is one end of a pseudo-terminal that this
script reads from (and answers on) while dripfeed writes to the other end.
"""
import os, pty, select, subprocess, sys, tempfile, time, tty, textwrap

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "bin/dripfeed")
FAILS = []

def check(name, cond, detail=""):
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f"   <- {detail}"))
    if not cond:
        FAILS.append(name)

def write_tmp(text, suffix=".nc"):
    f = tempfile.NamedTemporaryFile("w", suffix=suffix, delete=False)
    f.write(textwrap.dedent(text)); f.close()
    return f.name

def run(args, **kw):
    return subprocess.run([BIN] + args, capture_output=True, text=True, timeout=30, **kw)

class Machine:
    """A fake machine on a pty. reply(data_so_far) -> bytes to send back."""
    def __init__(self, reply=None):
        self.master, slave = pty.openpty()
        tty.setraw(slave)                       # raw, so no echo / CR mangling
        self.path = os.ttyname(slave)
        self.slave = slave                      # keep open so master doesn't see EIO
        self.reply = reply
        self.rx = b""

    def run(self, args, timeout=20):
        p = subprocess.Popen([BIN, "-d", self.path] + args,
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        end = time.time() + timeout
        seen_lines = 0
        while p.poll() is None and time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try: d = os.read(self.master, 4096)
                except OSError: break
                self.rx += d
                n = self.rx.count(b"\n")
                if self.reply:
                    for i in range(seen_lines, n):
                        out = self.reply(i)
                        if out: os.write(self.master, out)
                seen_lines = n
        if p.poll() is None:
            p.kill()
        out, err = p.communicate()
        # collect any trailing bytes
        while select.select([self.master], [], [], 0.1)[0]:
            try: self.rx += os.read(self.master, 4096)
            except OSError: break
        return p.returncode, out.decode(), err.decode()

# --------------------------------------------------------------------------
prog = write_tmp("""\
    (header comment)
    g0 x0 z1.0 ; rapid

    G1 X-1.0 F0.005 (cut)
    M30
    """)

# ---- CLI basics -----------------------------------------------------------
r = run(["--version"]);            check("--version", r.returncode == 0 and r.stdout.startswith("dripfeed "), r.stdout)
r = run(["--help"]);               check("--help", r.returncode == 0 and "Usage:" in r.stdout)
r = run(["--list-profiles"]);      check("--list-profiles lists haas+fanuc+grbl",
                                         all(n in r.stdout for n in ("haas", "fanuc-0i", "grbl")))
r = run(["-p", "haas", "--show-profile"])
check("--show-profile haas", "9600 baud, 8N1" in r.stdout and "xonxoff" in r.stdout, r.stdout)
r = run(["-p", "nope", prog, "-n"]);   check("unknown profile -> exit 2", r.returncode == 2, r.stderr)
r = run(["--baud", "123", "-n", prog]); check("bad baud -> exit 2", r.returncode == 2, r.stderr)
r = run(["-n", prog, "extra"]);    check("two files -> exit 2", r.returncode == 2)
r = run([prog]);                   check("no device without -n -> exit 2", r.returncode == 2)

# ---- dry run --------------------------------------------------------------
r = run(["-n", "-p", "fanuc-0i", "--strip-comments", prog])
check("dry-run exit 0", r.returncode == 0, r.stderr)
check("dry-run adds % framing", r.stdout.count('"%"') == 2, r.stdout)
check("dry-run uppercases + strips comments", '"G0 X0 Z1.0"' in r.stdout and '"G1 X-1.0 F0.005"' in r.stdout, r.stdout)
check("dry-run skips blank/comment-only", "header" not in r.stdout, r.stdout)
check("dry-run announces itself", "DRY RUN" in r.stderr)
r = run(["-n", "-v", "-p", "fanuc-0i", "--strip-comments", prog])
check("dry-run -v shows skip reasons", "skipped (comment only)" in r.stderr and "skipped (blank)" in r.stderr, r.stderr)
r = run(["-n", "-p", "generic", "-s", "4", prog])
check("--start-line skips earlier lines", "G1 X-1.0" in r.stdout.upper() and "G0" not in r.stdout.upper().replace("G1","") , r.stdout)

# ---- pre-flight problems --------------------------------------------------
bad = write_tmp("G1 X1\nG0 X1 (unterminated\nM30\n")
r = run(["-n", "--strip-comments", bad]);  check("unterminated comment refused", r.returncode == 1 and "unterminated" in r.stderr, r.stderr)
bad2 = write_tmp("G1 X1 (café °)\nM30\n")
r = run(["-n", "--no-strip-comments", bad2]); check("non-ASCII refused", r.returncode == 1 and "non-ASCII" in r.stderr, r.stderr)
r = run(["-n", "--strip-comments", bad2]);    check("non-ASCII in stripped comment is fine", r.returncode == 0, r.stderr)
longf = write_tmp("G1 " + "X1 " * 40 + "\n")
r = run(["-n", "-p", "grbl", longf]);      check("grbl line-length limit enforced", r.returncode == 1 and "allows 80" in r.stderr, r.stderr)

# ---- config file ----------------------------------------------------------
conf = write_tmp("""\
    [dripfeed]
    device = /dev/null
    profile = shop-lathe

    [shop-lathe]
    base = fanuc-0i
    description = "Test lathe"
    baud = 4800
    eol = cr
    """, ".conf")
r = run(["-C", conf, "--show-profile"])
check("config: profile + base + override", "4800 baud, 7E2" in r.stdout and "<CR>" in r.stdout and "shop-lathe" in r.stdout, r.stdout)
r = run(["-C", conf, "-b", "9600", "--show-profile"]);  check("CLI overrides config", "9600 baud" in r.stdout, r.stdout)
r = run(["-C", conf, "--list-profiles"]);              check("config profile listed", "shop-lathe" in r.stdout)
ex = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "dripfeed.conf.example")
r = run(["-C", ex, "--list-profiles"])
check("shipped example config loads", r.returncode == 0 and all(n in r.stdout for n in ("shop-lathe", "bench-router", "old-mill")), r.stderr)
r = run(["-C", ex, "--show-profile"])
check("example: inline comments stripped, global profile used",
      "4800 baud, 7E2, flow=rtscts" in r.stdout and "shop-lathe" in r.stdout, r.stdout + r.stderr)
r = run(["-C", ex, "-p", "old-mill", "--show-profile"])
check("example: char ack parsed", "char:0x3e" in r.stdout, r.stdout)
r = run(["-C", ex, "-p", "bench-router", "--show-profile"])
check("example: base=grbl + override", "ok (timeout 300000 ms)" in r.stdout, r.stdout)
badconf = write_tmp("[x]\nbaud = fast\nbogus = 1\n", ".conf")
r = run(["-C", badconf, "--list-profiles"])
check("bad config: all errors reported with line numbers", r.returncode == 2 and ":2:" in r.stderr and ":3:" in r.stderr, r.stderr)

# ---- real transmission over a pty ----------------------------------------
m = Machine()
rc, out, err = m.run(["-p", "generic", "-f", "none", "--strip-comments", "--upper", "--percent", prog])
check("pty: exit 0", rc == 0, err)
check("pty: exact bytes on the wire",
      m.rx == b"%\r\nG0 X0 Z1.0\r\nG1 X-1.0 F0.005\r\nM30\r\n%\r\n", repr(m.rx))

m = Machine()
rc, out, err = m.run(["-p", "generic", "-f", "none", "--eol", "lf", "--no-strip-comments", "--no-upper", prog])
check("pty: comments kept when not stripping", m.rx.startswith(b"(header comment)\ng0 x0 z1.0 ; rapid\n"), repr(m.rx))

# ---- ack: character -------------------------------------------------------
m = Machine(reply=lambda i: b">")
rc, out, err = m.run(["-p", "generic", "-f", "none", "--strip-comments", "-a", "char:3e", "-T", "2000", prog])
check("ack char: completes when machine answers", rc == 0, err)
m = Machine(reply=None)
rc, out, err = m.run(["-p", "generic", "-f", "none", "--strip-comments", "-a", "char:3e", "-T", "300", prog])
check("ack char: times out with resume hint", rc == 1 and "timed out" in err and "--start-line" in err, err)

# ---- ack: grbl style ok / error ------------------------------------------
m = Machine(reply=lambda i: b"ok\r\n")
rc, out, err = m.run(["-p", "grbl", prog])
check("grbl: ok handshake completes", rc == 0, err)
check("grbl: comments stripped, upper, LF", m.rx == b"G0 X0 Z1.0\nG1 X-1.0 F0.005\nM30\n", repr(m.rx))

m = Machine(reply=lambda i: b"ok\r\n" if i < 1 else b"error:20\r\n")
rc, out, err = m.run(["-p", "grbl", prog])
check("grbl: error line stops with exit 4", rc == 4 and "error:20" in err, f"{rc} {err}")
check("grbl: resume line reported", "--start-line 4" in err or "--start-line 2" in err or "--start-line 3" in err, err)

# ---- abort via signal -----------------------------------------------------
m = Machine(reply=None)
p = subprocess.Popen([BIN, "-d", m.path, "-p", "generic", "-f", "none", "-a", "char:3e",
                      "-T", "20000", prog], stdin=subprocess.DEVNULL,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
time.sleep(0.5); p.send_signal(2)           # SIGINT while waiting for an ack
try: p.wait(timeout=5)
except subprocess.TimeoutExpired: p.kill()
check("SIGINT aborts with exit 3", p.returncode == 3, str(p.returncode))

# ---- pause/resume via signals --------------------------------------------
m = Machine(reply=lambda i: b">")
p = subprocess.Popen([BIN, "-d", m.path, "-p", "generic", "-f", "none", "-a", "char:3e",
                      "-l", "400", prog], stdin=subprocess.DEVNULL,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
def pump_for(sec):
    end = time.time() + sec; n = 0
    while time.time() < end:
        if select.select([m.master], [], [], 0.05)[0]:
            d = os.read(m.master, 4096); m.rx += d
            for _ in range(d.count(b"\n")): os.write(m.master, b">")
time.sleep(0.2); p.send_signal(10)         # SIGUSR1 = pause
pump_for(0.8); n1 = m.rx.count(b"\n"); pump_for(1.0); n2 = m.rx.count(b"\n")
check("SIGUSR1 pauses (no new lines)", n1 == n2, f"{n1} -> {n2}")
p.send_signal(12)                           # SIGUSR2 = resume
pump_for(3.0)
check("SIGUSR2 resumes and finishes", p.poll() == 0, str(p.poll()))
if p.poll() is None: p.kill()

print()
if FAILS:
    print(f"{len(FAILS)} FAILED: {FAILS}"); sys.exit(1)
print("all tests passed")
