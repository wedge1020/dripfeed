/*
 * main.c - dripfeed entry point: wires the modules together.
 *
 *   cli      -> what the operator asked for
 *   config   -> user profiles / defaults from files
 *   profile  -> the resolved machine settings
 *   feeder   -> scan the file (pre-flight), then send or report it
 *   serial   -> the port; control/hw -> pause, abort, buttons, LEDs
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "common.h"
#include "config.h"
#include "control.h"
#include "feeder.h"
#include "log.h"
#include "profile.h"
#include "serial.h"

/* Load the two optional standard config locations. Errors are fatal; a
 * missing file is not. */
static int load_default_configs(config_globals_t *g)
{
    if (config_load("/etc/dripfeed.conf", g, 0) < 0) return -1;

    const char *home = getenv("HOME");
    if (home && *home) {
        char path[512];
        snprintf(path, sizeof path, "%s/.config/dripfeed/dripfeed.conf", home);
        if (config_load(path, g, 0) < 0) return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    cli_opts_t opt;
    config_globals_t g;
    memset(&g, 0, sizeof g);

    profile_registry_init();

    if (cli_parse(argc, argv, &opt) < 0) {
        fprintf(stderr, "Try 'dripfeed --help'.\n");
        return DF_USAGE;
    }
    if (opt.help)    { cli_usage(stdout);   return DF_OK; }
    if (opt.version) { cli_version(stdout); return DF_OK; }
    log_set_verbosity(opt.verbosity);

    /* ---- configuration: defaults, then the explicit -C file ---- */
    if (load_default_configs(&g) < 0) return DF_USAGE;
    if (opt.config && config_load(opt.config, &g, 1) < 0) return DF_USAGE;

    if (opt.list_profiles) {
        puts("Available profiles:");
        profile_list(stdout);
        return DF_OK;
    }

    /* ---- resolve the profile: command line > config file > "generic" ---- */
    const char *pname = opt.profile ? opt.profile : (*g.profile ? g.profile : "generic");
    const profile_t *base = profile_find(pname);
    if (!base) {
        log_error("unknown profile '%s' (see --list-profiles)", pname);
        return DF_USAGE;
    }
    profile_t prof = *base;                       /* private, modifiable copy */

    char err[160];
    for (int i = 0; i < opt.n_overrides; i++) {
        if (profile_set_field(&prof, opt.overrides[i].key, opt.overrides[i].value,
                              err, sizeof err)) {
            log_error("%s", err);
            return DF_USAGE;
        }
    }
    if (profile_validate(&prof, err, sizeof err)) {
        log_error("%s", err);
        return DF_USAGE;
    }

    if (opt.show_profile) { profile_print(stdout, &prof); return DF_OK; }

    /* ---- inputs ---- */
    if (!opt.file) {
        log_error("no G-code file given");
        cli_usage(stderr);
        return DF_USAGE;
    }
    const char *device = opt.device ? opt.device : (*g.device ? g.device : NULL);
    if (!device && !opt.dry_run) {
        log_error("no serial device given (use -d, or 'device =' in a config file; "
                  "use -n to rehearse without one)");
        return DF_USAGE;
    }

    feed_job_t job = {
        .path = opt.file, .device = device, .profile = &prof,
        .start_line = opt.start_line, .dry_run = opt.dry_run,
    };

    /* ---- pre-flight: validate the whole file before touching the port ---- */
    feed_scan_t scan;
    int rc = feeder_scan(&job, &scan);
    if (rc != DF_OK) return rc;
    if (scan.problems) {
        log_error("%ld problem line(s) found; nothing was sent", scan.problems);
        return DF_ERROR;
    }

    char dur[32];
    double est = feeder_estimate_seconds(&prof, &scan);
    snprintf(dur, sizeof dur, "%ldm %02lds", (long)est / 60, (long)est % 60);

    log_info("%s %s%s", DF_NAME, DF_VERSION, opt.dry_run ? "  [DRY RUN - nothing will be sent]" : "");
    log_info("  file      %s: %ld lines, %ld blocks to send (%ld bytes), %ld skipped",
             opt.file, scan.lines_in_file, scan.lines_to_send, scan.bytes_to_send,
             scan.lines_skipped);
    log_info("  profile   %s - %s", prof.name, prof.description);
    log_info("  link      %s, %d baud %d%c%d, flow=%s, ack=%s",
             device ? device : "(no device)", prof.baud, prof.databits, prof.parity,
             prof.stopbits, flow_name(prof.flow), ack_name(&prof));
    log_info("  estimate  ~%s on the wire%s", dur,
             prof.ack != ACK_NONE ? " (plus machine response time)" : "");
    if (opt.start_line > 1)
        log_info("  starting at file line %ld", opt.start_line);
    if (log_verbosity() >= 1) profile_print(stderr, &prof);

    /* ---- go ---- */
    control_init(1);
    int fd = -1;
    if (!opt.dry_run) {
        fd = serial_open(device, &prof);
        if (fd < 0) { control_shutdown(); return DF_ERROR; }
    }

    rc = feeder_run(&job, &scan, fd);

    if (fd >= 0) serial_close(fd);
    control_shutdown();
    return rc;
}
