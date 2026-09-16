#ifndef GV_GAME_H
#define GV_GAME_H

/* Scene state machine: main menu, level select, playing, pause, cleared, updates. */

void game_init(void);

/* Advance one frame: read input, update the current scene, draw it.
 * Returns 0 to keep running; non-zero once the player picks Quit on the
 * vault-complete screen (the caller should then shut down and exit). */
int game_frame(float dt);

void game_shutdown(void);

#endif
