/* Solvability + difficulty ramp for every level, driven through the real physics_step.
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
 * Fails if any level has no route, or (when every level is run) if difficulty does not
 * rise from each level to the next. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "physics.h"
#include "test.h"

#define DT            (1.0f / 60.0f)
#define FILTER_ALPHA  0.25f /* mirrors motion.c */
#define DEAD_ZONE     0.05f /* mirrors motion.c */
#define HOLD_FRAMES   180
#define PULSE_MIN     2
#define PULSE_MAX     24
#define COAST_MAX     600
#define BIN_PX        4.0f
#define MAX_STATES    60000
#define HASH_SIZE     (1 << 18)
#define COST_HOLD     10
#define COST_PULSE    10 /* plus 60 / window_frames */
#define UNUSABLE      (-1)

static const int TOLERANCES[] = {20, 16, 12, 8, 5, 3, 0};
#define TIER_COUNT (int)(sizeof TOLERANCES / sizeof TOLERANCES[0])

static const int DIRX[4] = {1, -1, 0, 0}, DIRY[4] = {0, 0, 1, -1};
static const char DIRC[4] = {'R', 'L', 'D', 'U'};

typedef struct {
    float x, y;
    int cost, parent, frames, dir, done;
} Node;

typedef struct {
    int solved, tier, cost, difficulty, moves, pulses, states;
    char route[512];
} Report;

static Node nodes[MAX_STATES];
static int n_nodes;
static int hash_tab[HASH_SIZE]; /* node index + 1, 0 = empty */

static float dead_zone(float v)
{
    float mag = v < 0.0f ? -v : v;
    if (mag < DEAD_ZONE)
        return 0.0f;
    mag = (mag - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    return v < 0.0f ? -mag : mag;
}

/* Tilt toward dir for on_frames, then release and coast. *out holds the resting ball when
 * the result is PHYS_ROLLING. */
static PhysResult simulate(const Level *lv, float x, float y, int dir, int on_frames, Ball *out)
{
    Ball b = {x, y, 0.0f, 0.0f};
    float filt = 0.0f;
    int f;

    for (f = 0; f < on_frames + COAST_MAX; f++) {
        float target = f < on_frames ? 1.0f : 0.0f, t;
        PhysResult r;
        filt += FILTER_ALPHA * (target - filt);
        t = dead_zone(filt);
        r = physics_step(&b, lv, t * (float)DIRX[dir], t * (float)DIRY[dir], DT, NULL);
        if (r != PHYS_ROLLING)
            return r;
        if (f >= on_frames && t == 0.0f && b.vx == 0.0f && b.vy == 0.0f)
            break;
    }
    *out = b;
    return PHYS_ROLLING;
}

static int cell_key(float x, float y)
{
    return (int)(x / CELL_PX) * 64 + (int)(y / CELL_PX);
}

/* The move's outcome if it is the same from the ball's spot and from tol px off in each
 * of the 4 directions; UNUSABLE if any of those falls or stops in a different cell. */
static int robust_move(const Level *lv, float x, float y, int dir, int frames, int tol, Ball *out)
{
    static const int OX[4] = {1, -1, 0, 0}, OY[4] = {0, 0, 1, -1};
    PhysResult r = simulate(lv, x, y, dir, frames, out);
    int k;

    if (r == PHYS_FELL)
        return UNUSABLE;
    for (k = 0; tol > 0 && k < 4; k++) {
        Ball e;
        PhysResult r2 = simulate(lv, x + (float)(OX[k] * tol), y + (float)(OY[k] * tol), dir, frames, &e);
        if (r2 != r || (r == PHYS_ROLLING && cell_key(e.x, e.y) != cell_key(out->x, out->y)))
            return UNUSABLE;
    }
    return (int)r;
}

static int bin_key(float x, float y)
{
    return (int)floor(x / BIN_PX) * 1024 + (int)floor(y / BIN_PX) + 1;
}

static int find_or_add(float x, float y)
{
    int key = bin_key(x, y);
    unsigned h = (unsigned)key * 2654435761u;
    for (;;) {
        int slot = (int)(h & (HASH_SIZE - 1));
        int id = hash_tab[slot];
        if (id == 0) {
            if (n_nodes >= MAX_STATES - 1) /* last slot is the goal pseudo-node */
                return -1;
            hash_tab[slot] = n_nodes + 1;
            memset(&nodes[n_nodes], 0, sizeof nodes[n_nodes]);
            nodes[n_nodes].x = x;
            nodes[n_nodes].y = y;
            nodes[n_nodes].cost = 1 << 30;
            nodes[n_nodes].parent = -1;
            return n_nodes++;
        }
        if (bin_key(nodes[id - 1].x, nodes[id - 1].y) == key)
            return id - 1;
        h++;
    }
}

static void relax(int from, int to_cost, int dir, int frames, int result, float x, float y,
                  int *goal_cost)
{
    int id = result == PHYS_GOAL ? MAX_STATES - 1 : find_or_add(x, y);
    if (id < 0)
        return;
    if (result == PHYS_GOAL) {
        if (to_cost >= *goal_cost)
            return;
        *goal_cost = to_cost;
    } else if (to_cost >= nodes[id].cost || nodes[id].done) {
        return;
    }
    nodes[id].cost = to_cost;
    nodes[id].parent = from;
    nodes[id].dir = dir;
    nodes[id].frames = frames;
}

static Report solve_at(const Level *lv, int tol)
{
    Report rep;
    int goal_cost = 1 << 30, start;

    memset(&rep, 0, sizeof rep);
    memset(hash_tab, 0, sizeof hash_tab);
    n_nodes = 0;
    start = find_or_add(lv->start_x, lv->start_y);
    nodes[start].cost = 0;

    for (;;) {
        int best = -1, i, dir;
        for (i = 0; i < n_nodes; i++) /* linear scan: state counts stay in the thousands */
            if (!nodes[i].done && (best < 0 || nodes[i].cost < nodes[best].cost))
                best = i;
        if (best < 0 || nodes[best].cost >= goal_cost)
            break;
        nodes[best].done = 1;

        for (dir = 0; dir < 4; dir++) {
            float bx = nodes[best].x, by = nodes[best].y;
            Ball end, ends[PULSE_MAX + 1];
            int r[PULSE_MAX + 1], f, run_start;

            r[0] = robust_move(lv, bx, by, dir, HOLD_FRAMES, tol, &end);
            if (r[0] != UNUSABLE)
                relax(best, nodes[best].cost + COST_HOLD, dir, HOLD_FRAMES, r[0], end.x, end.y,
                      &goal_cost);

            for (f = PULSE_MIN; f <= PULSE_MAX; f++)
                r[f] = robust_move(lv, bx, by, dir, f, tol, &ends[f]);

            /* Consecutive pulse lengths with the same outcome cell form one window; its
             * middle length becomes the edge. */
            run_start = PULSE_MIN;
            for (f = PULSE_MIN + 1; f <= PULSE_MAX + 1; f++) {
                int same = f <= PULSE_MAX && r[f] == r[run_start] &&
                           (r[f] != PHYS_ROLLING ||
                            cell_key(ends[f].x, ends[f].y) == cell_key(ends[run_start].x, ends[run_start].y));
                if (!same) {
                    int w = f - run_start, mid = run_start + w / 2;
                    if (r[run_start] != UNUSABLE)
                        relax(best, nodes[best].cost + COST_PULSE + 60 / w, dir, mid, r[run_start],
                              ends[mid].x, ends[mid].y, &goal_cost);
                    run_start = f;
                }
            }
        }
    }

    rep.states = n_nodes;
    if (goal_cost < (1 << 30)) {
        int path[512], len = 0, i, id = MAX_STATES - 1;
        size_t pos = 0;
        while (id != start && len < 512) {
            path[len++] = id;
            id = nodes[id].parent;
        }
        rep.solved = 1;
        rep.cost = goal_cost;
        rep.moves = len;
        for (i = len - 1; i >= 0; i--) {
            const Node *nd = &nodes[path[i]];
            int wrote = nd->frames == HOLD_FRAMES
                            ? snprintf(rep.route + pos, sizeof rep.route - pos, "%c ", DIRC[nd->dir])
                            : snprintf(rep.route + pos, sizeof rep.route - pos, "%c%d ", DIRC[nd->dir], nd->frames);
            rep.pulses += nd->frames != HOLD_FRAMES;
            if (wrote > 0 && (size_t)wrote < sizeof rep.route - pos)
                pos += (size_t)wrote;
        }
    }
    return rep;
}

/* Loosest tolerance with a route decides the tier. */
static Report grade(const Level *lv)
{
    Report rep;
    int t;
    memset(&rep, 0, sizeof rep);
    for (t = 0; t < TIER_COUNT; t++) {
        rep = solve_at(lv, TOLERANCES[t]);
        if (rep.solved) {
            rep.tier = t;
            rep.difficulty = t * 1000 + rep.cost;
            break;
        }
    }
    return rep;
}

static int count_holes(const Level *lv)
{
    int x, y, n = 0;
    for (y = 0; y < LEVEL_H; y++)
        for (x = 0; x < LEVEL_W; x++)
            n += lv->cells[y][x] == CELL_HOLE;
    return n;
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

    printf("  %-3s %-22s %5s %4s %5s %5s %6s %6s %6s  route\n", "#", "name", "holes", "tol",
           "cost", "diff", "moves", "pulses", "states");
    for (i = 0; i < LEVEL_COUNT; i++) {
        Report rep;
        if (only >= 0 && i != only)
            continue;
        CHECK(level_load(i, &lv) == 0);
        rep = grade(&lv);
        printf("  %-3d %-22s %5d %4d %5d %5d %6d %6d %6d  %s\n", i + 1, lv.name, count_holes(&lv),
               rep.solved ? TOLERANCES[rep.tier] : -1, rep.solved ? rep.cost : -1,
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
