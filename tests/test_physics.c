#include <math.h>
#include <string.h>

#include "physics.h"
#include "test.h"

static float cell_centre(int c)
{
    return (float)(c * CELL_PX + CELL_PX / 2);
}

/* Floor everywhere, walls on the border, start at cell (2,2). */
static void make_box(Level *lv)
{
    int x, y;
    memset(lv, 0, sizeof(*lv));
    lv->name = "test box";
    for (y = 0; y < LEVEL_H; y++)
        for (x = 0; x < LEVEL_W; x++)
            lv->cells[y][x] = (x == 0 || y == 0 || x == LEVEL_W - 1 || y == LEVEL_H - 1)
                                  ? CELL_WALL : CELL_FLOOR;
    lv->start_x = cell_centre(2);
    lv->start_y = cell_centre(2);
}

/* Smallest distance from the ball centre to any wall cell's AABB. */
static float wall_clearance(const Level *lv, const Ball *b)
{
    int x, y;
    float best = 1e9f;
    for (y = 0; y < LEVEL_H; y++)
        for (x = 0; x < LEVEL_W; x++) {
            float minx, miny, px, py, d;
            if (lv->cells[y][x] != CELL_WALL)
                continue;
            minx = (float)(x * CELL_PX);
            miny = (float)(y * CELL_PX);
            px = b->x < minx ? minx : (b->x > minx + CELL_PX ? minx + CELL_PX : b->x);
            py = b->y < miny ? miny : (b->y > miny + CELL_PX ? miny + CELL_PX : b->y);
            d = (float)sqrt((b->x - px) * (b->x - px) + (b->y - py) * (b->y - py));
            if (d < best)
                best = d;
        }
    return best;
}

/* Drive for `secs`; returns 1 if the ball never overlapped a wall after any step. */
static int drive_no_overlap(const Level *lv, Ball *b, float tx, float ty, float dt, float secs,
                            float *worst)
{
    int steps = (int)(secs / dt + 0.5f), i, ok = 1;
    *worst = 1e9f;
    for (i = 0; i < steps; i++) {
        float c;
        physics_step(b, lv, tx, ty, dt, NULL);
        c = wall_clearance(lv, b);
        if (c < *worst)
            *worst = c;
        if (c < BALL_RADIUS - 0.01f)
            ok = 0;
    }
    return ok;
}

static PhysResult roll_until_capture(const Level *lv, Ball *b, float tx, float ty, float secs)
{
    int steps = (int)(secs * 60.0f), i;
    for (i = 0; i < steps; i++) {
        PhysResult r = physics_step(b, lv, tx, ty, 1.0f / 60.0f, NULL);
        if (r != PHYS_ROLLING)
            return r;
    }
    return PHYS_ROLLING;
}

/* Sets the ball moving at vx0 (zero tilt, so it just coasts) toward the border wall and
 * runs for up to `secs`; returns the largest wall_impact physics_step ever reported. */
static float max_impact_driving(const Level *lv, float vx0, float secs)
{
    Ball b;
    float dt = 1.0f / 60.0f, best = 0.0f;
    int steps = (int)(secs / dt + 0.5f), i;
    physics_reset(&b, lv);
    b.vx = vx0;
    for (i = 0; i < steps; i++) {
        float impact = -1.0f; /* poisoned: physics_step must always write it */
        physics_step(&b, lv, 0.0f, 0.0f, dt, &impact);
        CHECK(impact >= 0.0f);
        if (impact > best)
            best = impact;
    }
    return best;
}

int main(void)
{
    Level lv;
    Ball b;
    float worst;
    int i;

    /* --- reset --- */
    make_box(&lv);
    b.x = b.y = b.vx = b.vy = 123.0f;
    physics_reset(&b, &lv);
    CHECK(b.x == cell_centre(2) && b.y == cell_centre(2) && b.vx == 0.0f && b.vy == 0.0f);

    /* --- direction --- */
    make_box(&lv);
    lv.start_x = cell_centre(11);
    lv.start_y = cell_centre(6);
    physics_reset(&b, &lv);
    for (i = 0; i < 12; i++)
        physics_step(&b, &lv, 1.0f, 0.0f, 1.0f / 60.0f, NULL);
    CHECK(b.x > cell_centre(11) + 1.0f);
    CHECK(b.vx > 0.0f);
    CHECK(fabs(b.y - cell_centre(6)) < 0.001f);

    physics_reset(&b, &lv);
    for (i = 0; i < 12; i++)
        physics_step(&b, &lv, -1.0f, 0.0f, 1.0f / 60.0f, NULL);
    CHECK(b.x < cell_centre(11) - 1.0f);
    CHECK(b.vx < 0.0f);

    physics_reset(&b, &lv);
    for (i = 0; i < 12; i++)
        physics_step(&b, &lv, 0.0f, 1.0f, 1.0f / 60.0f, NULL);
    CHECK(b.y > cell_centre(6) + 1.0f);
    CHECK(b.vy > 0.0f);

    physics_reset(&b, &lv);
    for (i = 0; i < 12; i++)
        physics_step(&b, &lv, 0.0f, -1.0f, 1.0f / 60.0f, NULL);
    CHECK(b.y < cell_centre(6) - 1.0f);
    CHECK(b.vy < 0.0f);

    /* Tilt is clamped: tilt 5 behaves exactly like tilt 1. */
    {
        Ball a2, b2;
        physics_reset(&a2, &lv);
        physics_reset(&b2, &lv);
        for (i = 0; i < 12; i++) {
            physics_step(&a2, &lv, 1.0f, 0.0f, 1.0f / 60.0f, NULL);
            physics_step(&b2, &lv, 5.0f, 0.0f, 1.0f / 60.0f, NULL);
        }
        CHECK(a2.x == b2.x && a2.vx == b2.vx);
    }

    /* Frame-rate independence: 0.5 s at 60 Hz vs 30 Hz lands within 2 px. */
    {
        Ball a60, a30;
        physics_reset(&a60, &lv);
        physics_reset(&a30, &lv);
        for (i = 0; i < 30; i++)
            physics_step(&a60, &lv, 1.0f, 0.0f, 1.0f / 60.0f, NULL);
        for (i = 0; i < 15; i++)
            physics_step(&a30, &lv, 1.0f, 0.0f, 1.0f / 30.0f, NULL);
        CHECK(fabs(a60.x - a30.x) < 2.0f);
    }

    /* Coasting ball eventually settles. */
    physics_reset(&b, &lv);
    b.vx = 300.0f;
    for (i = 0; i < 60 * 5; i++)
        physics_step(&b, &lv, 0.0f, 0.0f, 1.0f / 60.0f, NULL);
    CHECK(b.vx == 0.0f && b.vy == 0.0f);

    /* --- zero tilt at rest stays put for 5 s --- */
    physics_reset(&b, &lv);
    for (i = 0; i < 60 * 5; i++)
        physics_step(&b, &lv, 0.0f, 0.0f, 1.0f / 60.0f, NULL);
    CHECK(fabs(b.x - cell_centre(11)) < 0.01f && fabs(b.y - cell_centre(6)) < 0.01f);

    /* --- speed clamp --- */
    physics_reset(&b, &lv);
    b.vx = 5000.0f;
    physics_step(&b, &lv, 0.0f, 0.0f, 1.0f / 60.0f, NULL);
    CHECK(sqrt(b.vx * b.vx + b.vy * b.vy) <= 700.5f);

    /* --- walls: max speed into the border, dt 1/60 and 0.1 --- */
    make_box(&lv);
    lv.start_x = cell_centre(11);
    lv.start_y = cell_centre(6);
    physics_reset(&b, &lv);
    b.vx = 700.0f;
    CHECK(drive_no_overlap(&lv, &b, 1.0f, 0.0f, 1.0f / 60.0f, 3.0f, &worst));
    CHECK(b.x < (float)((LEVEL_W - 1) * CELL_PX));
    printf("  border 1/60: worst clearance %.4f\n", worst);

    physics_reset(&b, &lv);
    b.vx = 700.0f;
    CHECK(drive_no_overlap(&lv, &b, 1.0f, 0.0f, 0.1f, 3.0f, &worst));
    CHECK(b.x < (float)((LEVEL_W - 1) * CELL_PX));
    printf("  border 0.1: worst clearance %.4f\n", worst);

    /* Diagonal into a corner. */
    physics_reset(&b, &lv);
    b.vx = 495.0f;
    b.vy = 495.0f;
    CHECK(drive_no_overlap(&lv, &b, 1.0f, 1.0f, 0.1f, 3.0f, &worst));
    CHECK(b.x < (float)((LEVEL_W - 1) * CELL_PX) && b.y < (float)((LEVEL_H - 1) * CELL_PX));

    /* A single 40 px interior wall cell must not be tunnelled through at dt 0.1. */
    make_box(&lv);
    lv.cells[6][12] = CELL_WALL;
    lv.start_x = cell_centre(8);
    lv.start_y = cell_centre(6);
    for (i = 0; i < 2; i++) {
        float dt = i == 0 ? 1.0f / 60.0f : 0.1f;
        physics_reset(&b, &lv);
        b.vx = 700.0f;
        CHECK(drive_no_overlap(&lv, &b, 1.0f, 0.0f, dt, 3.0f, &worst));
        CHECK(b.x < (float)(12 * CELL_PX));
    }

    /* Wall absorbs speed: bounce-back speed is well below impact speed. */
    physics_reset(&b, &lv);
    b.vx = 600.0f;
    {
        float minvx = 0.0f;
        for (i = 0; i < 60; i++) {
            physics_step(&b, &lv, 0.0f, 0.0f, 1.0f / 60.0f, NULL);
            if (b.vx < minvx)
                minvx = b.vx;
        }
        CHECK(minvx < 0.0f && minvx > -600.0f * 0.5f);
    }

    /* --- hole / goal capture --- */
    make_box(&lv);
    lv.cells[6][8] = CELL_HOLE;
    lv.start_x = cell_centre(3);
    lv.start_y = cell_centre(6);
    physics_reset(&b, &lv);
    CHECK(roll_until_capture(&lv, &b, 1.0f, 0.0f, 5.0f) == PHYS_FELL);
    CHECK(fabs(b.x - cell_centre(8)) < 13.0f);

    make_box(&lv);
    lv.cells[6][8] = CELL_GOAL;
    lv.start_x = cell_centre(3);
    lv.start_y = cell_centre(6);
    physics_reset(&b, &lv);
    CHECK(roll_until_capture(&lv, &b, 1.0f, 0.0f, 5.0f) == PHYS_GOAL);

    /* Grazing: centre passes 20 px from the hole centre -> survives. */
    make_box(&lv);
    lv.cells[6][8] = CELL_HOLE;
    lv.start_x = cell_centre(3);
    lv.start_y = cell_centre(6) - 20.0f;
    physics_reset(&b, &lv);
    {
        PhysResult r = PHYS_ROLLING;
        for (i = 0; i < 60 * 5 && b.x < cell_centre(8) + 60.0f; i++) {
            r = physics_step(&b, &lv, 1.0f, 0.0f, 1.0f / 60.0f, NULL);
            if (r != PHYS_ROLLING)
                break;
        }
        CHECK(r == PHYS_ROLLING);
        CHECK(b.x >= cell_centre(8) + 60.0f); /* it really went past */
    }
    /* Control: same pass 5 px off centre does fall (the graze check can go red). */
    lv.start_y = cell_centre(6) - 5.0f;
    physics_reset(&b, &lv);
    CHECK(roll_until_capture(&lv, &b, 1.0f, 0.0f, 5.0f) == PHYS_FELL);

    /* Degenerate input. */
    physics_reset(&b, &lv);
    CHECK(physics_step(&b, &lv, 1.0f, 1.0f, 0.0f, NULL) == PHYS_ROLLING);
    CHECK(physics_step(&b, &lv, 1.0f, 1.0f, -1.0f, NULL) == PHYS_ROLLING);
    CHECK(b.x == lv.start_x && b.y == lv.start_y);
    CHECK(physics_step(NULL, &lv, 1.0f, 1.0f, 0.1f, NULL) == PHYS_ROLLING);

    /* --- wall_impact --- */
    make_box(&lv);
    lv.start_x = cell_centre(11);
    lv.start_y = cell_centre(6);

    /* Free rolling on open floor, far from any wall: impact reports 0. */
    physics_reset(&b, &lv);
    b.vx = 200.0f;
    {
        float impact = -1.0f;
        for (i = 0; i < 30; i++)
            physics_step(&b, &lv, 0.0f, 0.0f, 1.0f / 60.0f, &impact);
        CHECK(impact == 0.0f);
    }

    /* NULL is accepted, including on a step that strikes a wall. */
    physics_reset(&b, &lv);
    b.vx = 700.0f;
    for (i = 0; i < 60 * 3; i++)
        physics_step(&b, &lv, 1.0f, 0.0f, 1.0f / 60.0f, NULL);

    /* Driven into the border wall, impact goes positive on the contact step; a faster
     * approach gives a larger impact. Start close enough to the wall (cell 20 of 23)
     * that even the slow approach's friction-limited coasting distance reaches it. */
    {
        Level implv;
        float impact_slow, impact_fast;
        make_box(&implv);
        implv.start_x = cell_centre(20);
        implv.start_y = cell_centre(6);
        impact_slow = max_impact_driving(&implv, 300.0f, 3.0f);
        impact_fast = max_impact_driving(&implv, 700.0f, 3.0f);
        CHECK(impact_slow > 0.0f);
        CHECK(impact_fast > 0.0f);
        CHECK(impact_fast > impact_slow);
    }

    return test_summary("physics");
}
