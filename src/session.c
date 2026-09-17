/* Per-run session state. No sce includes: compiled by tests/Makefile on the host. */
#include "session.h"

/* 0 until a calibration completes, then 1 for the rest of the run. */
static int calib_done = 0;

void session_calib_reset(void)
{
    calib_done = 0;
}

void session_calib_mark_done(void)
{
    calib_done = 1;
}

int session_calib_needed(void)
{
    return !calib_done;
}
