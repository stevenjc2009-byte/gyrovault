#include "sound_synth.h"

#include <math.h>

#define GV_PI 3.14159265358979323846f

/* ---- rolling-bed curves -------------------------------------------------------- */

/* The ball's top speed (see physics.c MAX_SPEED_PX_S); the curves below reach their
 * max at this speed and hold flat past it, so they stay sane if the ball ever exceeds it. */
#define ROLL_SPEED_FOR_MAX 700.0f

#define ROLL_GAIN_MAX   0.55f
#define ROLL_PITCH_MIN  0.60f
#define ROLL_PITCH_MAX  2.20f

#define ROLL_BASE_HZ       60.0f  /* rumble tone frequency at ROLL_PITCH_MIN */
#define ROLL_TONE_MIX      0.55f
#define ROLL_NOISE_MIX     0.45f  /* ROLL_TONE_MIX + ROLL_NOISE_MIX == 1 */
#define ROLL_NOISE_LP_COEF 0.35f  /* one-pole low-pass on the noise bed */

/* Smoothing coefficient applied per sample: cur += (target - cur) * COEFF. Chosen for a
 * ~80ms time constant at 48kHz (1 / (48000 * 0.08) =~ 0.00026) so speed changes glide
 * instead of clicking. */
#define ROLL_SMOOTH_COEFF 0.00026f

static float clamp01(float v)
{
    if (!(v >= 0.0f)) /* also catches NaN */
        return 0.0f;
    return v > 1.0f ? 1.0f : v;
}

float sound_roll_gain(float speed)
{
    float t, ease;

    if (!(speed > 0.0f)) /* also catches NaN */
        return 0.0f;
    t = speed / ROLL_SPEED_FOR_MAX;
    if (t > 1.0f)
        t = 1.0f;
    /* ease-out (1 - (1-t)^2): audible quickly, doesn't keep climbing linearly forever */
    ease = 1.0f - (1.0f - t) * (1.0f - t);
    return ROLL_GAIN_MAX * ease;
}

float sound_roll_pitch(float speed)
{
    float t;

    if (!(speed > 0.0f))
        speed = 0.0f;
    t = speed / ROLL_SPEED_FOR_MAX;
    if (t > 1.0f)
        t = 1.0f;
    return ROLL_PITCH_MIN + (ROLL_PITCH_MAX - ROLL_PITCH_MIN) * t;
}

/* ---- noise -------------------------------------------------------------------- */

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    if (x == 0)
        x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Roughly uniform in [-1, 1). */
static float noise_unit(uint32_t *state)
{
    return ((float)(xorshift32(state) >> 8) / (float)(1 << 24)) * 2.0f - 1.0f;
}

/* ---- one-shot voices ------------------------------------------------------------ */

#define WALL_DURATION   0.15f
#define WALL_DECAY_TAU  0.028f
#define WALL_F0         850.0f
#define WALL_F1         1300.0f
#define WALL_GAIN       0.70f
#define WALL_NOISE_MIX  0.35f

#define FELL_DURATION   0.6f
#define FELL_F0         520.0f
#define FELL_F1         85.0f
#define FELL_DECAY_TAU  0.22f
#define FELL_ATTACK_S   0.015f
#define FELL_GAIN       0.75f
#define FELL_NOISE_MIX  0.25f

#define GOAL_DURATION    0.8f
#define GOAL_F0          440.0f
#define GOAL_F1          880.0f
#define GOAL_H2_RATIO    1.5f
#define GOAL_DECAY_TAU   0.32f
#define GOAL_ATTACK_S    0.006f
#define GOAL_GAIN        0.70f

static float voice_duration(SoundEvent ev)
{
    switch (ev) {
    case SND_WALL_HIT: return WALL_DURATION;
    case SND_FELL:      return FELL_DURATION;
    case SND_GOAL:       return GOAL_DURATION;
    default:              return 0.0f;
    }
}

static float voice_decay_tau(SoundEvent ev)
{
    switch (ev) {
    case SND_WALL_HIT: return WALL_DECAY_TAU;
    case SND_FELL:      return FELL_DECAY_TAU;
    case SND_GOAL:       return GOAL_DECAY_TAU;
    default:              return 1.0f;
    }
}

static void wrap_phase(float *phase)
{
    if (*phase >= 1.0f)
        *phase -= (float)(int)*phase;
}

/* Advances one voice by one sample and returns its (unmixed) contribution. Deactivates
 * the voice once it passes its duration, so silence after that point is exact -- no
 * asymptotic tail to reason about in tests or on device. */
static float voice_sample(SynthVoice *v, uint32_t *noise_state)
{
    const float dt = 1.0f / (float)SYNTH_SAMPLE_RATE;
    float duration = voice_duration(v->event);
    float out = 0.0f;
    float env;

    if (!v->active || v->t >= duration) {
        v->active = 0;
        return 0.0f;
    }

    switch (v->event) {
    case SND_WALL_HIT: {
        float tone, click;

        /* env[n] == expf(-t/TAU): tracked incrementally via decay_mult (set at trigger
         * time) instead of calling expf() every sample here in the audio thread. */
        env = v->env;
        v->env *= v->decay_mult;
        v->phase[0] += WALL_F0 * dt; wrap_phase(&v->phase[0]);
        v->phase[1] += WALL_F1 * dt; wrap_phase(&v->phase[1]);
        tone = 0.6f * sinf(2.0f * GV_PI * v->phase[0]) + 0.4f * sinf(2.0f * GV_PI * v->phase[1]);
        click = noise_unit(noise_state);
        out = env * v->intensity * WALL_GAIN *
              ((1.0f - WALL_NOISE_MIX) * tone + WALL_NOISE_MIX * click);
        break;
    }
    case SND_FELL: {
        float k = v->t / duration;
        float freq = FELL_F0 + (FELL_F1 - FELL_F0) * k;
        float tone, noise, attack;

        v->phase[0] += freq * dt; wrap_phase(&v->phase[0]);
        tone = sinf(2.0f * GV_PI * v->phase[0]);
        noise = noise_unit(noise_state);
        attack = v->t < FELL_ATTACK_S ? (v->t / FELL_ATTACK_S) : 1.0f;
        env = attack * v->env;
        v->env *= v->decay_mult;
        out = env * v->intensity * FELL_GAIN *
              ((1.0f - FELL_NOISE_MIX) * tone + FELL_NOISE_MIX * noise);
        break;
    }
    case SND_GOAL: {
        float k = v->t / duration;
        float freq = GOAL_F0 + (GOAL_F1 - GOAL_F0) * k;
        float t1, t2, attack;

        v->phase[0] += freq * dt; wrap_phase(&v->phase[0]);
        v->phase[1] += freq * GOAL_H2_RATIO * dt; wrap_phase(&v->phase[1]);
        t1 = sinf(2.0f * GV_PI * v->phase[0]);
        t2 = sinf(2.0f * GV_PI * v->phase[1]);
        attack = v->t < GOAL_ATTACK_S ? (v->t / GOAL_ATTACK_S) : 1.0f;
        env = attack * v->env;
        v->env *= v->decay_mult;
        out = env * v->intensity * GOAL_GAIN * (0.65f * t1 + 0.35f * t2);
        break;
    }
    default:
        break;
    }

    v->t += dt;
    return out;
}

/* ---- public state machine ------------------------------------------------------ */

void synth_init(SynthState *st)
{
    int i;

    if (!st)
        return;
    st->roll_target = 0.0f;
    st->roll_gain_cur = 0.0f;
    st->roll_pitch_cur = ROLL_PITCH_MIN;
    st->roll_tone_phase = 0.0f;
    st->roll_noise_lfsr = 0x1u;
    st->roll_noise_lp = 0.0f;
    st->next_order = 0;
    st->voice_noise_lfsr = 0xACE1u;
    for (i = 0; i < SYNTH_MAX_VOICES; i++) {
        st->voices[i].event = SND_WALL_HIT;
        st->voices[i].active = 0;
        st->voices[i].t = 0.0f;
        st->voices[i].intensity = 0.0f;
        st->voices[i].order = 0;
        st->voices[i].phase[0] = 0.0f;
        st->voices[i].phase[1] = 0.0f;
        st->voices[i].env = 0.0f;
        st->voices[i].decay_mult = 1.0f;
    }
}

void synth_set_rolling(SynthState *st, float speed)
{
    if (!st)
        return;
    st->roll_target = speed > 0.0f ? speed : 0.0f;
}

void synth_trigger(SynthState *st, SoundEvent ev, float intensity)
{
    int i, victim;
    uint32_t oldest;

    if (!st)
        return;

    victim = -1;
    for (i = 0; i < SYNTH_MAX_VOICES; i++) {
        if (!st->voices[i].active) {
            victim = i;
            break;
        }
    }
    if (victim < 0) {
        /* every slot busy: steal the oldest by trigger order */
        victim = 0;
        oldest = st->voices[0].order;
        for (i = 1; i < SYNTH_MAX_VOICES; i++) {
            if (st->voices[i].order < oldest) {
                oldest = st->voices[i].order;
                victim = i;
            }
        }
    }

    st->voices[victim].event = ev;
    st->voices[victim].active = 1;
    st->voices[victim].t = 0.0f;
    st->voices[victim].intensity = clamp01(intensity);
    st->voices[victim].order = st->next_order++;
    st->voices[victim].phase[0] = 0.0f;
    st->voices[victim].phase[1] = 0.0f;
    st->voices[victim].env = 1.0f; /* expf(-0/tau) == 1.0 at trigger */
    /* Per-sample decay factor, computed once here instead of calling expf() every sample
     * in the audio thread: env[n] = decay_mult^n == expf(-(n*dt)/tau). */
    st->voices[victim].decay_mult =
        expf(-1.0f / (voice_decay_tau(ev) * (float)SYNTH_SAMPLE_RATE));
}

void synth_render(SynthState *st, int16_t *out, int frames)
{
    const float dt = 1.0f / (float)SYNTH_SAMPLE_RATE;
    int i, k;

    if (!st || !out || frames <= 0)
        return;

    for (i = 0; i < frames; i++) {
        float mix = 0.0f;
        float target_gain = sound_roll_gain(st->roll_target);
        float target_pitch = sound_roll_pitch(st->roll_target);

        st->roll_gain_cur += (target_gain - st->roll_gain_cur) * ROLL_SMOOTH_COEFF;
        st->roll_pitch_cur += (target_pitch - st->roll_pitch_cur) * ROLL_SMOOTH_COEFF;

        if (st->roll_gain_cur > 0.00001f) {
            float freq = ROLL_BASE_HZ * st->roll_pitch_cur;
            float tone, raw, noise;

            st->roll_tone_phase += freq * dt;
            wrap_phase(&st->roll_tone_phase);
            tone = sinf(2.0f * GV_PI * st->roll_tone_phase);

            raw = noise_unit(&st->roll_noise_lfsr);
            st->roll_noise_lp += (raw - st->roll_noise_lp) * ROLL_NOISE_LP_COEF;
            noise = st->roll_noise_lp;

            mix += st->roll_gain_cur * (ROLL_TONE_MIX * tone + ROLL_NOISE_MIX * noise);
        }

        for (k = 0; k < SYNTH_MAX_VOICES; k++) {
            if (st->voices[k].active)
                mix += voice_sample(&st->voices[k], &st->voice_noise_lfsr);
        }

        /* hard safety clamp: whatever combination of bed + voices summed to, this can
         * never wrap an int16 on the cast below */
        if (mix > 0.98f)
            mix = 0.98f;
        else if (mix < -0.98f)
            mix = -0.98f;

        out[i] = (int16_t)(mix * 32000.0f);
    }
}
