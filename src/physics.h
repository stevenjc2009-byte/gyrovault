#ifndef GV_PHYSICS_H
#define GV_PHYSICS_H

#include "level.h"

#define BALL_RADIUS 14.0f
#define BALL_MAX_SPEED 700.0f /* px/s; matches physics.c's top-speed clamp */

typedef struct {
    float x, y;   /* board-space px, +x right, +y DOWN the screen */
    float vx, vy; /* px/s */
} Ball;

typedef enum {
    PHYS_ROLLING = 0,
    PHYS_FELL,    /* ball dropped into a CELL_HOLE */
    PHYS_GOAL     /* ball dropped into the CELL_GOAL */
} PhysResult;

/* Place the ball at rest on the level's start cell. */
void physics_reset(Ball *b, const Level *lv);

/* Advance by dt seconds. tilt_x/tilt_y in [-1,1] (clamped): +tilt_x accelerates the
 * ball toward +x (screen right), +tilt_y toward +y (screen bottom). Walls block and
 * absorb some speed; a hole/goal captures the ball when its centre is inside the
 * hole's radius, so grazing a hole's edge is survivable.
 * wall_impact (may be NULL) receives the speed in px/s at which the ball struck a wall
 * this step (the largest normal-velocity component removed), 0 if none. */
PhysResult physics_step(Ball *b, const Level *lv, float tilt_x, float tilt_y, float dt,
                         float *wall_impact);

#endif
