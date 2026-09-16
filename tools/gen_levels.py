#!/usr/bin/env python3
"""Generate candidate vault maps for tools/level_grade.c to score.

The 40 vaults added in 3.0.0 were picked from a pool this produced. Grading is the
expensive step (~12 s for an easy map, far longer for an unsolvable one), so this does a
cheap slide-model reachability pass first and only emits candidates that pass it. The
slide model is deliberately weaker than the real solver — it knows only full tilts, not
the partial pulses physics_step allows — so anything it can solve the real solver can
solve too, and it never filters out a map that would have been fine.

Nothing here decides whether a map ships: level_grade.c does, using the game's own
level_parse and the test suite's own solver.

Usage:
    python3 tools/gen_levels.py --count 400 --seed 3 --shards 10 --out DIR
"""
from __future__ import annotations

import argparse
import random
from collections import deque
from dataclasses import dataclass
from pathlib import Path

LEVEL_W, LEVEL_H = 24, 13
WALL, FLOOR, HOLE = "#", ".", "O"
DIRS = ((1, 0), (-1, 0), (0, 1), (0, -1))

Grid = list[list[str]]


def blank() -> Grid:
    """Open floor inside a solid border."""
    return [
        [WALL if x in (0, LEVEL_W - 1) or y in (0, LEVEL_H - 1) else FLOOR for x in range(LEVEL_W)]
        for y in range(LEVEL_H)
    ]


def interior() -> list[tuple[int, int]]:
    return [(x, y) for y in range(1, LEVEL_H - 1) for x in range(1, LEVEL_W - 1)]


# --- map families -------------------------------------------------------------------
# Each returns a grid of walls, floor and holes. Start and goal are placed afterwards.


def fam_lattice(rng: random.Random) -> Grid:
    """Pillars on a regular grid, the Lattice idiom: every move ends against a pillar."""
    g = blank()
    step_x = rng.choice((2, 2, 3))
    step_y = rng.choice((2, 2, 3))
    off = rng.randint(0, 1)
    for y in range(1 + off, LEVEL_H - 1, step_y):
        for x in range(1 + off, LEVEL_W - 1, step_x):
            if rng.random() < 0.88:
                g[y][x] = WALL
    return g


def fam_chambers(rng: random.Random) -> Grid:
    """Rooms divided by full walls with a doorway or two, the Four Chambers idiom."""
    g = blank()
    cuts_x = rng.sample(range(4, LEVEL_W - 4), rng.randint(1, 3))
    cuts_y = rng.sample(range(3, LEVEL_H - 3), rng.randint(1, 2))
    for x in cuts_x:
        for y in range(1, LEVEL_H - 1):
            g[y][x] = WALL
    for y in cuts_y:
        for x in range(1, LEVEL_W - 1):
            g[y][x] = WALL
    for x in cuts_x:
        for _ in range(rng.randint(1, 2)):
            g[rng.randrange(1, LEVEL_H - 1)][x] = FLOOR
    for y in cuts_y:
        for _ in range(rng.randint(1, 3)):
            g[y][rng.randrange(1, LEVEL_W - 1)] = FLOOR
    return g


def fam_comb(rng: random.Random) -> Grid:
    """Teeth hanging off one or both long edges, the Comb idiom."""
    g = blank()
    for x in range(2, LEVEL_W - 2, rng.choice((2, 3))):
        depth = rng.randint(3, LEVEL_H - 4)
        top = rng.random() < 0.5
        for d in range(depth):
            y = 1 + d if top else LEVEL_H - 2 - d
            g[y][x] = WALL
    return g


def fam_drift(rng: random.Random) -> Grid:
    """Scattered blocks in open space — the loosest family, and the one that most often
    grades easy, so it is generated with a higher hole budget to compensate."""
    g = blank()
    for _ in range(rng.randint(14, 30)):
        x, y = rng.randrange(1, LEVEL_W - 1), rng.randrange(1, LEVEL_H - 1)
        run = rng.randint(1, 3)
        horiz = rng.random() < 0.5
        for k in range(run):
            cx, cy = (x + k, y) if horiz else (x, y + k)
            if 1 <= cx < LEVEL_W - 1 and 1 <= cy < LEVEL_H - 1:
                g[cy][cx] = WALL
    return g


def fam_spiral(rng: random.Random) -> Grid:
    """Nested rectangles with one gap each, the Spiral idiom."""
    g = blank()
    inset = 2
    while inset < min(LEVEL_W, LEVEL_H) // 2:
        x0, x1 = inset, LEVEL_W - 1 - inset
        y0, y1 = inset, LEVEL_H - 1 - inset
        if x1 - x0 < 2 or y1 - y0 < 2:
            break
        for x in range(x0, x1 + 1):
            g[y0][x] = g[y1][x] = WALL
        for y in range(y0, y1 + 1):
            g[y][x0] = g[y][x1] = WALL
        side = rng.randrange(4)
        if side == 0:
            g[y0][rng.randint(x0 + 1, x1 - 1)] = FLOOR
        elif side == 1:
            g[y1][rng.randint(x0 + 1, x1 - 1)] = FLOOR
        elif side == 2:
            g[rng.randint(y0 + 1, y1 - 1)][x0] = FLOOR
        else:
            g[rng.randint(y0 + 1, y1 - 1)][x1] = FLOOR
        inset += 2
    return g


def fam_maze(rng: random.Random) -> Grid:
    """A real corridor maze: recursive backtracker on the odd cells, then a few walls
    knocked out so it is not a single forced thread.

    Route length is what actually buys difficulty — the solver's cost is roughly 10 to 45
    per move, so the shipped Pinball Gauntlet reaches cost 1740 on 39 moves while the
    much tighter Lattice only reaches 570 on 18. The scattered families top out around 18
    moves, which is why they could not produce anything harder than the existing 20."""
    g = [[WALL] * LEVEL_W for _ in range(LEVEL_H)]
    cells = [(x, y) for y in range(1, LEVEL_H - 1, 2) for x in range(1, LEVEL_W - 1, 2)]
    start = rng.choice(cells)
    seen = {start}
    stack = [start]
    g[start[1]][start[0]] = FLOOR
    while stack:
        cx, cy = stack[-1]
        nbrs = [
            (cx + dx * 2, cy + dy * 2)
            for dx, dy in DIRS
            if (cx + dx * 2, cy + dy * 2) in cells and (cx + dx * 2, cy + dy * 2) not in seen
        ]
        if not nbrs:
            stack.pop()
            continue
        nx, ny = rng.choice(nbrs)
        g[(cy + ny) // 2][(cx + nx) // 2] = FLOOR
        g[ny][nx] = FLOOR
        seen.add((nx, ny))
        stack.append((nx, ny))
    for _ in range(rng.randint(3, 10)):  # braid: a few loops make it less of a single thread
        x, y = rng.randrange(2, LEVEL_W - 2), rng.randrange(2, LEVEL_H - 2)
        g[y][x] = FLOOR
    return g


def fam_serpentine(rng: random.Random) -> Grid:
    """Alternating stubs that force the ball to switch back along the whole board."""
    g = blank()
    step = rng.choice((2, 3))
    top = rng.random() < 0.5
    for x in range(2, LEVEL_W - 2, step):
        for d in range(LEVEL_H - 3):
            y = 1 + d if top else LEVEL_H - 2 - d
            g[y][x] = WALL
        top = not top
    for _ in range(rng.randint(0, 4)):  # a gap or two so it is not purely mechanical
        g[rng.randrange(1, LEVEL_H - 1)][rng.randrange(2, LEVEL_W - 2)] = FLOOR
    return g


FAMILIES = {
    "lattice": fam_lattice,
    "chambers": fam_chambers,
    "comb": fam_comb,
    "drift": fam_drift,
    "spiral": fam_spiral,
    "maze": fam_maze,
    "serpentine": fam_serpentine,
}


# --- slide model --------------------------------------------------------------------


def slide(g: Grid, cx: int, cy: int, dx: int, dy: int, goal: tuple[int, int]):
    """One full tilt: roll until a wall stops it. Returns ("goal",), ("fell",) or the
    resting cell. A hole crossed on the way swallows the ball, which is what makes this
    weaker than the real solver — a real pulse could stop short of it."""
    x, y = cx, cy
    while True:
        nx, ny = x + dx, y + dy
        if g[ny][nx] == WALL:
            return ("rest", x, y)
        x, y = nx, ny
        if (x, y) == goal:
            return ("goal",)
        if g[y][x] == HOLE:
            return ("fell",)


def slide_distance(g: Grid, start: tuple[int, int], goal: tuple[int, int]) -> int:
    """Fewest full tilts from start to goal, or -1 if the goal cannot be reached."""
    seen = {start}
    q = deque([(start, 0)])
    while q:
        (cx, cy), d = q.popleft()
        for dx, dy in DIRS:
            r = slide(g, cx, cy, dx, dy, goal)
            if r[0] == "goal":
                return d + 1
            if r[0] == "fell":
                continue
            cell = (r[1], r[2])
            if cell not in seen:
                seen.add(cell)
                q.append((cell, d + 1))
    return -1


def pulse_distance(g: Grid, start: tuple[int, int], goal: tuple[int, int]) -> int:
    """Fewest moves when a tilt may also be cut short, which is what the real solver's
    2..24 frame pulses buy: the ball can stop anywhere along the ray, but still cannot
    cross a hole. Full-tilt-only reachability (slide_distance) turns out to be far too
    strict for open rooms — every chambers and spiral candidate failed it, while the
    shipped Four Chambers grades fine — so this is what decides whether a candidate is
    worth the cost of grading. Returns -1 if the goal cannot be reached."""
    seen = {start}
    q = deque([(start, 0)])
    while q:
        (cx, cy), d = q.popleft()
        for dx, dy in DIRS:
            x, y = cx, cy
            while True:
                x, y = x + dx, y + dy
                if g[y][x] == WALL:
                    break
                if (x, y) == goal:
                    return d + 1
                if g[y][x] == HOLE:
                    break  # a pulse can stop short of it, never roll over it
                if (x, y) not in seen:
                    seen.add((x, y))
                    q.append(((x, y), d + 1))
    return -1


@dataclass
class Candidate:
    name: str
    rows: list[str]
    family: str
    moves: int      # pulse_distance
    slides: int     # slide_distance, -1 when full tilts alone cannot do it
    holes: int


def build(rng: random.Random, family: str, index: int, min_moves: int = 6) -> Candidate | None:
    g = FAMILIES[family](rng)
    free = [(x, y) for (x, y) in interior() if g[y][x] == FLOOR]
    if len(free) < 30:
        return None

    # Start and goal far apart, so the route has to cross the board.
    for _ in range(40):
        sx, sy = rng.choice(free)
        gx, gy = rng.choice(free)
        if abs(sx - gx) + abs(sy - gy) >= 20:
            break
    else:
        return None
    if (sx, sy) == (gx, gy):
        return None

    hole_budget = rng.randint(6, 26 if family == "drift" else 18)
    guard = rng.randint(2, 4)
    protected = {(sx, sy), (gx, gy)}
    holes = 0
    for x, y in rng.sample(free, min(len(free), hole_budget * 3)):
        if holes >= hole_budget or (x, y) in protected:
            continue
        g[y][x] = HOLE
        if pulse_distance(g, (sx, sy), (gx, gy)) < 0:
            g[y][x] = FLOOR  # this hole seals the vault; leave it out
        else:
            holes += 1

    # Guard the goal. Both of the hardest shipped vaults — Four Chambers and Lattice, the
    # only two that reach tier 3 — sit their goal right beside a hole, so the last move has
    # to be exact or it is fatal. Scattered holes do not do this on their own: a pool of
    # 153 candidates without it produced nothing above difficulty 3542, and the ramp has to
    # start above 3570.
    for anchor in ((gx, gy), (sx, sy)):
        placed = 0
        for dx, dy in rng.sample([(a, b) for a in (-1, 0, 1) for b in (-1, 0, 1) if a or b], 8):
            if placed >= guard:
                break
            x, y = anchor[0] + dx, anchor[1] + dy
            if not (1 <= x < LEVEL_W - 1 and 1 <= y < LEVEL_H - 1):
                continue
            if g[y][x] != FLOOR or (x, y) in ((sx, sy), (gx, gy)):
                continue
            g[y][x] = HOLE
            if pulse_distance(g, (sx, sy), (gx, gy)) < 0:
                g[y][x] = FLOOR  # that was the last way in
            else:
                holes += 1
                placed += 1

    moves = pulse_distance(g, (sx, sy), (gx, gy))
    if moves < min_moves or holes < 4:
        return None

    rows = ["".join(r) for r in g]
    rows[sy] = rows[sy][:sx] + "S" + rows[sy][sx + 1 :]
    rows[gy] = rows[gy][:gx] + "G" + rows[gy][gx + 1 :]
    return Candidate(f"{family}-{index}", rows, family, moves, slide_distance(g, (sx, sy), (gx, gy)), holes)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--shards", type=int, default=10)
    ap.add_argument("--min-moves", type=int, default=6)
    ap.add_argument("--families", default="", help="comma-separated subset of the families")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    wanted = [f for f in (args.families.split(",") if args.families else list(FAMILIES)) if f]
    for f in wanted:
        if f not in FAMILIES:
            raise SystemExit(f"unknown family {f!r}; have {', '.join(FAMILIES)}")

    rng = random.Random(args.seed)
    pool: list[Candidate] = []
    tries = 0
    # Per family rather than round-robin over tries: the families differ by an order of
    # magnitude in how often they survive the filters, so an even share of tries gives a
    # wildly uneven pool, and variety is what keeps the 40 vaults from feeling alike.
    per_family = max(1, args.count // len(wanted))
    for family in wanted:
        made = 0
        cap = per_family * 400
        for _ in range(cap):
            tries += 1
            c = build(rng, family, len(pool), args.min_moves)
            if c:
                pool.append(c)
                made += 1
                if made >= per_family:
                    break

    args.out.mkdir(parents=True, exist_ok=True)
    shards = [[] for _ in range(args.shards)]
    for i, c in enumerate(pool):
        shards[i % args.shards].append(c)
    for i, shard in enumerate(shards):
        text = "".join("NAME " + c.name + "\n" + "\n".join(c.rows) + "\n" for c in shard)
        (args.out / f"cand{i:02d}.maps").write_text(text, encoding="utf-8", newline="\n")

    by_family: dict[str, int] = {}
    for c in pool:
        by_family[c.family] = by_family.get(c.family, 0) + 1
    print(f"{len(pool)} candidates in {tries} tries -> {args.out} ({args.shards} shards)")
    print("  by family:", by_family)
    print("  pulse-moves:", min(c.moves for c in pool), "..", max(c.moves for c in pool))
    print("  solvable by full tilts alone:", sum(1 for c in pool if c.slides >= 0), "/", len(pool))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
