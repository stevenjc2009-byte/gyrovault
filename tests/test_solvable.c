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
#include "solve_core.h"
#include "test.h"

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
    }
    return test_summary("solvable");
}
