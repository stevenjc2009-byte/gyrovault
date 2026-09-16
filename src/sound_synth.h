#ifndef GV_SOUND_SYNTH_H
#define GV_SOUND_SYNTH_H

/* Pure synthesis + mixing math for the sound system: no psp2/sce* calls anywhere in this
 * file, so it builds and runs on the host for unit testing. sound.c is the only file that
 * talks to sceAudioOut; it just feeds the ball speed and trigger events in here and asks
 * for rendered PCM. */

#include <stdint.h>

#include "sound.h" /* SoundEvent -- a plain enum, no platform dependency */

#define SYNTH_SAMPLE_RATE  48000
#define SYNTH_MAX_VOICES   6 /* concurrent one-shot voices; extra triggers steal the oldest */

typedef struct {
    SoundEvent event;
    int        active;    /* 1 while this voice is sounding */
    float      t;         /* seconds since triggered */
    float      intensity; /* 0..1, clamped at trigger time */
    uint32_t   order;     /* trigger sequence number; smallest = oldest = stolen first */
    float      phase[2];  /* oscillator phase accumulators, each wrapped to 0..1 */
    float      env;        /* running exponential-decay envelope, 1.0 at trigger, multiplied
                             * by decay_mult once per sample -- avoids an expf() every sample */
    float      decay_mult; /* per-sample decay factor for this voice's event, precomputed at
                             * trigger time: expf(-1 / (event_tau * SYNTH_SAMPLE_RATE)) */
} SynthVoice;

typedef struct {
    /* continuous "ball rolling" bed */
    float    roll_target;     /* last speed passed to synth_set_rolling(), px/s, >=0 */
    float    roll_gain_cur;   /* smoothed towards sound_roll_gain(roll_target) each sample */
    float    roll_pitch_cur;  /* smoothed towards sound_roll_pitch(roll_target) each sample */
    float    roll_tone_phase; /* 0..1 phase of the low rumble tone */
    uint32_t roll_noise_lfsr; /* xorshift state feeding the filtered-noise part of the bed */
    float    roll_noise_lp;   /* one-pole low-pass state for that noise */

    /* one-shot voices: wall hit / fell / goal */
    SynthVoice voices[SYNTH_MAX_VOICES];
    uint32_t   next_order;
    uint32_t   voice_noise_lfsr; /* separate noise generator so one-shots don't perturb
                                  * the rolling bed's own filter state */
} SynthState;

/* Zeroes the state (silence, rolling target 0). Must be called before any other synth_*
 * call on a given SynthState. */
void synth_init(SynthState *st);

/* Sets the rolling bed's target speed in px/s; negative values clamp to 0. Cheap and
 * lock-free from the caller's point of view -- just stores a float. */
void synth_set_rolling(SynthState *st, float speed);

/* Triggers a one-shot voice. intensity is clamped to 0..1. If every voice slot is already
 * active, the oldest (by trigger order) is stolen and restarted with the new event. */
void synth_trigger(SynthState *st, SoundEvent ev, float intensity);

/* Rolling-bed gain/pitch curves, pure functions of speed so they can be unit tested in
 * isolation. Both are 0/flat at speed 0 and monotonically non-decreasing with speed. */
float sound_roll_gain(float speed);  /* 0 .. ROLL_GAIN_MAX (<=1) */
float sound_roll_pitch(float speed); /* ROLL_PITCH_MIN .. ROLL_PITCH_MAX, a frequency multiplier */

/* Renders `frames` mono 16-bit samples at SYNTH_SAMPLE_RATE into out[0..frames-1], mixing
 * the rolling bed and every active voice. The final mix is hard-clamped before the int16
 * cast, so this can never wrap around regardless of how many voices overlap. A no-op if
 * st, out are NULL or frames <= 0. */
void synth_render(SynthState *st, int16_t *out, int frames);

#endif
