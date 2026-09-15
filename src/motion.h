#ifndef GV_MOTION_H
#define GV_MOTION_H

/* Start sceMotion sampling. */
void motion_init(void);

/* Take the console's current orientation as "level" (neutral). */
void motion_calibrate(void);

/* Filtered, dead-zoned tilt relative to the calibrated neutral, each in [-1,1], in
 * the same axes as physics_step: +x = ball rolls toward screen right, +y = toward
 * screen bottom. */
void motion_read(float *tilt_x, float *tilt_y);

/* Pure mapping, host-testable: gravity-relative accel (from SceMotionState.acceleration)
 * minus neutral -> raw screen-axis tilt before filtering. */
void motion_map_accel(float ax, float ay, float az, float nx, float ny, float nz,
                      float *tilt_x, float *tilt_y);

#endif
