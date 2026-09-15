/* Direction tests for motion_map_accel. The gravity vectors below follow the convention
 * sourced in src/motion_map.c (Vita3K: acceleration = gravity vector in g, device frame
 * +X right, +Y toward the top edge, +Z out of the screen; flat screen-up = (0,0,-1)). */
#include <math.h>
#include <stdio.h>

#include "../src/motion.h"

static int passed = 0, failed = 0;

static void check(int cond, const char *what, float tx, float ty)
{
    if (cond) {
        passed++;
    } else {
        failed++;
        printf("FAIL: %s (tilt_x=%.3f tilt_y=%.3f)\n", what, tx, ty);
    }
}

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
    check(fabsf(tx) < 1e-4f && fabsf(ty) < 1e-4f, "flat -> ~0", tx, ty);

    /* RIGHT edge down 25 deg: gravity gains +X -> ball rolls RIGHT (+tilt_x), ~1.0. */
    motion_map_accel(sinf(a), 0.0f, -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    check(tx > 0.0f, "right edge down -> ball rolls right (tilt_x > 0)", tx, ty);
    check(fabsf(tx - 1.0f) < 0.02f, "25 deg right -> tilt_x ~= 1.0", tx, ty);
    check(fabsf(ty) < 1e-4f, "right edge down -> no vertical tilt", tx, ty);

    /* LEFT edge down: ball rolls left. */
    motion_map_accel(-sinf(a), 0.0f, -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    check(tx < 0.0f, "left edge down -> ball rolls left (tilt_x < 0)", tx, ty);

    /* TOP edge (away from you) down 25 deg: gravity gains +Y -> ball rolls UP the screen,
     * which is -y in physics space. */
    motion_map_accel(0.0f, sinf(a), -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    check(ty < 0.0f, "top edge down -> ball rolls up the screen (tilt_y < 0)", tx, ty);
    check(fabsf(ty + 1.0f) < 0.02f, "25 deg top -> tilt_y ~= -1.0", tx, ty);
    check(fabsf(tx) < 1e-4f, "top edge down -> no horizontal tilt", tx, ty);

    /* BOTTOM edge (toward you) down: ball rolls down the screen. */
    motion_map_accel(0.0f, -sinf(a), -cosf(a), FLAT_X, FLAT_Y, FLAT_Z, &tx, &ty);
    check(ty > 0.0f, "bottom edge down -> ball rolls down the screen (tilt_y > 0)", tx, ty);

    /* Neutral subtraction: holding the calibrated pose reads 0 even when not flat. */
    const float nx = 0.20f, ny = 0.35f, nz = -0.915f;
    motion_map_accel(nx, ny, nz, nx, ny, nz, &tx, &ty);
    check(fabsf(tx) < 1e-4f && fabsf(ty) < 1e-4f, "at calibrated neutral -> ~0", tx, ty);

    /* ...and tilting right/away from that neutral keeps the same directions. */
    motion_map_accel(nx + 0.10f, ny, nz, nx, ny, nz, &tx, &ty);
    check(tx > 0.0f && fabsf(ty) < 1e-4f, "right of neutral -> tilt_x > 0", tx, ty);
    motion_map_accel(nx, ny + 0.10f, nz, nx, ny, nz, &tx, &ty);
    check(ty < 0.0f && fabsf(tx) < 1e-4f, "top-down from neutral -> tilt_y < 0", tx, ty);

    printf("test_motion: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
