/*
 * common.h - project-wide constants: version string and process exit codes.
 */
#ifndef DRIPFEED_COMMON_H
#define DRIPFEED_COMMON_H

#define DF_NAME     "dripfeed"
#define DF_VERSION  "0.2.0"

/*
 * Process exit codes. Scripts (and a future GPIO supervisor) can use these to
 * tell "finished cleanly" from "stopped part-way, safe to resume with
 * --start-line".
 */
enum df_exit {
    DF_OK       = 0,  /* whole program delivered                              */
    DF_ERROR    = 1,  /* I/O failure, bad file, timeout, ...                  */
    DF_USAGE    = 2,  /* bad command line or configuration                    */
    DF_ABORTED  = 3,  /* operator abort (key, signal, hardware input)         */
    DF_MACHINE  = 4   /* machine answered with an error/alarm line            */
};

#endif /* DRIPFEED_COMMON_H */
