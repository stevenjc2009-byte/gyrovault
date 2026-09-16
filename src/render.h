#ifndef GV_RENDER_H
#define GV_RENDER_H

#include "level.h"
#include "physics.h"

#define SCREEN_W 960
#define SCREEN_H 544

/* Text sizes (PGF scale). */
#define TEXT_SMALL  0.8f
#define TEXT_NORMAL 1.0f
#define TEXT_BIG    1.6f
#define TEXT_TITLE  3.0f

int  render_init(void);   /* vita2d + default PGF font; 0 on success */
void render_shutdown(void);

void render_begin(void);  /* start drawing + clear */
void render_end(void);    /* end drawing + swap */

/* Backgrounds / primitives */
void render_steel_background(void);
void render_panel(float x, float y, float w, float h, int highlighted, int greyed);
void render_progress_bar(float x, float y, float w, float h, float frac);
void render_dim(unsigned char alpha);

/* Text: colour is RGBA8 (vita2d). Centered variant centres on cx. */
void  render_text(float x, float y, unsigned int color, float scale, const char *s);
void  render_text_centered(float cx, float y, unsigned int color, float scale, const char *s);
float render_text_width(float scale, const char *s);

/* Game board */
/* Pre-render the level's STATIC parts (floor, walls, holes) into an offscreen
 * texture; call once when a vault loads. 0 on success. If this hasn't been
 * called, or it failed, for the level currently being drawn, render_board()
 * falls back to drawing everything live. */
int  render_board_cache(const Level *lv);
/* time_s animates the goal glow and never stops; hazard_t is the current attempt's
 * elapsed time and places the hazards. They are separate because a hazard has to be
 * drawn exactly where game.c's collision test says it is -- see hazard_t in game.c. */
void render_board(const Level *lv, float time_s, float hazard_t);
void render_ball(const Ball *b, float scale);   /* scale < 1 = sinking into a hole */
void render_hud(const Level *lv, int level_index, const char *time_text); /* time_text NULL = no timer */

/* Round bubble-level indicator centred at (cx, cy) with outer radius `radius`.
 * tilt_x/tilt_y are the same [-1,1] tilt values passed to physics_step(). */
void render_tilt_gauge(float cx, float cy, float radius, float tilt_x, float tilt_y);

/* Full-screen white flash for level completion. t is seconds since the flash
 * started; draws nothing outside [0, RENDER_FLASH_DURATION). */
#define RENDER_FLASH_DURATION 0.6f
void render_flash(float t);

/* Small check mark icon, top-left at (x, y), size s. */
void render_check(float x, float y, float s, unsigned int color);

#endif
