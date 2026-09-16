#ifndef GV_LEVEL_H
#define GV_LEVEL_H

/* Board: 24 x 13 cells of 40 px = 960 x 520 px, drawn below a 24 px HUD strip. */
#define LEVEL_W      24
#define LEVEL_H      13
#define CELL_PX      40
#define BOARD_TOP_PX 24
#define LEVEL_COUNT 60

typedef enum {
    CELL_FLOOR = 0,
    CELL_WALL,
    CELL_HOLE,
    CELL_GOAL
} CellType;

/* A hazard slides back and forth along a straight patrol between two cells, forever, and
 * catches the ball on contact. Up to LEVEL_MAX_HAZARDS of them per vault, written into
 * the map as a lower-case letter and its capital: 'a' and 'A' are the two ends of one
 * patrol, 'b' and 'B' the next. Both ends are floor.
 *
 * They are the only thing in a vault that moves on its own, so they are kept out of the
 * physics step entirely: game.c asks level_hazard_hit() after physics_step() has run.
 * That leaves physics_step() a pure function of the ball and the static map, which is
 * what lets the solver in tests/solve_core.c drive it. */
#define LEVEL_MAX_HAZARDS 4
#define HAZARD_RADIUS_PX  12.0f
#define HAZARD_SPEED_PX   95.0f /* px/s along the patrol, both directions */

typedef struct {
    float x0, y0, x1, y1; /* board-space px, the two patrol ends (cell centres) */
    float length;         /* px between the ends; 0 for an unused slot */
} Hazard;

typedef struct {
    const char *name;
    unsigned char cells[LEVEL_H][LEVEL_W]; /* CellType values */
    float start_x, start_y;                /* board-space px, centre of the start cell */
    Hazard hazards[LEVEL_MAX_HAZARDS];
    int hazard_count;
} Level;

/* Build a level from LEVEL_H rows of LEVEL_W characters: '#' wall, '.' floor, 'O' hole,
 * 'G' goal, 'S' start. Returns 0, or -1 if a row is the wrong length, the border is not
 * solid wall, a character is unknown, or there is not exactly one 'S' and one 'G'.
 * `name` is stored by pointer and must outlive *out.
 *
 * Exposed so tools/level_grade.c judges a candidate map by the same rule the game uses;
 * a generator with its own idea of "valid" would emit maps level_load then rejects. */
int level_parse(const char *const *rows, const char *name, Level *out);

/* Build level `index` (0..LEVEL_COUNT-1) from its built-in ASCII map.
 * Returns 0 on success, -1 on bad index or malformed map. */
int level_load(int index, Level *out);

/* Cell at (cx, cy); anything outside the grid reads as CELL_WALL. */
CellType level_cell(const Level *lv, int cx, int cy);

/* Centre of hazard `h` at t seconds into the vault. It starts at (x0, y0) and bounces
 * between the ends at HAZARD_SPEED_PX. */
void level_hazard_pos(const Hazard *h, float t, float *out_x, float *out_y);

/* 1 if any hazard is touching a ball of radius ball_r centred at (bx, by) at time t.
 * ball_r is passed in so level.h does not have to know about physics.h. */
int level_hazard_hit(const Level *lv, float t, float bx, float by, float ball_r);

#endif
