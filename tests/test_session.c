/* The once-per-app-run calibration latch (src/session.c).
 *
 * The behaviour being pinned: the first vault you start after opening the game measures
 * your neutral pose, and every vault after that reuses it. Closing the game and opening it
 * again must ask once more, because you will not be sitting the same way. */
#include "../src/session.h"
#include "test.h"

int main(void)
{
    int i;

    /* ---- a fresh app run asks ---- */
    session_calib_reset();
    CHECK(session_calib_needed() == 1); /* first vault of the run must calibrate */

    /* Asking is not answering: querying it repeatedly must not clear the latch by itself,
     * or a vault that reads the flag twice in one frame would skip its own calibration. */
    CHECK(session_calib_needed() == 1); /* still needed after being read once */
    CHECK(session_calib_needed() == 1); /* still needed after being read twice */

    /* ---- once a calibration completes, no vault asks again ---- */
    session_calib_mark_done();
    CHECK(session_calib_needed() == 0); /* the vault right after calibrating does not ask */

    /* steve's actual complaint was every vault asking. Sixty entries, none of them asking. */
    for (i = 0; i < 60; i++)
        CHECK(session_calib_needed() == 0);

    /* Marking again (the pause menu's Recalibrate, which ends with a fresh neutral) is
     * idempotent -- it must not toggle the latch back to needing one. */
    session_calib_mark_done();
    CHECK(session_calib_needed() == 0); /* recalibrating mid-session still does not re-ask */

    /* ---- closing and reopening the game asks again ---- */
    session_calib_reset();
    CHECK(session_calib_needed() == 1); /* next app run asks for a fresh neutral */

    /* And the latch still works after the reset -- not stuck on. */
    session_calib_mark_done();
    CHECK(session_calib_needed() == 0); /* second run behaves like the first */

    return test_summary("session");
}
