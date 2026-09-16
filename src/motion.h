#ifndef GV_MOTION_H
#define GV_MOTION_H

/* Start sceMotion sampling. */
void motion_init(void);

/* Averaged calibration. begin clears the accumulator; call sample once per frame while the
 * player holds still; end averages the samples into the new neutral and returns the number
 * of samples used. With 0 samples, end keeps the previous neutral and returns 0. */
void motion_calibrate_begin(void);
void motion_calibrate_sample(void);
int  motion_calibrate_end(void);

/* Filtered, dead-zoned tilt relative to the calibrated neutral, each in [-1,1], in
 * the same axes as physics_step: +x = ball rolls toward screen right, +y = toward
 * screen bottom. */
void motion_read(float *tilt_x, float *tilt_y);

/* Tilt relative to lying perfectly flat (screen facing up), NOT the calibrated neutral,
 * each in [-1,1], same axes as motion_read. For the "place your Vita on a flat surface" gauge. */
void motion_read_flat(float *tilt_x, float *tilt_y);

/* Pure mapping, host-testable: gravity-relative accel (from SceMotionState.acceleration)
 * minus neutral -> raw screen-axis tilt before filtering. */
void motion_map_accel(float ax, float ay, float az, float nx, float ny, float nz,
                      float *tilt_x, float *tilt_y);

/* Pure accumulator for calibration averaging, host-testable (see motion_map.c). init clears
 * the accumulator; add folds in one raw acceleration sample; mean averages the accumulated
 * samples and normalises the result to a unit gravity vector, writing into *nx,*ny,*nz and
 * returning the sample count. With count == 0, mean leaves *nx,*ny,*nz untouched and returns 0. */
typedef struct {
    float sum_x, sum_y, sum_z;
    int   count;
} MotionAccum;

void motion_accum_init(MotionAccum *acc);
void motion_accum_add(MotionAccum *acc, float x, float y, float z);
int  motion_accum_mean(const MotionAccum *acc, float *nx, float *ny, float *nz);

#endif
