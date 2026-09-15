/* All-procedural vita2d rendering: steel menus, wooden board, steel walls, holes, chrome ball. */
#include <math.h>
#include <stdio.h>
#include <vita2d.h>

#include "render.h"

#define POOL_SIZE     (4 * 1024 * 1024)
#define HOLE_RADIUS   (CELL_PX * 0.42f)
#define BEVEL         3.0f

static vita2d_pgf *font = NULL;

int render_init(void)
{
    if (vita2d_init_advanced(POOL_SIZE) < 0)
        return -1;
    vita2d_set_clear_color(RGBA8(12, 13, 16, 255));
    vita2d_set_vblank_wait(1);
    font = vita2d_load_default_pgf();
    return font ? 0 : -1;
}

void render_shutdown(void)
{
    vita2d_wait_rendering_done();
    if (font) {
        vita2d_free_pgf(font);
        font = NULL;
    }
    vita2d_fini();
}

void render_begin(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void render_end(void)
{
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

/* ---------- helpers ---------- */

static unsigned char mix8(int a, int b, float t)
{
    float v = a + (b - a) * t;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (unsigned char)v;
}

static unsigned int mix_rgb(int r0, int g0, int b0, int r1, int g1, int b1, float t, int alpha)
{
    return RGBA8(mix8(r0, r1, t), mix8(g0, g1, t), mix8(b0, b1, t), alpha);
}

/* ---------- text ---------- */

float render_text_width(float scale, const char *s)
{
    return font ? (float)vita2d_pgf_text_width(font, scale, s) : 0.0f;
}

/* (x, y) is the TOP-left of the text; vita2d's pgf y is the baseline. */
void render_text(float x, float y, unsigned int color, float scale, const char *s)
{
    if (!font)
        return;
    float h = (float)vita2d_pgf_text_height(font, scale, s);
    float baseline = y + h * 0.82f;
    vita2d_pgf_draw_text(font, (int)(x + 2), (int)(baseline + 2), RGBA8(0, 0, 0, 150), scale, s);
    vita2d_pgf_draw_text(font, (int)x, (int)baseline, color, scale, s);
}

void render_text_centered(float cx, float y, unsigned int color, float scale, const char *s)
{
    render_text(cx - render_text_width(scale, s) * 0.5f, y, color, scale, s);
}

/* ---------- menus ---------- */

void render_steel_background(void)
{
    const int bands = 34;
    float bh = (float)SCREEN_H / bands;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (bands - 1);
        vita2d_draw_rectangle(0, i * bh, SCREEN_W, bh + 1, mix_rgb(46, 50, 60, 12, 13, 17, t, 255));
    }
    /* brushed streaks */
    for (int y = 0; y < SCREEN_H; y += 6)
        vita2d_draw_rectangle(0, (float)y, SCREEN_W, 1, RGBA8(255, 255, 255, (y * 7) % 9 + 3));
    /* top sheen + bottom vignette */
    vita2d_draw_rectangle(0, 0, SCREEN_W, 2, RGBA8(200, 210, 225, 60));
    for (int i = 0; i < 12; i++)
        vita2d_draw_rectangle(0, SCREEN_H - 12 * 6 + i * 6, SCREEN_W, 6, RGBA8(0, 0, 0, 8 + i * 4));
}

void render_panel(float x, float y, float w, float h, int highlighted, int greyed)
{
    unsigned int base = greyed ? RGBA8(40, 42, 46, 255)
                      : highlighted ? RGBA8(92, 100, 114, 255) : RGBA8(62, 67, 78, 255);
    vita2d_draw_rectangle(x + 5, y + 6, w, h, RGBA8(0, 0, 0, 110)); /* drop shadow */
    vita2d_draw_rectangle(x, y, w, h, base);
    for (float yy = y + 4; yy < y + h - 3; yy += 4)
        vita2d_draw_rectangle(x + 3, yy, w - 6, 1, RGBA8(255, 255, 255, greyed ? 4 : 10));
    unsigned char hi = greyed ? 70 : 190, lo = greyed ? 20 : 28;
    vita2d_draw_rectangle(x, y, w, BEVEL, RGBA8(hi, hi, hi + 10, 255));
    vita2d_draw_rectangle(x, y, BEVEL, h, RGBA8(hi - 20, hi - 20, hi - 10, 255));
    vita2d_draw_rectangle(x, y + h - BEVEL, w, BEVEL, RGBA8(lo, lo, lo + 4, 255));
    vita2d_draw_rectangle(x + w - BEVEL, y, BEVEL, h, RGBA8(lo + 10, lo + 10, lo + 14, 255));
    if (highlighted && !greyed) {
        vita2d_draw_rectangle(x - 3, y - 3, w + 6, 2, RGBA8(90, 230, 130, 255));
        vita2d_draw_rectangle(x - 3, y + h + 1, w + 6, 2, RGBA8(90, 230, 130, 255));
        vita2d_draw_rectangle(x - 3, y - 3, 2, h + 6, RGBA8(90, 230, 130, 255));
        vita2d_draw_rectangle(x + w + 1, y - 3, 2, h + 6, RGBA8(90, 230, 130, 255));
    }
}

void render_progress_bar(float x, float y, float w, float h, float frac)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    vita2d_draw_rectangle(x - 2, y - 2, w + 4, h + 4, RGBA8(8, 9, 11, 255));
    vita2d_draw_rectangle(x, y, w, h, RGBA8(34, 37, 44, 255));
    float fw = w * frac;
    if (fw > 0) {
        vita2d_draw_rectangle(x, y, fw, h, RGBA8(60, 190, 100, 255));
        vita2d_draw_rectangle(x, y, fw, h * 0.4f, RGBA8(140, 240, 170, 110));
    }
}

void render_dim(unsigned char alpha)
{
    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, alpha));
}

void render_check(float x, float y, float s, unsigned int color)
{
    /* thick check mark from stacked lines */
    for (int i = 0; i < 4; i++) {
        float o = (float)i;
        vita2d_draw_line(x, y + s * 0.55f + o, x + s * 0.38f, y + s * 0.9f + o, color);
        vita2d_draw_line(x + s * 0.38f, y + s * 0.9f + o, x + s, y + s * 0.15f + o, color);
    }
}

/* ---------- board ---------- */

static int is_wall(const Level *lv, int cx, int cy)
{
    return level_cell(lv, cx, cy) == CELL_WALL;
}

static void draw_floor(void)
{
    const float top = BOARD_TOP_PX;
    const float h = LEVEL_H * CELL_PX;
    /* walnut planks, 20 px tall, slightly varying tone */
    for (int i = 0; i * 20 < (int)h; i++) {
        int v = (i * 53) % 13;
        vita2d_draw_rectangle(0, top + i * 20, SCREEN_W, 20, RGBA8(70 + v, 48 + v / 2, 32 + v / 3, 255));
        vita2d_draw_rectangle(0, top + i * 20, SCREEN_W, 1, RGBA8(30, 20, 12, 120));
        /* grain streaks */
        for (int k = 0; k < 3; k++) {
            float gx = (float)((i * 131 + k * 317) % SCREEN_W);
            vita2d_draw_rectangle(gx, top + i * 20 + 6 + k * 4, 140 + k * 40, 1, RGBA8(40, 26, 16, 60));
        }
    }
    /* subtle grid tint */
    for (int cx = 1; cx < LEVEL_W; cx++)
        vita2d_draw_rectangle((float)(cx * CELL_PX), top, 1, h, RGBA8(255, 230, 190, 14));
    for (int cy = 1; cy < LEVEL_H; cy++)
        vita2d_draw_rectangle(0, top + cy * CELL_PX, SCREEN_W, 1, RGBA8(255, 230, 190, 14));
}

static void draw_wall(const Level *lv, int cx, int cy)
{
    float x = (float)(cx * CELL_PX), y = (float)(BOARD_TOP_PX + cy * CELL_PX);
    float s = (float)CELL_PX;

    vita2d_draw_rectangle(x, y, s, s, RGBA8(138, 144, 154, 255));
    /* brushed streaks, continuous across neighbouring blocks (keyed on absolute y) */
    for (int k = 0; k < CELL_PX; k += 3) {
        int yy = cy * CELL_PX + k;
        unsigned char v = (unsigned char)(128 + (yy * 37) % 26);
        vita2d_draw_rectangle(x, y + k, s, 2, RGBA8(v, v + 6, v + 14, 255));
    }
    /* bevel only on faces exposed to non-wall cells, so wall runs read as one slab */
    if (!is_wall(lv, cx, cy - 1)) vita2d_draw_rectangle(x, y, s, BEVEL, RGBA8(222, 228, 236, 255));
    if (!is_wall(lv, cx - 1, cy)) vita2d_draw_rectangle(x, y, BEVEL, s, RGBA8(200, 206, 216, 255));
    if (!is_wall(lv, cx, cy + 1)) {
        vita2d_draw_rectangle(x, y + s - BEVEL, s, BEVEL, RGBA8(48, 51, 58, 255));
        vita2d_draw_rectangle(x, y + s, s, 4, RGBA8(0, 0, 0, 70)); /* cast shadow on floor */
    }
    if (!is_wall(lv, cx + 1, cy)) {
        vita2d_draw_rectangle(x + s - BEVEL, y, BEVEL, s, RGBA8(64, 68, 76, 255));
        vita2d_draw_rectangle(x + s, y + 3, 4, s, RGBA8(0, 0, 0, 60));
    }
}

static void draw_hole(float px, float py)
{
    const int rings = 6;
    for (int i = 0; i < rings; i++) {
        float t = (float)i / (rings - 1);
        float r = HOLE_RADIUS + 4.0f - t * 6.0f;
        vita2d_draw_fill_circle(px, py, r, mix_rgb(52, 36, 24, 4, 3, 3, t, 255));
    }
    vita2d_draw_fill_circle(px + 1.5f, py + 2.0f, HOLE_RADIUS - 5.0f, RGBA8(0, 0, 0, 255));
}

static void draw_goal(float px, float py, float time_s)
{
    float pulse = 0.5f + 0.5f * sinf(time_s * 3.2f);
    const int glow = 7;
    for (int i = 0; i < glow; i++) {
        float t = (float)i / (glow - 1);
        float r = HOLE_RADIUS + 14.0f - t * 12.0f;
        vita2d_draw_fill_circle(px, py, r, RGBA8(70, 230, 120, (int)(14 + t * (60 + 60 * pulse))));
    }
    vita2d_draw_fill_circle(px, py, HOLE_RADIUS + 1.0f, RGBA8(120, 255, 160, 255));
    vita2d_draw_fill_circle(px, py, HOLE_RADIUS - 2.0f, RGBA8(20, 70, 36, 255));
    for (int i = 0; i < 4; i++) {
        float t = (float)i / 3.0f;
        vita2d_draw_fill_circle(px, py, HOLE_RADIUS - 3.0f - t * 5.0f, mix_rgb(16, 50, 26, 2, 6, 3, t, 255));
    }
}

void render_board(const Level *lv, float time_s)
{
    draw_floor();
    for (int cy = 0; cy < LEVEL_H; cy++) {
        for (int cx = 0; cx < LEVEL_W; cx++) {
            float px = cx * CELL_PX + CELL_PX * 0.5f;
            float py = BOARD_TOP_PX + cy * CELL_PX + CELL_PX * 0.5f;
            switch (level_cell(lv, cx, cy)) {
            case CELL_HOLE: draw_hole(px, py); break;
            case CELL_GOAL: draw_goal(px, py, time_s); break;
            default: break;
            }
        }
    }
    /* walls last so their cast shadows fall over the floor */
    for (int cy = 0; cy < LEVEL_H; cy++)
        for (int cx = 0; cx < LEVEL_W; cx++)
            if (is_wall(lv, cx, cy))
                draw_wall(lv, cx, cy);
}

void render_ball(const Ball *b, float scale)
{
    if (scale <= 0.02f)
        return;
    float r = BALL_RADIUS * scale;
    float x = b->x, y = b->y + BOARD_TOP_PX;

    /* soft drop shadow, offset down-right */
    vita2d_draw_fill_circle(x + 5 * scale, y + 7 * scale, r + 3, RGBA8(0, 0, 0, 40));
    vita2d_draw_fill_circle(x + 4 * scale, y + 6 * scale, r + 1, RGBA8(0, 0, 0, 70));

    /* dark outline */
    vita2d_draw_fill_circle(x, y, r, RGBA8(22, 24, 30, 255));

    /* concentric shading, drifting toward the top-left light */
    const int steps = 12;
    for (int i = 0; i < steps; i++) {
        float t = (float)i / (steps - 1);
        float rr = (r - 1.2f) * (1.0f - t * 0.88f);
        float ox = -r * 0.26f * t, oy = -r * 0.30f * t;
        vita2d_draw_fill_circle(x + ox, y + oy, rr, mix_rgb(64, 70, 82, 238, 242, 250, t * t, 255));
    }
    /* reflected horizon band (chrome look) */
    vita2d_draw_fill_circle(x + r * 0.18f, y + r * 0.34f, r * 0.30f, RGBA8(150, 160, 176, 90));

    /* specular highlight */
    vita2d_draw_fill_circle(x - r * 0.38f, y - r * 0.42f, r * 0.20f, RGBA8(255, 255, 255, 230));
    vita2d_draw_fill_circle(x - r * 0.40f, y - r * 0.44f, r * 0.10f, RGBA8(255, 255, 255, 255));
}

void render_hud(const Level *lv, int level_index)
{
    vita2d_draw_rectangle(0, 0, SCREEN_W, BOARD_TOP_PX, RGBA8(18, 20, 24, 255));
    vita2d_draw_rectangle(0, BOARD_TOP_PX - 1, SCREEN_W, 1, RGBA8(90, 96, 108, 255));
    char buf[96];
    snprintf(buf, sizeof buf, "Vault %d: %s", level_index + 1, (lv && lv->name) ? lv->name : "?");
    render_text(10, 3, RGBA8(225, 230, 238, 255), TEXT_SMALL, buf);
    const char *hint = "START: pause";
    render_text(SCREEN_W - 10 - render_text_width(TEXT_SMALL, hint), 3, RGBA8(150, 158, 170, 255),
                TEXT_SMALL, hint);
}
