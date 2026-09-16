#ifndef GV_SOUND_H
#define GV_SOUND_H

/* All sounds are synthesised in code at runtime (no audio files, no third-party audio
 * library). The synthesis math lives in sound_synth.c/.h, which is pure and host-testable;
 * this header is the platform-facing API backed by a dedicated audio thread in sound.c. */

typedef enum {
    SND_WALL_HIT = 0,
    SND_FELL,
    SND_GOAL
} SoundEvent;

/* Starts the audio thread and opens the output port. Returns 0 on success; on failure
 * every other call in this file becomes a silent no-op (the game must still run without
 * sound). Safe to call more than once (a no-op after the first success). */
int sound_init(void);

/* Stops the audio thread and releases the output port. Safe to call even if sound_init()
 * failed or was never called. */
void sound_shutdown(void);

/* Sets the continuous rolling-ball bed's target speed in px/s (0 = ball at rest, silent).
 * Called every frame; the audio thread smooths towards this so speed changes don't click. */
void sound_set_rolling(float speed);

/* Triggers a one-shot sound. intensity is 0..1 (wall hit: impact strength). Several
 * one-shots may overlap; the mixer caps concurrent voices and clips safely. */
void sound_play(SoundEvent ev, float intensity);

#endif
