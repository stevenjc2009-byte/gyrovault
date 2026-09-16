#include <string.h>

#include "level.h"
#include "test.h"

static int count_cells(const Level *lv, CellType t)
{
    int x, y, n = 0;
    for (y = 0; y < LEVEL_H; y++)
        for (x = 0; x < LEVEL_W; x++)
            if (lv->cells[y][x] == t)
                n++;
    return n;
}

static int border_all_walls(const Level *lv)
{
    int x, y;
    for (y = 0; y < LEVEL_H; y++)
        for (x = 0; x < LEVEL_W; x++)
            if ((x == 0 || y == 0 || x == LEVEL_W - 1 || y == LEVEL_H - 1) &&
                lv->cells[y][x] != CELL_WALL)
                return 0;
    return 1;
}

/* 4-connected BFS over floor/goal cells (holes and walls block). */
static int goal_reachable(const Level *lv)
{
    int qx[LEVEL_W * LEVEL_H], qy[LEVEL_W * LEVEL_H];
    unsigned char seen[LEVEL_H][LEVEL_W];
    int head = 0, tail = 0, i;
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};

    memset(seen, 0, sizeof(seen));
    qx[tail] = (int)lv->start_x / CELL_PX;
    qy[tail] = (int)lv->start_y / CELL_PX;
    seen[qy[tail]][qx[tail]] = 1;
    tail++;
    while (head < tail) {
        int x = qx[head], y = qy[head];
        head++;
        if (level_cell(lv, x, y) == CELL_GOAL)
            return 1;
        for (i = 0; i < 4; i++) {
            int nx = x + DX[i], ny = y + DY[i];
            CellType c = level_cell(lv, nx, ny);
            if ((c == CELL_FLOOR || c == CELL_GOAL) && !seen[ny][nx]) {
                seen[ny][nx] = 1;
                qx[tail] = nx;
                qy[tail] = ny;
                tail++;
            }
        }
    }
    return 0;
}

int main(void)
{
    Level lv[LEVEL_COUNT];
    int i, x, y, diff = 0;

    for (i = 0; i < LEVEL_COUNT; i++) {
        int scx, scy;
        memset(&lv[i], 0xAB, sizeof(lv[i]));
        CHECK(level_load(i, &lv[i]) == 0);
        CHECK(lv[i].name != NULL && strlen(lv[i].name) > 0);
        CHECK(count_cells(&lv[i], CELL_GOAL) == 1);
        CHECK(count_cells(&lv[i], CELL_FLOOR) + count_cells(&lv[i], CELL_WALL) +
                  count_cells(&lv[i], CELL_HOLE) + count_cells(&lv[i], CELL_GOAL) ==
              LEVEL_W * LEVEL_H);
        CHECK(border_all_walls(&lv[i]));
        scx = (int)lv[i].start_x / CELL_PX;
        scy = (int)lv[i].start_y / CELL_PX;
        CHECK(lv[i].start_x == (float)(scx * CELL_PX + 20));
        CHECK(lv[i].start_y == (float)(scy * CELL_PX + 20));
        CHECK(level_cell(&lv[i], scx, scy) == CELL_FLOOR);
        CHECK(goal_reachable(&lv[i]));
    }

    /* Dims: the 24x13 board is 960x520 px. */
    CHECK(LEVEL_W * CELL_PX == 960 && LEVEL_H * CELL_PX == 520);

    /* 60 vaults, all different: every pair has its own name, and inside the border more than
     * half of the cells that hold a wall, pit or goal in either vault differ between them.
     * (Shared open floor is ignored: two open maps match on floor without being alike.)
     * The easy-to-hard ramp is proven with real physics in test_solvable. */
    CHECK(LEVEL_COUNT == 60);
    {
        int j, used, least_pct = 100;
        for (i = 0; i < LEVEL_COUNT; i++)
            for (j = i + 1; j < LEVEL_COUNT; j++) {
                diff = used = 0;
                for (y = 1; y < LEVEL_H - 1; y++)
                    for (x = 1; x < LEVEL_W - 1; x++) {
                        CellType a = lv[i].cells[y][x], b = lv[j].cells[y][x];
                        if (a == CELL_FLOOR && b == CELL_FLOOR)
                            continue;
                        used++;
                        if (a != b)
                            diff++;
                    }
                if (used > 0 && diff * 100 / used < least_pct)
                    least_pct = diff * 100 / used;
                if (diff * 2 <= used)
                    printf("  levels %d and %d share their layout: only %d/%d features differ\n",
                           i + 1, j + 1, diff, used);
                CHECK(diff * 2 > used);
                CHECK(strcmp(lv[i].name, lv[j].name) != 0);
            }
        printf("  least-different pair of vaults: %d%% of features differ\n", least_pct);
    }
    /* Vault 1 stays the gentle opener it shipped as. */
    CHECK(count_cells(&lv[0], CELL_HOLE) >= 4 && count_cells(&lv[0], CELL_HOLE) <= 6);
    CHECK(strcmp(lv[0].name, "The First Vault") == 0);

    /* Out of range reads as wall. */
    CHECK(level_cell(&lv[0], -1, 5) == CELL_WALL);
    CHECK(level_cell(&lv[0], 5, -1) == CELL_WALL);
    CHECK(level_cell(&lv[0], LEVEL_W, 5) == CELL_WALL);
    CHECK(level_cell(&lv[0], 5, LEVEL_H) == CELL_WALL);
    CHECK(level_cell(NULL, 5, 5) == CELL_WALL);

    /* Bad index. */
    CHECK(level_load(-1, &lv[0]) == -1);
    CHECK(level_load(LEVEL_COUNT, &lv[0]) == -1);
    CHECK(level_load(0, NULL) == -1);

    return test_summary("level");
}
