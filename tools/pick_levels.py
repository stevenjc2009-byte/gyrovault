#!/usr/bin/env python3
"""Pick the vaults to ship from a graded candidate pool, and splice them into src/level.c.

What has to be true of the result, all of it enforced by the test suite:

  * tests/test_solvable.c asserts CHECK(rep.difficulty > prev) for every vault, so the
    whole 60 must form a strictly rising measured-difficulty sequence. The 20 that already
    ship run 1216 .. 3570 and are not touched, so every new vault must exceed 3570 and
    each other.
  * tests/test_level.c requires every pair of vaults to differ in more than half of the
    cells that hold a wall, pit or goal in either of them -- shared open floor does not
    count. Candidates from the same family look alike, so this is the binding constraint
    on variety, not difficulty.
  * level_parse refuses a malformed map or a bad hazard patrol.

Selection spreads the picks evenly across the eligible difficulty range rather than taking
the 40 easiest above 3570: the easy end of the range is dominated by one family, and an
even spread buys both a wider ramp and a mix of layouts.

Usage:
    python3 tools/pick_levels.py --graded DIR --cands DIR --write
"""
from __future__ import annotations

import argparse
import random
import re
from pathlib import Path

LEVEL_W, LEVEL_H = 24, 13
NEW_COUNT = 40
HAZARD_COUNT = 10
MIN_DIFFICULTY = 3570  # the hardest shipped vault, Lattice

# Vaults that get a moving hazard are named for something that moves. They are spread
# evenly through the 40 rather than clustered at the end.
MOVING_NAMES = [
    "Tumbler", "Chicane", "Ratline", "Carousel", "Slipstream",
    "Paternoster", "The Escapement", "Cataract", "Pendulum", "Clockwork",
]
STILL_NAMES = [
    "Dovetail", "Cross Section", "The Sieve", "Ribcage", "Split Decision",
    "Widow's Walk", "The Culvert", "Dead Reckoning", "Cat's Cradle", "The Weir",
    "Harrow", "Needlepoint", "The Drawbridge", "Portcullis", "Stepwell",
    "The Bottleneck", "Cross Hatch", "Rookery", "The Gantry", "The Aqueduct",
    "Cloister", "The Foundry", "The Scaffold", "Vice Grip", "The Undercroft",
    "Snake Eyes", "Deadbolt", "The Long Drop", "Strongroom", "The Final Vault",
]


def features(rows: list[str]) -> list[str]:
    """The interior cells, with start and goal normalised away so two maps are compared on
    layout. Mirrors what test_level.c walks."""
    out = []
    for y in range(1, LEVEL_H - 1):
        for x in range(1, LEVEL_W - 1):
            c = rows[y][x]
            out.append("." if c in "Sabcd" or c in "ABCD" else c)
    return out


def distinct(a: list[str], b: list[str]) -> tuple[bool, int, int]:
    """test_level.c's rule: of the cells holding a wall, pit or goal in either map, more
    than half must differ."""
    used = diff = 0
    for ca, cb in zip(a, b):
        if ca == "." and cb == ".":
            continue
        used += 1
        if ca != cb:
            diff += 1
    return (diff * 2 > used, diff, used)


def read_maps(path: Path) -> dict[str, list[str]]:
    maps: dict[str, list[str]] = {}
    name, rows = None, []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("NAME "):
            name, rows = line[5:], []
            maps[name] = rows
        elif name is not None and line:
            rows.append(line)
    return maps


def straight_runs(rows: list[str]) -> list[tuple[int, int, int, int]]:
    """Every maximal horizontal or vertical run of plain floor, as (x0, y0, x1, y1).
    A hazard patrol has to be one of these: level_parse refuses a patrol that crosses
    anything but floor."""
    runs = []
    for y in range(1, LEVEL_H - 1):
        x = 1
        while x < LEVEL_W - 1:
            if rows[y][x] != ".":
                x += 1
                continue
            x0 = x
            while x < LEVEL_W - 1 and rows[y][x] == ".":
                x += 1
            if x - x0 >= 4:
                runs.append((x0, y, x - 1, y))
    for x in range(1, LEVEL_W - 1):
        y = 1
        while y < LEVEL_H - 1:
            if rows[y][x] != ".":
                y += 1
                continue
            y0 = y
            while y < LEVEL_H - 1 and rows[y][x] == ".":
                y += 1
            if y - y0 >= 4:
                runs.append((x, y0, x, y - 1))
    return runs


CELL_PX = 40
HAZARD_REACH = 12.0 + 14.0  # HAZARD_RADIUS_PX + BALL_RADIUS, from src/level.h and physics.h


def swept(run: tuple[int, int, int, int]) -> set[tuple[int, int]]:
    """The cells tests/test_solvable.c walls off for this patrol: a ball parked at the
    cell centre would be touching the hazard somewhere along its travel. Same segment
    distance the C does, so the two agree on which cells count."""
    cx0, cy0 = run[0] * CELL_PX + CELL_PX / 2, run[1] * CELL_PX + CELL_PX / 2
    cx1, cy1 = run[2] * CELL_PX + CELL_PX / 2, run[3] * CELL_PX + CELL_PX / 2
    dx, dy = cx1 - cx0, cy1 - cy0
    len2 = dx * dx + dy * dy
    out = set()
    for y in range(1, LEVEL_H - 1):
        for x in range(1, LEVEL_W - 1):
            px, py = x * CELL_PX + CELL_PX / 2, y * CELL_PX + CELL_PX / 2
            t = (((px - cx0) * dx + (py - cy0) * dy) / len2) if len2 else 0.0
            t = min(1.0, max(0.0, t))
            ox, oy = px - (cx0 + t * dx), py - (cy0 + t * dy)
            if ox * ox + oy * oy <= HAZARD_REACH * HAZARD_REACH:
                out.add((x, y))
    return out


def reaches_goal(rows: list[str], blocked: set[tuple[int, int]]) -> bool:
    """Flood fill from 'S' to 'G' over plain floor, with `blocked` treated as wall. Holes
    stop the fill because the ball cannot roll over one. This is a necessary condition for
    the real solver, which moves in slides and so is strictly more restricted -- it is a
    prefilter, not the gate. The gate is tests/test_solvable.c."""
    start = goal = None
    for y in range(LEVEL_H):
        for x in range(LEVEL_W):
            if rows[y][x] == "S":
                start = (x, y)
            elif rows[y][x] == "G":
                goal = (x, y)
    if start is None or goal is None or start in blocked or goal in blocked:
        return False
    seen, stack = {start}, [start]
    while stack:
        x, y = stack.pop()
        if (x, y) == goal:
            return True
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (nx, ny) in seen or (nx, ny) in blocked:
                continue
            if not (0 <= nx < LEVEL_W and 0 <= ny < LEVEL_H):
                continue
            if rows[ny][nx] in "#O":
                continue
            seen.add((nx, ny))
            stack.append((nx, ny))
    return False


def place_hazard(rows: list[str], rng: random.Random) -> list[str] | None:
    """Write one patrol into the map as 'a' and 'A'. Longest run first, but only a run the
    ball can still get past: a patrol that seals the only corridor would fail the
    hazard-clearance check in tests/test_solvable.c. Returns None if no run works."""
    runs = sorted(straight_runs(rows), key=lambda r: (r[2] - r[0]) + (r[3] - r[1]), reverse=True)
    for run in runs:
        if not reaches_goal(rows, swept(run)):
            continue
        x0, y0, x1, y1 = run
        out = [list(r) for r in rows]
        out[y0][x0] = "a"
        out[y1][x1] = "A"
        return ["".join(r) for r in out]
    return None


def c_block(name: str, rows: list[str]) -> str:
    body = "".join(f'        "{r}",\n' for r in rows)
    return f"    {{ /* {name} */\n{body}    }},\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--graded", type=Path, required=True)
    ap.add_argument("--cands", type=Path, required=True, nargs="+",
                    help="one or more candidate directories; names must be unique across them")
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--seed", type=int, default=5)
    ap.add_argument("--write", action="store_true", help="splice into src/level.c and level.h")
    args = ap.parse_args()
    rng = random.Random(args.seed)

    maps: dict[str, list[str]] = {}
    for d in args.cands:
        for f in sorted(d.glob("*.maps")):
            for name, rows in read_maps(f).items():
                if name in maps:
                    raise SystemExit(f"two candidates are both called {name!r}; regenerate "
                                     f"one pool with a family it does not share")
                maps[name] = rows

    graded = []
    for f in sorted(args.graded.glob("*.txt")):
        for line in f.read_text(encoding="utf-8").splitlines():
            p = line.split()
            if p and p[0] == "OK":
                name = " ".join(p[9:])
                if int(p[4]) > MIN_DIFFICULTY and name in maps:
                    graded.append((int(p[4]), name))
    graded.sort()
    print(f"eligible (difficulty > {MIN_DIFFICULTY}): {len(graded)} of {len(maps)} candidates")
    if len(graded) < NEW_COUNT:
        print("NOT ENOUGH -- grade a bigger pool")
        return 1

    # The 20 shipped vaults, so the new ones are checked for distinctness against them too.
    level_c = (args.repo / "src" / "level.c").read_bytes().decode("utf-8")
    shipped_rows = re.findall(r'"((?:[^"\\]|\\.)*)"', block_after(level_c, "LEVEL_MAPS["))
    shipped = [features(shipped_rows[i * LEVEL_H : (i + 1) * LEVEL_H]) for i in range(len(shipped_rows) // LEVEL_H)]
    print(f"shipped vaults read back from src/level.c: {len(shipped)}")

    # Two passes, because doing it in one does not work. Walking the eligible list at even
    # intervals and taking the first distinct candidate at or after each target found only
    # 37 of the 40 from a pool that holds 79: the low end of the range is dominated by one
    # family, whose members collide with each other, so a target landing there burns its
    # search on maps that can never be taken.
    #
    # Pass one instead takes every candidate that is distinct from the 20 shipped vaults
    # and from everything already taken, in rising difficulty order. That is the largest
    # set this greedy rule can reach, and because it walks in order it is strictly rising
    # for free.
    pool: list[tuple[int, str, list[str]]] = []
    pool_feats: list[list[str]] = []
    for diff_val, name in graded:
        # Equal scores are common -- difficulty is tier * 1000 + an integer cost -- and
        # test_solvable.c wants each vault strictly harder than the one before, so a tie
        # with the last one taken is no more usable than a collision.
        if pool and diff_val <= pool[-1][0]:
            continue
        feats = features(maps[name])
        if all(distinct(feats, o)[0] for o in shipped + pool_feats):
            pool.append((diff_val, name, maps[name]))
            pool_feats.append(feats)
    print(f"mutually distinct: {len(pool)} of {len(graded)} eligible")

    # Pass two spreads the 40 evenly across that set, so the ramp covers the whole range
    # rather than bunching at whichever end happens to be crowded.
    if len(pool) < NEW_COUNT:
        print(f"only found {len(pool)} distinct candidates, need {NEW_COUNT}")
        return 1
    picks = sorted({round(k * (len(pool) - 1) / (NEW_COUNT - 1)) for k in range(NEW_COUNT)})
    while len(picks) < NEW_COUNT:  # rounding can collide; fill from whatever is left
        picks = sorted(set(picks) | {next(i for i in range(len(pool)) if i not in picks)})
    chosen = [pool[i] for i in picks]
    if len(chosen) != NEW_COUNT:
        print(f"only found {len(chosen)} distinct candidates, need {NEW_COUNT}")
        return 1

    # Hazards on every fourth vault, so they are spread through the run rather than bunched.
    haz_slots = [i for i in range(NEW_COUNT) if i % (NEW_COUNT // HAZARD_COUNT) == 2]
    still = list(STILL_NAMES)
    moving = list(MOVING_NAMES)
    final: list[tuple[str, list[str], bool]] = []
    for i, (diff_val, cand, rows) in enumerate(chosen):
        want_haz = i in haz_slots
        placed = place_hazard(rows, rng) if want_haz else None
        if want_haz and placed is None:
            print(f"  slot {i + 21}: no room for a patrol in {cand}, shipping it without one")
        name = moving.pop(0) if placed else still.pop(0)
        final.append((name, placed or rows, placed is not None))

    print(f"\n{'#':>3}  {'name':22} {'diff':>5} {'hazard':>6}  from")
    for i, (name, rows, haz) in enumerate(final):
        print(f"{i + 21:>3}  {name:22} {chosen[i][0]:>5} {'yes' if haz else '':>6}  {chosen[i][1]}")
    print(f"\nhazards placed: {sum(1 for _, _, h in final if h)}")
    print(f"difficulty span: {chosen[0][0]} .. {chosen[-1][0]}  (strictly rising: "
          f"{all(chosen[i][0] < chosen[i + 1][0] for i in range(len(chosen) - 1))})")

    if not args.write:
        print("\n(dry run; pass --write to splice into src/level.c)")
        return 0

    names_c = "".join(f'    "{n}",\n' for n, _, _ in final)
    maps_c = "".join(c_block(n, r) for n, r, _ in final)
    splice(args.repo / "src" / "level.c", names_c, maps_c)
    bump_count(args.repo / "src" / "level.h", len(shipped) + NEW_COUNT)
    print(f"\nsrc/level.c spliced, LEVEL_COUNT -> {len(shipped) + NEW_COUNT}")
    return 0


def block_after(src: str, marker: str) -> str:
    i = src.index(marker)
    depth = 0
    start = src.index("{", i)
    for j in range(start, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[start : j + 1]
    raise ValueError(f"unterminated block after {marker}")


def splice(path: Path, names_c: str, maps_c: str) -> None:
    """Append to both tables. Read and written as bytes with the file's own line endings
    preserved -- src/level.c is CRLF in the working tree, and Python's text mode would
    turn every existing "\\r\\n" into "\\r\\r\\n"."""
    raw = path.read_bytes()
    crlf = b"\r\n" in raw
    src = raw.decode("utf-8").replace("\r\n", "\n")

    for marker, addition in (("LEVEL_NAMES[", names_c), ("LEVEL_MAPS[", maps_c)):
        block = block_after(src, marker)
        end = src.index(block) + len(block)
        assert src[end - 2 : end] == "\n}", f"unexpected end of {marker} block"
        src = src[: end - 1] + addition + src[end - 1 :]

    out = src.replace("\n", "\r\n") if crlf else src
    path.write_bytes(out.encode("utf-8"))


def bump_count(path: Path, count: int) -> None:
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    new, n = re.subn(r"#define LEVEL_COUNT \d+", f"#define LEVEL_COUNT {count}", text)
    assert n == 1, f"expected one LEVEL_COUNT define, found {n}"
    path.write_bytes(new.encode("utf-8"))


if __name__ == "__main__":
    raise SystemExit(main())
