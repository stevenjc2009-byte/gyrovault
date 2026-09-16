/* Solvability + difficulty ramp for every level. The solver itself lives in solve_core.c,
 * which documents how a move is modelled and how difficulty is scored; tools/level_grade.c
 * grades candidate mazes with the same code.
 *
 * Fails if any level has no route, or (when every level is run) if difficulty does not
 * rise from each level to the next. Pass a level index as argv[1] to grade just that one,
 * which skips the ramp check. */
#include <stdio.h>
#include <stdlib.h>

#include "level.h"
#include "physics.h" /* BALL_RADIUS, for the hazard-clearance check below */
#include "solve_core.h"
#include "test.h"

/* Copy `src` into `out` with every cell a hazard sweeps turned to wall, and return how
 * many cells that was. "Sweeps" means: a ball parked at that cell's centre would be
 * touching the hazard at some point in its patrol, i.e. the centre is within
 * HAZARD_RADIUS_PX + BALL_RADIUS of the patrol segment. The patrol runs between two
 * arbitrary cells and need not be axis-aligned, so this measures distance to the segment
 * rather than stepping along it. Distances stay squared -- no sqrtf, and no rounding to
 * argue about. */
static int mask_hazards(const Level *src, Level *out)
{
    const float reach = HAZARD_RADIUS_PX + BALL_RADIUS;
    int k, x, y, masked = 0;

    *out = *src;
    for (k = 0; k < src->hazard_count; k++) {
        const Hazard *h = &src->hazards[k];
        float dx = h->x1 - h->x0, dy = h->y1 - h->y0;
        float len2 = dx * dx + dy * dy;
        for (y = 1; y < LEVEL_H - 1; y++) {
            for (x = 1; x < LEVEL_W - 1; x++) {
                float px = x * CELL_PX + CELL_PX / 2.0f;
                float py = y * CELL_PX + CELL_PX / 2.0f;
                float t = len2 > 0.0f ? ((px - h->x0) * dx + (py - h->y0) * dy) / len2 : 0.0f;
                float ox, oy;
                if (t < 0.0f)
                    t = 0.0f;
                else if (t > 1.0f)
                    t = 1.0f;
                ox = px - (h->x0 + t * dx);
                oy = py - (h->y0 + t * dy);
                if (ox * ox + oy * oy <= reach * reach && out->cells[y][x] != CELL_WALL) {
                    out->cells[y][x] = CELL_WALL;
                    masked++;
                }
            }
        }
    }
    return masked;
}

int main(int argc, char **argv)
{
    static Level lv;
    int i, only = argc > 1 ? atoi(argv[1]) : -1, prev = -1;

    /* Sanity, both ways: a 1-cell corridor from the start to a goal is solvable, and the
     * same corridor with a hole in it is not (6 px of slack cannot clear a 13 px capture).
     * Two diagonal holes are NOT a trap: a ball threads the 14 px gap between them. */
    {
        static Level corridor;
        int x, y;
        CHECK(level_load(0, &corridor) == 0);
        for (y = 0; y < LEVEL_H; y++)
            for (x = 0; x < LEVEL_W; x++)
                corridor.cells[y][x] = (y == 1 && x >= 1 && x <= 10) ? CELL_FLOOR : CELL_WALL;
        corridor.cells[1][10] = CELL_GOAL;
        corridor.start_x = 1 * CELL_PX + CELL_PX / 2;
        corridor.start_y = 1 * CELL_PX + CELL_PX / 2;
        CHECK(solve_at(&corridor, 0).solved == 1);
        CHECK(grade(&corridor).tier == 0);
        corridor.cells[1][5] = CELL_HOLE;
        CHECK(grade(&corridor).solved == 0);
    }

    /* The hazard-clearance check below only ever runs on vaults that have hazards, so
     * prove here that it can go both ways rather than trusting it to. Same corridor, two
     * rows tall: a patrol along the lower row leaves the upper one clear, and a patrol
     * across the only row seals it. */
    {
        static Level haz, safe;
        int x, y;
        CHECK(level_load(0, &haz) == 0);
        for (y = 0; y < LEVEL_H; y++)
            for (x = 0; x < LEVEL_W; x++)
                haz.cells[y][x] = (x >= 1 && x <= 10 && (y == 1 || y == 2)) ? CELL_FLOOR : CELL_WALL;
        haz.cells[1][10] = CELL_GOAL;
        haz.start_x = 1 * CELL_PX + CELL_PX / 2;
        haz.start_y = 1 * CELL_PX + CELL_PX / 2;
        haz.hazard_count = 1;
        haz.hazards[0].x0 = 4 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].y0 = 2 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].x1 = 7 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].y1 = 2 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].length = 3 * CELL_PX;
        /* Four cells of the lower row, and nothing in the row the ball needs. */
        CHECK(mask_hazards(&haz, &safe) == 4);
        CHECK(safe.cells[1][5] == CELL_FLOOR);
        CHECK(solve_at(&safe, 0).solved == 1);

        /* Stand the same patrol on end so it crosses both rows: now there is no way past
         * it, and the check has to say so. */
        haz.hazards[0].x0 = haz.hazards[0].x1 = 5 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].y0 = 1 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].y1 = 2 * CELL_PX + CELL_PX / 2.0f;
        haz.hazards[0].length = CELL_PX;
        CHECK(mask_hazards(&haz, &safe) == 2);
        CHECK(safe.cells[1][5] == CELL_WALL);
        CHECK(solve_at(&safe, 0).solved == 0);
    }

    printf("  %-3s %-22s %5s %4s %5s %5s %6s %6s %6s  route\n", "#", "name", "holes", "tol",
           "cost", "diff", "moves", "pulses", "states");
    for (i = 0; i < LEVEL_COUNT; i++) {
        Report rep;
        if (only >= 0 && i != only)
            continue;
        CHECK(level_load(i, &lv) == 0);
        rep = grade(&lv);
        printf("  %-3d %-22s %5d %4d %5d %5d %6d %6d %6d  %s\n", i + 1, lv.name, count_holes(&lv),
               rep.solved ? SOLVE_TOLERANCES[rep.tier] : -1, rep.solved ? rep.cost : -1,
               rep.solved ? rep.difficulty : -1, rep.moves, rep.pulses, rep.states,
               rep.solved ? rep.route : "NO ROUTE");
        CHECK(rep.solved);
        if (only < 0) {
            if (rep.difficulty <= prev)
                printf("  level %d is not harder than level %d\n", i + 1, i);
            CHECK(rep.difficulty > prev);
            prev = rep.difficulty;
        }

        /* A hazard must not be the only thing between the ball and the goal. The grade
         * above is blind to hazards on purpose, so it could be reporting a route that
         * only works if you out-run one. Wall off every cell a hazard sweeps and ask
         * again at the loosest tolerance: if a route survives that, the vault can be
         * finished without ever sharing a cell with a hazard, and the difficulty above is
         * a floor rather than a fiction. */
        if (lv.hazard_count > 0) {
            static Level safe;
            Report clear;
            /* Every patrol has two distinct ends, so it must have masked something. */
            CHECK(mask_hazards(&lv, &safe) >= 2);
            clear = solve_at(&safe, 0); /* one Dijkstra: keep the result */
            if (!clear.solved)
                printf("  level %d cannot be finished without crossing a hazard's path\n", i + 1);
            CHECK(clear.solved);
        }
    }
    return test_summary("solvable");
}
