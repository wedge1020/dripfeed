/*
 * cli.c - command-line parsing (see cli.h).
 */
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "common.h"
#include "log.h"

/* Option identifiers for long-only options (above the char range). */
enum {
    OPT_SHOW_PROFILE = 256,
    OPT_DATABITS, OPT_PARITY, OPT_STOPBITS, OPT_EOL, OPT_MAX_LINE,
    OPT_STRIP_COMMENTS, OPT_NO_STRIP_COMMENTS,
    OPT_STRIP_BLANK,    OPT_NO_STRIP_BLANK,
    OPT_PERCENT,        OPT_NO_PERCENT,
    OPT_UPPER,          OPT_NO_UPPER
};

static const struct option LONG_OPTS[] = {
    { "device",            required_argument, 0, 'd' },
    { "profile",           required_argument, 0, 'p' },
    { "config",            required_argument, 0, 'C' },
    { "list-profiles",     no_argument,       0, 'L' },
    { "show-profile",      no_argument,       0, OPT_SHOW_PROFILE },
    { "baud",              required_argument, 0, 'b' },
    { "databits",          required_argument, 0, OPT_DATABITS },
    { "parity",            required_argument, 0, OPT_PARITY },
    { "stopbits",          required_argument, 0, OPT_STOPBITS },
    { "flow",              required_argument, 0, 'f' },
    { "eol",               required_argument, 0, OPT_EOL },
    { "max-line",          required_argument, 0, OPT_MAX_LINE },
    { "strip-comments",    no_argument,       0, OPT_STRIP_COMMENTS },
    { "no-strip-comments", no_argument,       0, OPT_NO_STRIP_COMMENTS },
    { "strip-blank",       no_argument,       0, OPT_STRIP_BLANK },
    { "no-strip-blank",    no_argument,       0, OPT_NO_STRIP_BLANK },
    { "percent",           no_argument,       0, OPT_PERCENT },
    { "no-percent",        no_argument,       0, OPT_NO_PERCENT },
    { "upper",             no_argument,       0, OPT_UPPER },
    { "no-upper",          no_argument,       0, OPT_NO_UPPER },
    { "line-delay",        required_argument, 0, 'l' },
    { "char-delay",        required_argument, 0, 't' },
    { "ack",               required_argument, 0, 'a' },
    { "ack-timeout",       required_argument, 0, 'T' },
    { "start-line",        required_argument, 0, 's' },
    { "dry-run",           no_argument,       0, 'n' },
    { "verbose",           no_argument,       0, 'v' },
    { "version",           no_argument,       0, 'V' },
    { "help",              no_argument,       0, 'h' },
    { 0, 0, 0, 0 }
};

static int add_override(cli_opts_t *o, const char *key, const char *value)
{
    if (o->n_overrides >= CLI_MAX_OVERRIDES) {
        log_error("too many setting options");
        return -1;
    }
    o->overrides[o->n_overrides].key = key;
    o->overrides[o->n_overrides].value = value;
    o->n_overrides++;
    return 0;
}

int cli_parse(int argc, char **argv, cli_opts_t *o)
{
    memset(o, 0, sizeof *o);
    o->start_line = 1;

    int c;
    while ((c = getopt_long(argc, argv, "d:p:C:Lb:f:l:t:a:T:s:nvVh", LONG_OPTS, NULL)) != -1) {
        int rc = 0;
        switch (c) {
        /* ---- program behaviour ---- */
        case 'd': o->device  = optarg; break;
        case 'p': o->profile = optarg; break;
        case 'C': o->config  = optarg; break;
        case 'L': o->list_profiles = 1; break;
        case OPT_SHOW_PROFILE: o->show_profile = 1; break;
        case 'n': o->dry_run = 1; break;
        case 'v': o->verbosity++; break;
        case 'V': o->version = 1; break;
        case 'h': o->help = 1; break;
        case 's': {
            char *end;
            long v = strtol(optarg, &end, 10);
            if (*end || v < 1) { log_error("--start-line needs a positive number"); return -1; }
            o->start_line = v;
            break;
        }

        /* ---- profile settings, applied later as overrides ---- */
        case 'b': rc = add_override(o, "baud", optarg); break;
        case OPT_DATABITS: rc = add_override(o, "databits", optarg); break;
        case OPT_PARITY:   rc = add_override(o, "parity", optarg); break;
        case OPT_STOPBITS: rc = add_override(o, "stopbits", optarg); break;
        case 'f': rc = add_override(o, "flow", optarg); break;
        case OPT_EOL:      rc = add_override(o, "eol", optarg); break;
        case OPT_MAX_LINE: rc = add_override(o, "max_line_len", optarg); break;
        case 'l': rc = add_override(o, "line_delay_ms", optarg); break;
        case 't': rc = add_override(o, "char_delay_us", optarg); break;
        case 'a': rc = add_override(o, "ack", optarg); break;
        case 'T': rc = add_override(o, "ack_timeout_ms", optarg); break;
        case OPT_STRIP_COMMENTS:    rc = add_override(o, "strip_comments", "yes"); break;
        case OPT_NO_STRIP_COMMENTS: rc = add_override(o, "strip_comments", "no");  break;
        case OPT_STRIP_BLANK:       rc = add_override(o, "strip_blank", "yes"); break;
        case OPT_NO_STRIP_BLANK:    rc = add_override(o, "strip_blank", "no");  break;
        case OPT_PERCENT:           rc = add_override(o, "percent_frame", "yes"); break;
        case OPT_NO_PERCENT:        rc = add_override(o, "percent_frame", "no");  break;
        case OPT_UPPER:             rc = add_override(o, "uppercase", "yes"); break;
        case OPT_NO_UPPER:          rc = add_override(o, "uppercase", "no");  break;

        default: return -1;     /* getopt already printed the problem */
        }
        if (rc) return -1;
    }

    if (optind < argc) o->file = argv[optind++];
    if (optind < argc) { log_error("only one G-code file may be given"); return -1; }
    return 0;
}

void cli_version(FILE *out)
{
    fprintf(out, "%s %s\n", DF_NAME, DF_VERSION);
}

void cli_usage(FILE *out)
{
    fputs(
"Usage: dripfeed [options] FILE.nc\n"
"\n"
"Drip-feed a G-code file to a CNC control over a serial line.\n"
"\n"
"Selecting the machine:\n"
"  -p, --profile NAME      machine profile (see --list-profiles); default: generic\n"
"  -d, --device DEV        serial device, e.g. /dev/ttyUSB0 or /dev/serial0\n"
"  -C, --config FILE       read extra profiles/defaults from FILE\n"
"  -L, --list-profiles     list available profiles and exit\n"
"      --show-profile      print the fully resolved settings and exit\n"
"\n"
"Overriding profile settings (each replaces the profile's value):\n"
"  -b, --baud N            baud rate\n"
"      --databits 7|8      data bits\n"
"      --parity N|E|O      parity\n"
"      --stopbits 1|2      stop bits\n"
"  -f, --flow MODE         none | xonxoff | rtscts | both\n"
"      --eol crlf|lf|cr    line terminator sent after each block\n"
"      --[no-]percent      wrap the program in '%' lines\n"
"      --[no-]strip-comments   remove (..) and ;.. comments\n"
"      --[no-]strip-blank  skip blank lines\n"
"      --[no-]upper        force upper case\n"
"      --max-line N        refuse blocks longer than N characters (0 = no limit)\n"
"  -l, --line-delay MS     pause after each line\n"
"  -t, --char-delay US     pause after each character (very slow controls)\n"
"  -a, --ack MODE          none | ok | char:HEX   (wait for a reply after each line)\n"
"  -T, --ack-timeout MS    how long to wait for that reply\n"
"\n"
"Running:\n"
"  -s, --start-line N      begin at file line N (resume after a stop)\n"
"  -n, --dry-run           don't open the port; report what would be sent\n"
"  -v, --verbose           more detail (repeat for byte-level debugging)\n"
"  -V, --version           print version and exit\n"
"  -h, --help              this help\n"
"\n"
"While running:  p / space = pause or resume,  q = abort.\n"
"Signals:        SIGUSR1 = pause, SIGUSR2 = resume, SIGINT/SIGTERM = abort.\n"
"Exit status:    0 done, 1 error, 2 usage, 3 aborted, 4 machine reported an error.\n"
"\n"
"Pausing stops NEW lines being sent; the machine keeps running what it has\n"
"already buffered. It is not a substitute for the machine's feed hold / e-stop.\n"
"\n"
"Examples:\n"
"  dripfeed --list-profiles\n"
"  dripfeed -n -p haas part.nc                     # rehearse, nothing is sent\n"
"  dripfeed -p fanuc-0i -d /dev/ttyUSB0 part.nc\n"
"  dripfeed -p haas -b 19200 -d /dev/serial0 part.nc\n"
"  dripfeed -p fanuc-0i -d /dev/ttyUSB0 -s 120 part.nc   # resume at line 120\n"
"\n"
"Config files: /etc/dripfeed.conf, ~/.config/dripfeed/dripfeed.conf, and -C FILE.\n"
"See docs/dripfeed.conf.example and USAGE.md.\n", out);
}
