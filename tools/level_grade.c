/* Grades candidate vault maps with the same solver the test suite uses.
 *
 * Input file: one or more maps, each a "NAME <name>" line followed by LEVEL_H rows of
 * LEVEL_W characters (# wall, . floor, O hole, G goal, S start). Blank lines and lines
 * starting with ';' are ignored between maps.
 *
 * Output: one line per map, fields separated by spaces —
 *     OK <tier> <tol> <cost> <difficulty> <moves> <pulses> <states> <holes> <name>
 *     BADMAP <name>    (level_parse rejected it)
 *     UNSOLVED <name>  (no route at any tolerance)
 * The name comes last because vault names contain spaces; a blank NAME becomes "-".
 *
 * Grading is the expensive step — measured at up to ~22 s for one hard map — so the
 * generator runs several of these at once over disjoint candidate files.
 *
 * Build (host, from the repo root):
 *   gcc -Isrc -Itests -Wall -Wextra -Werror -O2 -o build-host/level_grade \
 *       tools/level_grade.c tests/solve_core.c src/level.c src/physics.c -lm
 */
#include <stdio.h>
#include <string.h>

#include "level.h"
#include "solve_core.h"

#define MAX_NAME 64

/* Reads the next map into rows/name. Returns 1 on a map, 0 at end of file. */
static int read_map(FILE *f, char rows[LEVEL_H][LEVEL_W + 1], char *name)
{
    char line[256];
    int y = 0;

    name[0] = '\0';
    while (fgets(line, sizeof line, f)) {
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        if (len == 0 || line[0] == ';')
            continue;
        if (strncmp(line, "NAME ", 5) == 0) {
            size_t n = len - 5 < MAX_NAME - 1 ? len - 5 : MAX_NAME - 1;
            memcpy(name, line + 5, n);
            name[n] = '\0';
            y = 0;
            continue;
        }
        /* A row of the wrong length becomes a row level_parse rejects, rather than being
         * trimmed to fit — silently repairing it would hide a generator bug. */
        if (len == LEVEL_W) {
            memcpy(rows[y], line, LEVEL_W);
            rows[y][LEVEL_W] = '\0';
        } else {
            rows[y][0] = '?';
            rows[y][1] = '\0';
        }
        if (++y == LEVEL_H)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char rows[LEVEL_H][LEVEL_W + 1], name[MAX_NAME];
    const char *row_ptrs[LEVEL_H];
    FILE *f;
    int y;

    if (argc < 2) {
        fprintf(stderr, "usage: level_grade <maps-file>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "level_grade: cannot open %s\n", argv[1]);
        return 2;
    }
    for (y = 0; y < LEVEL_H; y++)
        row_ptrs[y] = rows[y];

    while (read_map(f, rows, name)) {
        Level lv;
        Report rep;
        const char *tag = name[0] ? name : "-";

        if (level_parse(row_ptrs, tag, &lv) != 0) {
            printf("BADMAP %s\n", tag);
            fflush(stdout);
            continue;
        }
        rep = grade(&lv);
        if (!rep.solved)
            printf("UNSOLVED %s\n", tag);
        else
            printf("OK %d %d %d %d %d %d %d %d %s\n", rep.tier, SOLVE_TOLERANCES[rep.tier],
                   rep.cost, rep.difficulty, rep.moves, rep.pulses, rep.states, count_holes(&lv),
                   tag);
        fflush(stdout);
    }
    fclose(f);
    return 0;
}
