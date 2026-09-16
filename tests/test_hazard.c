/* Moving hazards: how a patrol is written into a map, what level_parse refuses, where a
 * hazard is at a given moment, and when it has the ball.
 *
 * Every map here is built from a template so a test says only what it is testing. */
#include <math.h>
#include <string.h>

#include "level.h"
#include "physics.h"
#include "test.h"

#define CENTRE(c) ((float)((c) * CELL_PX + CELL_PX / 2))

static char grid[LEVEL_H][LEVEL_W + 1];
static const char *rows[LEVEL_H];

/* Open floor inside a wall border, with the start top-left and the goal bottom-right. */
static void reset_grid(void)
{
    int x, y;
    for (y = 0; y < LEVEL_H; y++) {
        for (x = 0; x < LEVEL_W; x++)
            grid[y][x] = (x == 0 || y == 0 || x == LEVEL_W - 1 || y == LEVEL_H - 1) ? '#' : '.';
        grid[y][LEVEL_W] = '\0';
        rows[y] = grid[y];
    }
    grid[1][1] = 'S';
    grid[LEVEL_H - 2][LEVEL_W - 2] = 'G';
}

static int parse(Level *out)
{
    return level_parse(rows, "Hazard Test", out);
}

int main(void)
{
    Level lv;
    float hx, hy;

    /* Control: the template itself parses, and carries no hazards. */
    reset_grid();
    CHECK(parse(&lv) == 0);
    CHECK(lv.hazard_count == 0);
    CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(5), CENTRE(5), BALL_RADIUS) == 0);
    CHECK(level_hazard_hit(&lv, 7.5f, CENTRE(5), CENTRE(5), BALL_RADIUS) == 0);

    /* One horizontal patrol, cells (4,6) to (10,6): 6 cells of travel. */
    reset_grid();
    grid[6][4] = 'a';
    grid[6][10] = 'A';
    CHECK(parse(&lv) == 0);
    CHECK(lv.hazard_count == 1);
    CHECK(lv.hazards[0].x0 == CENTRE(4) && lv.hazards[0].y0 == CENTRE(6));
    CHECK(lv.hazards[0].x1 == CENTRE(10) && lv.hazards[0].y1 == CENTRE(6));
    CHECK(lv.hazards[0].length == 6 * CELL_PX);
    /* Both ends are ordinary floor, so the ball can roll over them. */
    CHECK(level_cell(&lv, 4, 6) == CELL_FLOOR && level_cell(&lv, 10, 6) == CELL_FLOOR);

    /* It starts at the first end, is at the far end after length/speed seconds, and is
     * back at the start after a full round trip. */
    {
        float half = lv.hazards[0].length / HAZARD_SPEED_PX;
        level_hazard_pos(&lv.hazards[0], 0.0f, &hx, &hy);
        CHECK(fabsf(hx - CENTRE(4)) < 0.01f && fabsf(hy - CENTRE(6)) < 0.01f);
        level_hazard_pos(&lv.hazards[0], half, &hx, &hy);
        CHECK(fabsf(hx - CENTRE(10)) < 0.01f && fabsf(hy - CENTRE(6)) < 0.01f);
        level_hazard_pos(&lv.hazards[0], half * 2.0f, &hx, &hy);
        CHECK(fabsf(hx - CENTRE(4)) < 0.01f && fabsf(hy - CENTRE(6)) < 0.01f);
        /* Halfway out, and the same place one full period later. */
        level_hazard_pos(&lv.hazards[0], half * 0.5f, &hx, &hy);
        CHECK(fabsf(hx - CENTRE(7)) < 0.01f);
        {
            float lx = hx;
            level_hazard_pos(&lv.hazards[0], half * 2.5f, &hx, &hy);
            CHECK(fabsf(hx - lx) < 0.01f);
        }
        /* It never leaves its patrol, and never leaves its row. */
        {
            int i, off = 0;
            for (i = 0; i <= 400; i++) {
                float t = (float)i * 0.037f;
                level_hazard_pos(&lv.hazards[0], t, &hx, &hy);
                off += hy != CENTRE(6) || hx < CENTRE(4) - 0.01f || hx > CENTRE(10) + 0.01f;
            }
            CHECK(off == 0);
        }

        /* Contact: dead on it, just inside the reach, and just outside. */
        CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(4), CENTRE(6), BALL_RADIUS) == 1);
        CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(4) + BALL_RADIUS + HAZARD_RADIUS_PX - 0.5f,
                               CENTRE(6), BALL_RADIUS) == 1);
        CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(4) + BALL_RADIUS + HAZARD_RADIUS_PX + 0.5f,
                               CENTRE(6), BALL_RADIUS) == 0);
        /* The same spot is safe at t=0 and fatal once the hazard has arrived — this is
         * the whole point of the feature, so check it both ways round. */
        CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(10), CENTRE(6), BALL_RADIUS) == 0);
        CHECK(level_hazard_hit(&lv, half, CENTRE(10), CENTRE(6), BALL_RADIUS) == 1);
        CHECK(level_hazard_hit(&lv, half, CENTRE(4), CENTRE(6), BALL_RADIUS) == 0);
    }

    /* A vertical patrol, and a second hazard alongside it. */
    reset_grid();
    grid[2][8] = 'a';
    grid[9][8] = 'A';
    grid[4][15] = 'b';
    grid[4][20] = 'B';
    CHECK(parse(&lv) == 0);
    CHECK(lv.hazard_count == 2);
    CHECK(lv.hazards[0].length == 7 * CELL_PX && lv.hazards[1].length == 5 * CELL_PX);
    level_hazard_pos(&lv.hazards[0], 7 * CELL_PX / HAZARD_SPEED_PX, &hx, &hy);
    CHECK(fabsf(hx - CENTRE(8)) < 0.01f && fabsf(hy - CENTRE(9)) < 0.01f);
    /* Either one is enough to catch the ball. */
    CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(8), CENTRE(2), BALL_RADIUS) == 1);
    CHECK(level_hazard_hit(&lv, 0.0f, CENTRE(15), CENTRE(4), BALL_RADIUS) == 1);

    /* Rejections. Each starts from a map that is known to parse, so the only thing that
     * can make it fail is the one change. */
    reset_grid();
    grid[6][4] = 'a';
    CHECK(parse(&lv) == -1); /* an end with no partner */

    reset_grid();
    grid[6][4] = 'A';
    CHECK(parse(&lv) == -1); /* the other end with no partner */

    reset_grid();
    grid[6][4] = 'a';
    grid[8][10] = 'A';
    CHECK(parse(&lv) == -1); /* diagonal patrol */

    reset_grid();
    grid[6][4] = 'a';
    grid[6][10] = 'A';
    grid[6][7] = '#';
    CHECK(parse(&lv) == -1); /* patrol walks through a wall */

    reset_grid();
    grid[6][4] = 'a';
    grid[6][10] = 'A';
    grid[6][7] = 'O';
    CHECK(parse(&lv) == -1); /* patrol walks over a hole */

    reset_grid();
    grid[6][4] = 'b';
    grid[6][10] = 'B';
    CHECK(parse(&lv) == -1); /* slot 'b' used while slot 'a' is empty */

    reset_grid();
    grid[6][4] = 'a';
    grid[6][10] = 'A';
    grid[2][4] = 'a';
    CHECK(parse(&lv) == -1); /* the same end twice */

    /* All four slots at once is fine. */
    reset_grid();
    grid[2][3] = 'a';  grid[2][8] = 'A';
    grid[4][3] = 'b';  grid[4][8] = 'B';
    grid[6][3] = 'c';  grid[6][8] = 'C';
    grid[8][3] = 'd';  grid[8][8] = 'D';
    CHECK(parse(&lv) == 0);
    CHECK(lv.hazard_count == LEVEL_MAX_HAZARDS);

    /* A letter past the last slot is not a hazard, it is a typo. */
    reset_grid();
    grid[6][4] = 'e';
    grid[6][10] = 'E';
    CHECK(parse(&lv) == -1);

    /* Every shipped vault parses, and any hazard it carries stays on floor for its whole
     * patrol — level_parse guarantees it, so this is a check that the shipped maps really
     * do go through level_parse. */
    {
        int i, with_hazards = 0, total = 0;
        for (i = 0; i < LEVEL_COUNT; i++) {
            Level s;
            CHECK(level_load(i, &s) == 0);
            total += s.hazard_count;
            with_hazards += s.hazard_count > 0;
            {
                int k, step;
                for (k = 0; k < s.hazard_count; k++)
                    for (step = 0; step <= 60; step++) {
                        float t = (float)step * (s.hazards[k].length * 2.0f / HAZARD_SPEED_PX) / 60.0f;
                        level_hazard_pos(&s.hazards[k], t, &hx, &hy);
                        CHECK(level_cell(&s, (int)(hx / CELL_PX), (int)(hy / CELL_PX)) == CELL_FLOOR);
                    }
            }
        }
        printf("  %d shipped vaults carry %d hazards\n", with_hazards, total);
    }

    return test_summary("hazard");
}
