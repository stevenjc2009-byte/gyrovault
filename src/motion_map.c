/* Pure accel -> screen tilt mapping. No sce includes: compiled by tests/Makefile on the host.
 *
 * ---- Vita accelerometer axis convention: what is SOURCED vs ASSUMED ----
 *
 * SOURCED (read in the actual code, 2026-09-15):
 *  1. Vita3K vita3k/motion/src/motion.cpp, handle_motion_event():
 *       https://github.com/Vita3K/Vita3K/blob/master/vita3k/motion/src/motion.cpp
 *     Host phone/tablet sensors at ROTATION_0 are passed through with NO axis swap, then
 *       `sensor_data /= -SDL_STANDARD_GRAVITY;`
 *     SDL documents its accel axes as +X = right, +Y = top, +Z = toward the user
 *     (out of the screen), reporting the reaction force (+9.8 on the axis pointing up).
 *     Dividing by -g therefore gives the Vita GRAVITY vector in g: the component along
 *     the axis that points DOWN is positive.
 *  2. Vita3K vita3k/modules/SceDriverUser/SceMotion.cpp, sceMotionGetState() default
 *     state when no sensor is present: `motionState->acceleration.z = -1.0;`
 *       https://github.com/Vita3K/Vita3K/blob/master/vita3k/modules/SceDriverUser/SceMotion.cpp
 *     i.e. a Vita lying flat, screen up, reads (0, 0, -1): gravity points out the back.
 *  3. vitasdk psp2/motion.h (SceMotionDeviceLocation notes): z axis perpendicular through
 *     the screen, x axis parallel to the screen's top/bottom edge.
 *
 * CONTRADICTING SOURCE (flagged, not followed):
 *  - SDL2 src/sensor/vita/SDL_vitasensor.c passes acceleration through as
 *    `accelerometer.x * SDL_STANDARD_GRAVITY` with no sign flip, which would imply the
 *    OPPOSITE sign (reaction force). Vita3K's -g was chosen because Vita3K's convention is
 *    what commercial games (Gravity Rush, per PR #2628) were run against, and it agrees
 *    with its own flat default of z = -1.
 *       https://github.com/libsdl-org/SDL/blob/SDL2/src/sensor/vita/SDL_vitasensor.c
 *
 * ASSUMED (not measured on hardware):
 *  - The sign above. If real hardware reads z = +1 lying flat, BOTH tilt axes here are
 *    inverted. The one-line hardware check: tilt the right edge down -> ball must go right.
 *
 * Resulting mapping (gravity vector, device frame X right / Y top / Z out of screen):
 *  - Right edge down: gravity gains +X  -> ball rolls right  -> tilt_x = +(ax - nx)
 *  - Top edge down (away from you): gravity gains +Y -> ball rolls UP the screen,
 *    and physics +y is DOWN the screen -> tilt_y = -(ay - ny)
 */
#include "motion.h"

/* sin(25 deg): a 25 degree tilt moves one gravity component by this much -> tilt 1.0 */
#define TILT_FULL_SCALE_G 0.42261826f

void motion_map_accel(float ax, float ay, float az, float nx, float ny, float nz,
                      float *tilt_x, float *tilt_y)
{
    (void)az;
    (void)nz;
    *tilt_x = (ax - nx) / TILT_FULL_SCALE_G;
    *tilt_y = -(ay - ny) / TILT_FULL_SCALE_G;
}
