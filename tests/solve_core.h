#ifndef GV_SOLVE_CORE_H
#define GV_SOLVE_CORE_H

#include "level.h"

/* Solvability + difficulty grading, driven through the real physics_step.
 *
 * A player's move is modelled as a straight tilt in one of 4 directions, fed through the
 * same low-pass filter and dead zone as motion.c: either a HOLD (tilt for 3 s, release,
 * coast to rest) or a PULSE of 2..24 frames then coast. Dijkstra over resting positions
 * (4 px bins) finds the cheapest route to the goal: a hold costs 10, a pulse 10 + 60 /
 * (how many neighbouring pulse lengths stop in the same cell).
 *
 * PRECISION is what makes a level hard for a player, so a move only counts at tolerance T
 * if it still gives the same outcome (same resting cell, or the goal) when the ball starts
 * T px off in any of the 4 directions. Each level is solved at T = 20, 16, 12, 8, 5, 3, 0 px
 * and graded by the loosest T that has a route (tier 0 = 20 px, easiest), then by route cost:
 * difficulty = tier * 1000 + cost.
 *
 * This lives apart from the test suite because tools/level_grade.c grades candidate mazes
 * with exactly the same code the suite judges the shipped ones with. A generator scored by
 * its own private copy of the rules would prove nothing about what the suite will say.
 *
 * Moving hazards are deliberately NOT modelled here. The level generator blanks every cell
 * a hazard sweeps to CELL_WALL before grading, so the route this finds is one that never
 * goes near a hazard. That keeps the guarantee simple and honest: every vault has a route
 * that does not depend on out-running anything. */

typedef struct {
    int solved, tier, cost, difficulty, moves, pulses, states;
    char route[512];
} Report;

/* Tolerances in px, loosest first; tier is an index into this. */
extern const int SOLVE_TOLERANCES[];
int solve_tier_count(void);

/* Cheapest route at one tolerance. rep.solved = 0 if there is none. */
Report solve_at(const Level *lv, int tol);

/* Loosest tolerance with a route decides the tier; difficulty = tier * 1000 + cost. */
Report grade(const Level *lv);

int count_holes(const Level *lv);

#endif
