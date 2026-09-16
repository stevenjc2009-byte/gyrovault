/* Direction tests for motion_map_accel. The gravity vectors below follow the convention
 * sourced in src/motion_map.c (Vita3K: acceleration = gravity vector in g, device frame
 * +X right, +Y toward the top edge, +Z out of the screen; flat screen-up = (0,0,-1)). */
#include <math.h>

#include "../src/motion.h"
#include "test.h"

#define DEG(d) ((d) * 3.14159265f / 180.0f)
#define FLAT_X 0.0f
#define FLAT_Y 0.0f
#define FLAT_Z -1.0f

int main(void)
{
    float tx, ty;
    const float a = DEG(25.0f);

    /* Flat and calibrated flat -> no tilt. */
    motion_map_accel(0.0f, 0.0f, -1.0f, FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(fabsf(tx) < 1e-4f && fabsf(ty) < 1e-4f); /* flat -> ~0 */

    /* RIGHT edge down 25 deg: gravity gains +X -> ball rolls RIGHT (+tilt_x), ~1.0. */
    motion_map_accel(sinf(a), 0.0f, -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(tx > 0.0f);                /* right edge down -> ball rolls right (tilt_x > 0) */
    CHECK(fabsf(tx - 1.0f) < 0.02f); /* 25 deg right -> tilt_x ~= 1.0 */
    CHECK(fabsf(ty) < 1e-4f);        /* right edge down -> no vertical tilt */

    /* LEFT edge down: ball rolls left. */
    motion_map_accel(-sinf(a), 0.0f, -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(tx < 0.0f); /* left edge down -> ball rolls left (tilt_x < 0) */

    /* TOP edge (away from you) down 25 deg: gravity gains +Y -> ball rolls UP the screen,
     * which is -y in physics space. */
    motion_map_accel(0.0f, sinf(a), -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(ty < 0.0f);                /* top edge down -> ball rolls up the screen (tilt_y < 0) */
    CHECK(fabsf(ty + 1.0f) < 0.02f); /* 25 deg top -> tilt_y ~= -1.0 */
    CHECK(fabsf(tx) < 1e-4f);        /* top edge down -> no horizontal tilt */

    /* BOTTOM edge (toward you) down: ball rolls down the screen. */
    motion_map_accel(0.0f, -sinf(a), -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(ty > 0.0f); /* bottom edge down -> ball rolls down the screen (tilt_y > 0) */

    /* Neutral subtraction: holding the calibrated pose reads 0 even when not flat. */
    const float nx = 0.20f, ny = 0.35f, nz = -0.915f;
    motion_map_accel(nx, ny, nz, nx, ny, nz, &tx, &ty);
    CHECK(fabsf(tx) < 1e-4f && fabsf(ty) < 1e-4f); /* at calibrated neutral -> ~0 */

    /* ...and tilting right/away from that neutral keeps the same directions. */
    motion_map_accel(nx + 0.10f, ny, nz, nx, ny, nz, &tx, &ty);
    CHECK(tx > 0.0f && fabsf(ty) < 1e-4f); /* right of neutral -> tilt_x > 0 */
    motion_map_accel(nx, ny + 0.10f, nz, nx, ny, nz, &tx, &ty);
    CHECK(ty < 0.0f && fabsf(tx) < 1e-4f); /* top-down from neutral -> tilt_y < 0 */

    /* ---- motion_accum_* : pure averaging for motion_calibrate_begin/sample/end ---- */

    /* Mean of a single sample repeated -> that same sample (it's already unit magnitude,
     * so averaging then normalising is a no-op). */
    {
        MotionAccum acc;
        float mx, my, mz;
        const float sx = sinf(a), sz = -cosf(a); /* unit vector, 25 deg tilt */
        int n;

        motion_accum_init(&acc);
        motion_accum_add(&acc, sx, 0.0f, sz);
        motion_accum_add(&acc, sx, 0.0f, sz);
        motion_accum_add(&acc, sx, 0.0f, sz);
        n = motion_accum_mean(&acc, &mx, &my, &mz);
        CHECK(n == 3); /* mean of identical samples -> count == 3 */
        /* mean of identical samples -> that sample, checked per axis */
        CHECK(fabsf(mx - sx) < 1e-4f);
        CHECK(fabsf(my) < 1e-4f);
        CHECK(fabsf(mz - sz) < 1e-4f);
    }

    /* Mean of two samples symmetric about z (opposite x tilt, same z) -> x cancels and the
     * remaining z is renormalised back up to -1, proving the mean is normalised, not just averaged. */
    {
        MotionAccum acc;
        float mx, my, mz;
        const float sx = sinf(a), sz = -cosf(a);
        int n;

        motion_accum_init(&acc);
        motion_accum_add(&acc, sx, 0.0f, sz);
        motion_accum_add(&acc, -sx, 0.0f, sz);
        n = motion_accum_mean(&acc, &mx, &my, &mz);
        CHECK(n == 2); /* mean of symmetric samples -> count == 2 */
        /* mean of symmetric samples -> renormalised to (0,0,-1), checked per axis */
        CHECK(fabsf(mx) < 1e-4f);
        CHECK(fabsf(my) < 1e-4f);
        CHECK(fabsf(mz + 1.0f) < 1e-4f);
    }

    /* Zero samples: end must not touch the output (motion_calibrate_end keeps the old neutral). */
    {
        MotionAccum acc;
        float mx = -99.0f, my = -99.0f, mz = -99.0f;
        int n;

        motion_accum_init(&acc);
        n = motion_accum_mean(&acc, &mx, &my, &mz);
        CHECK(n == 0); /* mean of zero samples -> count == 0 */
        /* mean of zero samples -> output untouched, checked per axis */
        CHECK(mx == -99.0f);
        CHECK(my == -99.0f);
        CHECK(mz == -99.0f);
    }

    /* ---- motion_read_flat's pure core: motion_map_accel against the flat neutral ---- */

    /* Lying flat, screen up -> no tilt. */
    motion_map_accel(FLAT_X, FLAT_Y, FLAT_Z, FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(fabsf(tx) < 1e-4f && fabsf(ty) < 1e-4f); /* flat reading of a flat vector -> (0,0) */

    /* Right edge down, read against the flat neutral (not a calibrated one) -> tilt_x > 0,
     * same sign convention as motion_read. */
    motion_map_accel(sinf(a), 0.0f, -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(tx > 0.0f && fabsf(ty) < 1e-4f); /* flat reading, right edge down -> tilt_x > 0 */

    /* Top edge down, read against the flat neutral -> tilt_y < 0. */
    motion_map_accel(0.0f, sinf(a), -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    CHECK(ty < 0.0f && fabsf(tx) < 1e-4f); /* flat reading, top edge down -> tilt_y < 0 */

    return test_summary("motion");
}
