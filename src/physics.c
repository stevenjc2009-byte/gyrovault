#include "physics.h"

#include <math.h>

#define ACCEL_PX_S2      1100.0f /* full tilt */
#define FRICTION_PX_S2   90.0f   /* constant rolling friction: lets a coasting ball stop */
#define DAMPING_PER_S    0.35f   /* proportional drag on top of friction */
#define MAX_SPEED_PX_S   BALL_MAX_SPEED
#define RESTITUTION      0.3f
#define CAPTURE_R_PX     13.0f   /* centre within this of a hole/goal centre = captured */
#define MAX_DT_S         0.25f   /* longer hitches are simulated as 0.25 s */
#define MAX_SUBSTEP_PX   (BALL_RADIUS / 3.0f)

static float clamp_unit(float v)
{
    if (!(v >= -1.0f)) /* also catches NaN */
        return v > 1.0f ? 1.0f : (v != v ? 0.0f : -1.0f);
    return v > 1.0f ? 1.0f : v;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void physics_reset(Ball *b, const Level *lv)
{
    if (!b)
        return;
    b->x = lv ? lv->start_x : 0.0f;
    b->y = lv ? lv->start_y : 0.0f;
    b->vx = 0.0f;
    b->vy = 0.0f;
}

/* Push the ball out of every wall cell in the 3x3 neighbourhood and bounce the normal
 * velocity component. Two passes settle concave corners. *impact (if non-NULL) is raised
 * to the largest inbound normal speed removed by any collision found here. */
static void collide_walls(Ball *b, const Level *lv, float *impact)
{
    int pass, dx, dy;
    const float r = BALL_RADIUS;

    for (pass = 0; pass < 2; pass++) {
        int bcx = (int)floor(b->x / CELL_PX);
        int bcy = (int)floor(b->y / CELL_PX);
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                int cx = bcx + dx, cy = bcy + dy;
                float minx, miny, maxx, maxy, px, py, ox, oy, d2, nx, ny, pen, vn;

                if (level_cell(lv, cx, cy) != CELL_WALL)
                    continue;
                minx = (float)(cx * CELL_PX);
                miny = (float)(cy * CELL_PX);
                maxx = minx + CELL_PX;
                maxy = miny + CELL_PX;
                px = clampf(b->x, minx, maxx);
                py = clampf(b->y, miny, maxy);
                ox = b->x - px;
                oy = b->y - py;
                d2 = ox * ox + oy * oy;
                if (d2 >= r * r)
                    continue;

                if (d2 > 1e-8f) {
                    float d = (float)sqrt(d2);
                    nx = ox / d;
                    ny = oy / d;
                    pen = r - d;
                } else {
                    /* Centre inside the box: leave along the axis of least penetration. */
                    float l = b->x - minx, rr = maxx - b->x, t = b->y - miny, bt = maxy - b->y;
                    float m = l;
                    nx = -1.0f; ny = 0.0f;
                    if (rr < m) { m = rr; nx = 1.0f; ny = 0.0f; }
                    if (t < m)  { m = t;  nx = 0.0f; ny = -1.0f; }
                    if (bt < m) { m = bt; nx = 0.0f; ny = 1.0f; }
                    pen = m + r;
                }

                b->x += nx * pen;
                b->y += ny * pen;
                vn = b->vx * nx + b->vy * ny;
                if (vn < 0.0f) {
                    if (impact && -vn > *impact)
                        *impact = -vn;
                    b->vx -= (1.0f + RESTITUTION) * vn * nx;
                    b->vy -= (1.0f + RESTITUTION) * vn * ny;
                }
            }
        }
    }
}

static PhysResult check_capture(const Ball *b, const Level *lv)
{
    int cx = (int)floor(b->x / CELL_PX);
    int cy = (int)floor(b->y / CELL_PX);
    CellType c = level_cell(lv, cx, cy);
    float ox, oy;

    if (c != CELL_HOLE && c != CELL_GOAL)
        return PHYS_ROLLING;
    ox = b->x - (float)(cx * CELL_PX + CELL_PX / 2);
    oy = b->y - (float)(cy * CELL_PX + CELL_PX / 2);
    if (ox * ox + oy * oy >= CAPTURE_R_PX * CAPTURE_R_PX)
        return PHYS_ROLLING;
    return c == CELL_HOLE ? PHYS_FELL : PHYS_GOAL;
}

PhysResult physics_step(Ball *b, const Level *lv, float tilt_x, float tilt_y, float dt,
                         float *wall_impact)
{
    int n, i;
    float h, ax, ay;

    if (wall_impact)
        *wall_impact = 0.0f;
    if (!b || !lv || !(dt > 0.0f))
        return PHYS_ROLLING;
    if (dt > MAX_DT_S)
        dt = MAX_DT_S;

    ax = clamp_unit(tilt_x) * ACCEL_PX_S2;
    ay = clamp_unit(tilt_y) * ACCEL_PX_S2;

    /* Speed never exceeds MAX_SPEED, so this bounds per-substep travel to BALL_RADIUS/3. */
    n = (int)ceil(MAX_SPEED_PX_S * dt / MAX_SUBSTEP_PX);
    if (n < 1)
        n = 1;
    h = dt / (float)n;

    for (i = 0; i < n; i++) {
        float speed, drop, keep;
        PhysResult res;

        b->vx += ax * h;
        b->vy += ay * h;

        keep = 1.0f - DAMPING_PER_S * h;
        b->vx *= keep;
        b->vy *= keep;

        speed = (float)sqrt(b->vx * b->vx + b->vy * b->vy);
        drop = FRICTION_PX_S2 * h;
        if (speed <= drop) {
            b->vx = 0.0f;
            b->vy = 0.0f;
        } else {
            float s = speed - drop;
            if (s > MAX_SPEED_PX_S)
                s = MAX_SPEED_PX_S;
            b->vx *= s / speed;
            b->vy *= s / speed;
        }

        b->x += b->vx * h;
        b->y += b->vy * h;

        collide_walls(b, lv, wall_impact);

        res = check_capture(b, lv);
        if (res != PHYS_ROLLING)
            return res;
    }
    return PHYS_ROLLING;
}
