#ifndef GV_SESSION_H
#define GV_SESSION_H

/* State that lives for one run of the game and is deliberately never saved.
 *
 * Calibration latch: the neutral pose is measured once, on the first vault started after
 * the game opens, and reused for every vault after it. It is not written to the save file
 * -- the way the player is sitting when they next open the game is not the way they were
 * sitting when they closed it, so a new run always measures again. */

/* Forget any calibration. Called once at startup; a fresh run must ask. */
void session_calib_reset(void);

/* Record that a calibration has just finished, whether it was the hold-still step at the
 * start of a vault or Recalibrate from the pause menu. Idempotent. */
void session_calib_mark_done(void);

/* 1 = this vault must open with the hold-still step, 0 = roll straight away.
 * Reading it does not change it. */
int session_calib_needed(void);

#endif
