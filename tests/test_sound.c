#include <stdint.h>

#include "sound_synth.h"
#include "test.h"

static int nondecreasing(const float *vals, int n)
{
    int i;
    for (i = 1; i < n; i++)
        if (vals[i] < vals[i - 1])
            return 0;
    return 1;
}

static int16_t abs16(int16_t v)
{
    return v < 0 ? (int16_t)-v : v;
}

int main(void)
{
    static const float speeds[] = { 0.0f, 25.0f, 50.0f, 100.0f, 200.0f, 300.0f, 400.0f,
                                     500.0f, 600.0f, 700.0f, 900.0f, 2000.0f };
    const int n_speeds = (int)(sizeof(speeds) / sizeof(speeds[0]));
    float gains[sizeof(speeds) / sizeof(speeds[0])];
    float pitches[sizeof(speeds) / sizeof(speeds[0])];
    int i;

    /* --- rolling gain curve --- */
    for (i = 0; i < n_speeds; i++) {
        gains[i] = sound_roll_gain(speeds[i]);
        CHECK(gains[i] >= 0.0f && gains[i] <= 1.0f);
    }
    CHECK(sound_roll_gain(0.0f) == 0.0f);
    CHECK(sound_roll_gain(-50.0f) == 0.0f); /* negative speed treated as at-rest */
    CHECK(nondecreasing(gains, n_speeds));
    CHECK(sound_roll_gain(700.0f) > sound_roll_gain(50.0f)); /* clearly rises, not flat */

    /* --- rolling pitch curve --- */
    for (i = 0; i < n_speeds; i++)
        pitches[i] = sound_roll_pitch(speeds[i]);
    CHECK(nondecreasing(pitches, n_speeds));
    CHECK(sound_roll_pitch(700.0f) > sound_roll_pitch(0.0f)); /* pitch rises with speed */
    CHECK(sound_roll_pitch(2000.0f) == sound_roll_pitch(900.0f)); /* clamps past top speed */

    /* --- silence: no voices, speed 0, from a freshly-inited state --- */
    {
        SynthState st;
        int16_t buf[2048];

        synth_init(&st);
        synth_render(&st, buf, 2048);
        for (i = 0; i < 2048; i++)
            CHECK(buf[i] == 0);
    }

    /* --- rolling bed is smoothed, not an instant jump to the target gain --- */
    {
        SynthState st;
        int16_t buf[100];
        float target = sound_roll_gain(700.0f);

        synth_init(&st);
        synth_set_rolling(&st, 700.0f);
        synth_render(&st, buf, 100);
        CHECK(st.roll_gain_cur > 0.0f);
        CHECK(st.roll_gain_cur < target); /* still catching up after 100 samples */
    }

    /* --- wall hit: non-silent, never wraps an int16, actually decays, ends in exact
     * silence --- */
    {
        SynthState st;
        int16_t buf[SYNTH_SAMPLE_RATE]; /* 1 s, well past WALL_DURATION (0.15s = 7200 frames) */
        int16_t peak = 0;
        int16_t peak_early = 0; /* frames [0, 300): right after trigger */
        int16_t peak_late = 0;  /* frames [4000, 4300): still active, ~3 decay taus in */
        int tail_silent = 1;

        synth_init(&st);
        synth_trigger(&st, SND_WALL_HIT, 1.0f);
        synth_render(&st, buf, SYNTH_SAMPLE_RATE);

        for (i = 0; i < SYNTH_SAMPLE_RATE; i++) {
            int16_t av = abs16(buf[i]);
            if (av > peak)
                peak = av;
            if (i < 300 && av > peak_early)
                peak_early = av;
            if (i >= 4000 && i < 4300 && av > peak_late)
                peak_late = av;
        }
        CHECK(peak <= 32760); /* clamped well inside int16 range: no wraparound, ever */
        CHECK(peak > 200);    /* clearly audible somewhere in the clip */
        /* the envelope actually decays, not just "eventually the voice deactivates":
         * a window deep into the clip is much quieter than one right at the trigger */
        CHECK(peak_early > peak_late * 4);

        for (i = SYNTH_SAMPLE_RATE - 1000; i < SYNTH_SAMPLE_RATE; i++)
            if (buf[i] != 0) {
                tail_silent = 0;
                break;
            }
        CHECK(tail_silent);
    }

    /* --- fell and goal also produce sound and settle to exact silence --- */
    {
        int ev;
        for (ev = (int)SND_FELL; ev <= (int)SND_GOAL; ev++) {
            SynthState st;
            int16_t buf[SYNTH_SAMPLE_RATE]; /* 1 s covers both FELL (0.6s) and GOAL (0.8s) */
            int16_t peak = 0;
            int tail_silent = 1;

            synth_init(&st);
            synth_trigger(&st, (SoundEvent)ev, 1.0f);
            synth_render(&st, buf, SYNTH_SAMPLE_RATE);

            for (i = 0; i < SYNTH_SAMPLE_RATE; i++) {
                int16_t av = abs16(buf[i]);
                if (av > peak)
                    peak = av;
            }
            CHECK(peak <= 32760);
            CHECK(peak > 200);

            for (i = SYNTH_SAMPLE_RATE - 1000; i < SYNTH_SAMPLE_RATE; i++)
                if (buf[i] != 0) {
                    tail_silent = 0;
                    break;
                }
            CHECK(tail_silent);
        }
    }

    /* --- overlapping voices past the cap: never overflows the fixed voice array --- */
    {
        SynthState st;
        int16_t buf[SYNTH_SAMPLE_RATE];
        int extra = SYNTH_MAX_VOICES + 3, active_count = 0;

        synth_init(&st);
        for (i = 0; i < extra; i++)
            synth_trigger(&st, (SoundEvent)(i % 3), 0.4f + 0.1f * (float)(i % 5));

        for (i = 0; i < SYNTH_MAX_VOICES; i++)
            if (st.voices[i].active)
                active_count++;
        CHECK(active_count > 0 && active_count <= SYNTH_MAX_VOICES);

        synth_render(&st, buf, SYNTH_SAMPLE_RATE);
        {
            int16_t peak = 0;
            for (i = 0; i < SYNTH_SAMPLE_RATE; i++) {
                int16_t av = abs16(buf[i]);
                if (av > peak)
                    peak = av;
            }
            CHECK(peak <= 32760);
        }

        /* longest voice (goal, 0.8s) has finished well before the 1 s mark */
        {
            int tail_silent = 1;
            for (i = SYNTH_SAMPLE_RATE - 2000; i < SYNTH_SAMPLE_RATE; i++)
                if (buf[i] != 0) {
                    tail_silent = 0;
                    break;
                }
            CHECK(tail_silent);
        }
    }

    /* --- defensive no-ops --- */
    synth_render(NULL, NULL, 100);      /* must not crash */
    {
        SynthState st;
        synth_init(&st);
        synth_render(&st, NULL, 100);   /* must not crash */
        synth_set_rolling(NULL, 100.0f); /* must not crash */
        synth_trigger(NULL, SND_GOAL, 1.0f); /* must not crash */
    }

    return test_summary("sound");
}
