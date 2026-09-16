/* sceMotion wrapper: calibration, low-pass filter, dead zone, clamp.
 * The axis/sign convention and its sources are documented in motion_map.c. */
#include <psp2/motion.h>
#ifdef GV_TEST_INPUT
#include <psp2/ctrl.h>
#endif

#include "motion.h"

#define FILTER_ALPHA 0.25f
#define DEAD_ZONE    0.05f

static float neutral_x = 0.0f, neutral_y = 0.0f, neutral_z = -1.0f; /* flat, screen up */
static float filt_x = 0.0f, filt_y = 0.0f;
static int   sampling = 0;
static MotionAccum calib_accum;

void motion_init(void)
{
    sampling = (sceMotionStartSampling() >= 0);
}

void motion_calibrate_begin(void)
{
    motion_accum_init(&calib_accum);
}

void motion_calibrate_sample(void)
{
    SceMotionState st;
    if (sampling && sceMotionGetState(&st) >= 0)
        motion_accum_add(&calib_accum, st.acceleration.x, st.acceleration.y, st.acceleration.z);
}

int motion_calibrate_end(void)
{
    float nx, ny, nz;
    int n = motion_accum_mean(&calib_accum, &nx, &ny, &nz);

    if (n > 0) {
        neutral_x = nx;
        neutral_y = ny;
        neutral_z = nz;
    }
    filt_x = 0.0f;
    filt_y = 0.0f;
    return n;
}

static float dead_zone(float v)
{
    float mag = v < 0.0f ? -v : v;
    if (mag < DEAD_ZONE)
        return 0.0f;
    mag = (mag - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    return v < 0.0f ? -mag : mag;
}

static float clamp1(float v)
{
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

void motion_read(float *tilt_x, float *tilt_y)
{
    float raw_x = 0.0f, raw_y = 0.0f;
    SceMotionState st;

    if (sampling && sceMotionGetState(&st) >= 0)
        motion_map_accel(st.acceleration.x, st.acceleration.y, st.acceleration.z,
                         neutral_x, neutral_y, neutral_z, &raw_x, &raw_y);

    filt_x += FILTER_ALPHA * (raw_x - filt_x);
    filt_y += FILTER_ALPHA * (raw_y - filt_y);

    float tx = dead_zone(clamp1(filt_x));
    float ty = dead_zone(clamp1(filt_y));

#ifdef GV_TEST_INPUT
    /* Emulator-only: Vita3K cannot fake tilt from a keyboard, so the d-pad adds tilt.
     * Never defined in the release build (build.sh without "test"). */
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
        if (pad.buttons & SCE_CTRL_RIGHT) tx += 1.0f;
        if (pad.buttons & SCE_CTRL_LEFT)  tx -= 1.0f;
        if (pad.buttons & SCE_CTRL_DOWN)  ty += 1.0f;
        if (pad.buttons & SCE_CTRL_UP)    ty -= 1.0f;
    }
#endif

    *tilt_x = clamp1(tx);
    *tilt_y = clamp1(ty);
}

void motion_read_flat(float *tilt_x, float *tilt_y)
{
    float raw_x = 0.0f, raw_y = 0.0f;
    SceMotionState st;

    /* Unfiltered, un-dead-zoned: this feeds a live "how level is it" gauge, not the ball,
     * so it should react immediately rather than lag behind motion_read's low-pass filter. */
    if (sampling && sceMotionGetState(&st) >= 0)
        motion_map_accel(st.acceleration.x, st.acceleration.y, st.acceleration.z,
                         0.0f, 0.0f, -1.0f, &raw_x, &raw_y);

    *tilt_x = clamp1(raw_x);
    *tilt_y = clamp1(raw_y);
}
