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
void render_board(const Level *lv, float time_s);
void render_ball(const Ball *b, float scale);   /* scale < 1 = sinking into a hole */
void render_hud(const Level *lv, int level_index);

/* Small check mark icon, top-left at (x, y), size s. */
void render_check(float x, float y, float s, unsigned int color);

#endif
