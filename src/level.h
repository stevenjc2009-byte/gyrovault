#ifndef GV_LEVEL_H
#define GV_LEVEL_H

/* Board: 24 x 13 cells of 40 px = 960 x 520 px, drawn below a 24 px HUD strip. */
#define LEVEL_W      24
#define LEVEL_H      13
#define CELL_PX      40
#define BOARD_TOP_PX 24
#define LEVEL_COUNT 20

typedef enum {
    CELL_FLOOR = 0,
    CELL_WALL,
    CELL_HOLE,
    CELL_GOAL
} CellType;

typedef struct {
    const char *name;
    unsigned char cells[LEVEL_H][LEVEL_W]; /* CellType values */
    float start_x, start_y;                /* board-space px, centre of the start cell */
} Level;

/* Build level `index` (0..LEVEL_COUNT-1) from its built-in ASCII map.
 * Returns 0 on success, -1 on bad index or malformed map. */
int level_load(int index, Level *out);

/* Cell at (cx, cy); anything outside the grid reads as CELL_WALL. */
CellType level_cell(const Level *lv, int cx, int cy);

#endif
