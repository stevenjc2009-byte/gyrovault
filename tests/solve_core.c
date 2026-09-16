/* Implementation of the solver described in solve_core.h. Moved here verbatim from
 * test_solvable.c so tools/level_grade.c can score candidate mazes with the same code. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "physics.h"
#include "solve_core.h"

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

const int SOLVE_TOLERANCES[] = {20, 16, 12, 8, 5, 3, 0};
#define TIER_COUNT (int)(sizeof SOLVE_TOLERANCES / sizeof SOLVE_TOLERANCES[0])

int solve_tier_count(void)
{
    return TIER_COUNT;
}

static const int DIRX[4] = {1, -1, 0, 0}, DIRY[4] = {0, 0, 1, -1};
static const char DIRC[4] = {'R', 'L', 'D', 'U'};

typedef struct {
    float x, y;
    int cost, parent, frames, dir, done;
} Node;

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

Report solve_at(const Level *lv, int tol)
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
Report grade(const Level *lv)
{
    Report rep;
    int t;
    memset(&rep, 0, sizeof rep);
    for (t = 0; t < TIER_COUNT; t++) {
        rep = solve_at(lv, SOLVE_TOLERANCES[t]);
        if (rep.solved) {
            rep.tier = t;
            rep.difficulty = t * 1000 + rep.cost;
            break;
        }
    }
    return rep;
}

int count_holes(const Level *lv)
{
    int x, y, n = 0;
    for (y = 0; y < LEVEL_H; y++)
        for (x = 0; x < LEVEL_W; x++)
            n += lv->cells[y][x] == CELL_HOLE;
    return n;
}
