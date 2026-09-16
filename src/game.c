/* Scene state machine: menu, level select, play (hold/recal/run/fell/pause/cleared), updates. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

#include "game.h"
#include "level.h"
#include "motion.h"
#include "physics.h"
#include "render.h"
#include "save.h"
#include "sound.h"
#include "timer.h"
#include "updater.h"
#include "version.h"

#define HOLD_SECONDS  1.0f
#define FELL_SECONDS  0.8f
#define WALL_HIT_MIN_SPEED 30.0f   /* px/s below which a wall touch stays silent */
#define WALL_HIT_COOLDOWN  0.06f   /* min seconds between wall-hit sounds */

#define COL_TEXT   RGBA8(230, 234, 240, 255)
#define COL_DIM    RGBA8(140, 146, 156, 255)
#define COL_GREEN  RGBA8(110, 235, 150, 255)
#define COL_RED    RGBA8(240, 110, 100, 255)
#define COL_GREY   RGBA8(95, 98, 104, 255)

#define SELECT_COLS 5  /* level-select grid columns */

typedef enum { SCENE_MENU, SCENE_SELECT, SCENE_PLAY, SCENE_UPDATES } Scene;
typedef enum { PLAY_HOLD, PLAY_RECAL, PLAY_RUN, PLAY_FELL, PLAY_PAUSED, PLAY_CLEARED } PlayState;

static Scene     scene = SCENE_MENU;
static PlayState play_state = PLAY_HOLD;
static SaveData  save;
static Level     levels[LEVEL_COUNT];
static int       level_ok[LEVEL_COUNT];
static Ball      ball;
static int       cur_level = 0;
static int       menu_sel = 0, select_sel = 0, pause_sel = 0, cleared_sel = 0;
static float     state_timer = 0.0f, clock_s = 0.0f;
static int       save_failed = 0;
static int       updater_started = 0;
static unsigned  buttons_prev = 0, pressed = 0;

/* Level to render_board_cache() outside the next render_begin/render_end, or -1 for none. */
static int       pending_cache_index = -1;

/* Session-only: once the player has done a flat ("place on a table") recalibration,
 * vault-start no longer auto-recalibrates. Not saved. */
static int       calibrated_flat_this_session = 0;

/* Per-vault elapsed play time; only advances during PLAY_RUN. */
static float     play_time_s = 0.0f;

/* Wall-hit sound rate limit. */
static float     wall_sound_cd = 0.0f;

/* Result of the vault just cleared, captured on PHYS_GOAL for the completion screen. */
static uint32_t  cleared_time_cs = TIMER_NONE;
static uint32_t  best_time_cs = TIMER_NONE;
static int       cleared_new_best = 0;

/* Set on the completion screen's Quit; game_frame() returns it so main.c can exit. */
static int       quit_requested = 0;

/* ---------- input ---------- */

static void poll_input(void)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
        pad.buttons = 0;
    pressed = pad.buttons & ~buttons_prev;
    buttons_prev = pad.buttons;
}

static int hit(unsigned mask) { return (pressed & mask) != 0; }

/* ---------- helpers ---------- */

static int unlocked_count(void)
{
    int u = save.unlocked;
    if (u < 1) u = 1;
    if (u > LEVEL_COUNT) u = LEVEL_COUNT;
    return u;
}

static int level_done(int i) { return (save.completed_mask >> i) & 1; }

static int select_col(int i) { return i % SELECT_COLS; }

/* Highest row index (row = i / SELECT_COLS) that has a card in the given column;
 * the bottom row may be short, so not every column reaches it. */
static int select_last_row_with_col(int col)
{
    int r = (LEVEL_COUNT - 1) / SELECT_COLS;
    while (r * SELECT_COLS + col >= LEVEL_COUNT)
        r--;
    return r;
}

/* Move `delta` rows (+-SELECT_COLS) staying in the same column, wrapping within
 * 0..LEVEL_COUNT-1: past the bottom wraps to row 0 of that column; up from row 0
 * wraps to the lowest row that has that column (which may be the short last row). */
static int select_move_vert(int sel, int delta)
{
    int col = select_col(sel);
    int t = sel + delta;
    if (delta > 0) {
        if (t >= LEVEL_COUNT) t = col;
    } else if (t < 0) {
        t = select_last_row_with_col(col) * SELECT_COLS + col;
    }
    return t;
}

static void enter_level(int index)
{
    if (index < 0 || index >= LEVEL_COUNT || !level_ok[index])
        return;
    cur_level = index;
    physics_reset(&ball, &levels[index]);
    play_state = PLAY_HOLD;
    state_timer = 0.0f;
    play_time_s = 0.0f;
    wall_sound_cd = 0.0f;
    save_failed = 0;
    /* Once a flat recalibration has happened this session, vault-start no longer
     * recalibrates, so there is nothing to average. */
    if (!calibrated_flat_this_session)
        motion_calibrate_begin();
    pending_cache_index = index; /* cached outside render_begin/render_end, see game_frame() */
    scene = SCENE_PLAY;
}

static void draw_menu_list(const char *const *items, int count, int sel, float top)
{
    for (int i = 0; i < count; i++) {
        float w = 360, h = 56, x = (SCREEN_W - w) * 0.5f, y = top + i * 72;
        render_panel(x, y, w, h, i == sel, 0);
        render_text_centered(SCREEN_W * 0.5f, y + 14, i == sel ? COL_TEXT : COL_DIM, TEXT_BIG * 0.75f, items[i]);
    }
}

/* ---------- scenes ---------- */

static void scene_menu(void)
{
    static const char *const items[] = { "Play", "Level Select", "Check for Updates" };
    const int n = 3;

    if (hit(SCE_CTRL_UP))   menu_sel = (menu_sel + n - 1) % n;
    if (hit(SCE_CTRL_DOWN)) menu_sel = (menu_sel + 1) % n;
    if (hit(SCE_CTRL_CROSS)) {
        if (menu_sel == 0 && level_ok[unlocked_count() - 1]) {
            enter_level(unlocked_count() - 1);
        } else if (menu_sel <= 1) { /* Level Select, or Play when that vault is Unavailable */
            select_sel = unlocked_count() - 1;
            scene = SCENE_SELECT;
        } else {
            if (!updater_started) {
                updater_init();
                updater_started = 1;
            }
            scene = SCENE_UPDATES;
        }
    }

    render_steel_background();
    render_text_centered(SCREEN_W * 0.5f, 70, COL_TEXT, TEXT_TITLE, "GYROVAULT");
    render_text_centered(SCREEN_W * 0.5f, 150, COL_GREEN, TEXT_NORMAL, "Tilt your Vita to roll the ball into the vault");
    draw_menu_list(items, n, menu_sel, 230);
    render_text(SCREEN_W - 12 - render_text_width(TEXT_SMALL, "v" GV_VERSION), SCREEN_H - 26, COL_DIM,
                TEXT_SMALL, "v" GV_VERSION);
    render_text(12, SCREEN_H - 26, COL_DIM, TEXT_SMALL, "Up/Down: choose   X: select");
}

static void scene_select(void)
{
    if (hit(SCE_CTRL_LEFT))  select_sel = (select_sel + LEVEL_COUNT - 1) % LEVEL_COUNT;
    if (hit(SCE_CTRL_RIGHT)) select_sel = (select_sel + 1) % LEVEL_COUNT;
    if (hit(SCE_CTRL_UP))    select_sel = select_move_vert(select_sel, -SELECT_COLS);
    if (hit(SCE_CTRL_DOWN))  select_sel = select_move_vert(select_sel, SELECT_COLS);
    if (hit(SCE_CTRL_CIRCLE)) scene = SCENE_MENU;
    if (hit(SCE_CTRL_CROSS) && save_level_open(&save, select_sel))
        enter_level(select_sel);

    render_steel_background();
    render_text_centered(SCREEN_W * 0.5f, 40, COL_TEXT, TEXT_BIG, "Level Select");

    /* 5-column grid, rows as needed; laid out by fixed column slot (the short
     * last row is not re-centred) so on-screen position matches select_move_vert's
     * column-preserving math. */
    const float cw = 170, ch = 80, col_gap = 15, row_gap = 10, grid_top = 92;
    const float total_w = SELECT_COLS * cw + (SELECT_COLS - 1) * col_gap;
    const float grid_x0 = (SCREEN_W - total_w) * 0.5f;

    for (int i = 0; i < LEVEL_COUNT; i++) {
        int col = i % SELECT_COLS, row = i / SELECT_COLS;
        float x = grid_x0 + col * (cw + col_gap), y = grid_top + row * (ch + row_gap);
        int failed = !level_ok[i];
        int locked = failed || !save_level_open(&save, i);
        render_panel(x, y, cw, ch, i == select_sel, locked);

        char title[32];
        snprintf(title, sizeof title, "Vault %d", i + 1);
        render_text_centered(x + cw * 0.5f, y + 10, locked ? COL_GREY : COL_TEXT, TEXT_NORMAL, title);

        float sy = y + ch - 28;
        if (failed) {
            render_text_centered(x + cw * 0.5f, sy, COL_RED, TEXT_SMALL, "Unavailable");
        } else if (locked) {
            render_text_centered(x + cw * 0.5f, sy, COL_RED, TEXT_SMALL, "Locked");
        } else if (level_done(i)) {
            float tw = render_text_width(TEXT_SMALL, "Cleared");
            render_check(x + cw * 0.5f - tw * 0.5f - 20, sy - 2, 14, COL_GREEN);
            render_text_centered(x + cw * 0.5f, sy, COL_GREEN, TEXT_SMALL, "Cleared");
        }
    }

    int sel_unavailable = !level_ok[select_sel];
    int sel_locked = !sel_unavailable && !save_level_open(&save, select_sel);
    const char *name = level_ok[select_sel] && levels[select_sel].name ? levels[select_sel].name : "(unavailable)";
    render_text_centered(SCREEN_W * 0.5f, 460, (sel_unavailable || sel_locked) ? COL_DIM : COL_TEXT, TEXT_NORMAL, name);
    if (sel_unavailable)
        render_text_centered(SCREEN_W * 0.5f, 482, COL_RED, TEXT_SMALL, "This vault's data failed to load");
    else if (sel_locked)
        render_text_centered(SCREEN_W * 0.5f, 482, COL_RED, TEXT_SMALL, "Clear the previous vault to unlock");

    render_text(12, SCREEN_H - 26, COL_DIM, TEXT_SMALL, "X: play   O: back");
}

static void draw_play_world(float ball_scale, const char *time_text)
{
    render_board(&levels[cur_level], clock_s);
    render_ball(&ball, ball_scale);
    render_hud(&levels[cur_level], cur_level, time_text);
}

/* HUD timer text: only shown while a run is live (rolling, mid-fall animation, or
 * paused mid-run); NULL (no timer) during hold/recalibrate/cleared overlays. */
static const char *hud_time_text(char *buf, size_t cap)
{
    switch (play_state) {
    case PLAY_RUN:
    case PLAY_FELL:
    case PLAY_PAUSED:
        timer_format(timer_to_cs(play_time_s), buf, cap);
        return buf;
    default:
        return NULL;
    }
}

static void scene_play(float dt)
{
    const Level *lv = &levels[cur_level];
    float tx = 0.0f, ty = 0.0f;
    char time_buf[16];

    switch (play_state) {
    case PLAY_HOLD:
        state_timer += dt;
        if (!calibrated_flat_this_session)
            motion_calibrate_sample();
        motion_read(&tx, &ty);
        draw_play_world(1.0f, NULL);
        render_dim(120);
        render_panel(280, 190, 400, 170, 0, 0);
        render_text_centered(SCREEN_W * 0.5f, 215, COL_TEXT, TEXT_BIG * 0.8f, "Hold your Vita level");
        render_tilt_gauge(SCREEN_W * 0.5f, 305, 38, tx, ty);
        if (state_timer >= HOLD_SECONDS) {
            if (!calibrated_flat_this_session)
                motion_calibrate_end();
            play_state = PLAY_RUN;
        }
        break;

    case PLAY_RECAL:
        motion_calibrate_sample();
        motion_read_flat(&tx, &ty);
        draw_play_world(1.0f, NULL);
        render_dim(120);
        render_panel(230, 170, 500, 210, 0, 0);
        render_text_centered(SCREEN_W * 0.5f, 205, COL_TEXT, TEXT_BIG * 0.7f, "Place your PS Vita on a flat surface");
        render_text_centered(SCREEN_W * 0.5f, 240, COL_DIM, TEXT_NORMAL, "Press X");
        render_tilt_gauge(SCREEN_W * 0.5f, 320, 42, tx, ty);
        if (hit(SCE_CTRL_CROSS)) {
            motion_calibrate_end(); /* 0 samples safely keeps the previous neutral */
            calibrated_flat_this_session = 1;
            physics_reset(&ball, lv);
            play_time_s = 0.0f;
            play_state = PLAY_RUN;
        } else if (hit(SCE_CTRL_CIRCLE)) {
            play_state = PLAY_PAUSED; /* back out without touching calibration */
        }
        break;

    case PLAY_RUN: {
        if (hit(SCE_CTRL_START)) {
            play_state = PLAY_PAUSED;
            pause_sel = 0;
            draw_play_world(1.0f, hud_time_text(time_buf, sizeof time_buf));
            break;
        }
        play_time_s += dt;
        motion_read(&tx, &ty);
        float wall_impact = 0.0f;
        PhysResult r = physics_step(&ball, lv, tx, ty, dt, &wall_impact);
        sound_set_rolling(hypotf(ball.vx, ball.vy));

        wall_sound_cd -= dt;
        if (wall_impact > WALL_HIT_MIN_SPEED && wall_sound_cd <= 0.0f) {
            float inten = wall_impact / BALL_MAX_SPEED;
            if (inten > 1.0f) inten = 1.0f;
            if (inten < 0.0f) inten = 0.0f;
            sound_play(SND_WALL_HIT, inten);
            wall_sound_cd = WALL_HIT_COOLDOWN;
        }

        if (r == PHYS_FELL) {
            sound_play(SND_FELL, 1.0f);
            play_state = PLAY_FELL;
            state_timer = 0.0f;
            play_time_s = 0.0f;
        } else if (r == PHYS_GOAL) {
            sound_play(SND_GOAL, 1.0f);
            cleared_time_cs = timer_to_cs(play_time_s);
            save_mark_complete(&save, cur_level);
            cleared_new_best = save_record_time(&save, cur_level, cleared_time_cs);
            best_time_cs = save.best_cs[cur_level];
            save_failed = save_write(&save, SAVE_DIR, SAVE_PATH) != 0;
            play_state = PLAY_CLEARED;
            state_timer = 0.0f;
            cleared_sel = 0;
        }
        draw_play_world(1.0f, hud_time_text(time_buf, sizeof time_buf));
        break;
    }

    case PLAY_FELL:
        state_timer += dt;
        {
            float k = 1.0f - state_timer / (FELL_SECONDS * 0.5f);
            draw_play_world(k < 0 ? 0 : k, hud_time_text(time_buf, sizeof time_buf));
        }
        if (((int)(state_timer * 8)) % 2 == 0)
            render_dim(70);
        render_text_centered(SCREEN_W * 0.5f, 230, COL_RED, TEXT_TITLE * 0.8f, "Fell in!");
        if (state_timer >= FELL_SECONDS) {
            physics_reset(&ball, lv);
            motion_read(&tx, &ty); /* keep the filter warm */
            play_state = PLAY_RUN;
        }
        break;

    case PLAY_PAUSED: {
        static const char *const items[] = { "Resume", "Recalibrate", "Quit to menu" };
        if (hit(SCE_CTRL_UP))   pause_sel = (pause_sel + 2) % 3;
        if (hit(SCE_CTRL_DOWN)) pause_sel = (pause_sel + 1) % 3;
        int resume = hit(SCE_CTRL_START) || hit(SCE_CTRL_CIRCLE);
        if (hit(SCE_CTRL_CROSS)) {
            if (pause_sel == 0) resume = 1;
            else if (pause_sel == 1) {
                motion_calibrate_begin();
                play_state = PLAY_RECAL;
            }
            else scene = SCENE_MENU;
        }
        if (resume)
            play_state = PLAY_RUN;
        draw_play_world(1.0f, hud_time_text(time_buf, sizeof time_buf));
        render_dim(150);
        render_text_centered(SCREEN_W * 0.5f, 110, COL_TEXT, TEXT_BIG, "Paused");
        draw_menu_list(items, 3, pause_sel, 190);
        break;
    }

    case PLAY_CLEARED: {
        int last = cur_level >= LEVEL_COUNT - 1;
        int can_next = !last && level_ok[cur_level + 1];
        static const char *const items_next[]   = { "Next Level", "Level Select", "Quit" };
        static const char *const items_nonext[] = { "Level Select", "Quit" };
        const char *const *items = can_next ? items_next : items_nonext;
        int n = can_next ? 3 : 2;

        state_timer += dt;
        if (hit(SCE_CTRL_UP))   cleared_sel = (cleared_sel + n - 1) % n;
        if (hit(SCE_CTRL_DOWN)) cleared_sel = (cleared_sel + 1) % n;
        if (hit(SCE_CTRL_CROSS)) {
            if (can_next && cleared_sel == 0) {
                enter_level(cur_level + 1);
            } else if ((can_next && cleared_sel == 1) || (!can_next && cleared_sel == 0)) {
                select_sel = cur_level;
                scene = SCENE_SELECT;
            } else {
                quit_requested = 1;
            }
        }
        if (scene != SCENE_PLAY || play_state != PLAY_CLEARED) {
            /* left this state this frame; draw the destination next frame */
            render_steel_background();
            break;
        }

        draw_play_world(0.6f, NULL);
        render_flash(state_timer);
        render_dim(160);
        render_panel(180, 30, 600, 480, 0, 0);

        render_text_centered(SCREEN_W * 0.5f, 55, COL_GREEN, TEXT_BIG * 1.1f,
                             last ? "All vaults cleared!" : "Vault cleared!");
        render_check(SCREEN_W * 0.5f - 20, 95, 40, COL_GREEN);

        const char *vname = levels[cur_level].name ? levels[cur_level].name : "";
        render_text_centered(SCREEN_W * 0.5f, 155, COL_TEXT, TEXT_NORMAL, vname);

        char tbuf[16], bbuf[16], line[48];
        timer_format(cleared_time_cs, tbuf, sizeof tbuf);
        timer_format(best_time_cs, bbuf, sizeof bbuf);
        float y = 190;
        snprintf(line, sizeof line, "Time: %s", tbuf);
        render_text_centered(SCREEN_W * 0.5f, y, COL_TEXT, TEXT_NORMAL, line);
        y += 27;
        snprintf(line, sizeof line, "Best: %s", bbuf);
        render_text_centered(SCREEN_W * 0.5f, y, COL_TEXT, TEXT_NORMAL, line);
        y += 27;
        if (cleared_new_best) {
            render_text_centered(SCREEN_W * 0.5f, y, COL_GREEN, TEXT_SMALL, "New best!");
            y += 24;
        }
        if (save_failed) {
            render_text_centered(SCREEN_W * 0.5f, y, COL_RED, TEXT_SMALL, "Warning: progress could not be saved");
            y += 24;
        }
        draw_menu_list(items, n, cleared_sel, y + 15);
        break;
    }
    }
}

static const char *state_name(UpdState s)
{
    switch (s) {
    case UPD_IDLE:        return "Idle";
    case UPD_CHECKING:    return "Checking for updates...";
    case UPD_UP_TO_DATE:  return "Up to date";
    case UPD_AVAILABLE:   return "Update available";
    case UPD_DOWNLOADING: return "Downloading...";
    case UPD_INSTALLING:  return "Installing...";
    case UPD_DONE:        return "Update installed";
    case UPD_ERROR:       return "Error";
    }
    return "?";
}

/* Word-wrap s into at most max_lines lines no wider than max_w; the last line is cut. */
static void draw_wrapped(float x, float y, float max_w, int max_lines, unsigned int color,
                         float scale, const char *s)
{
    char line[128];
    for (int ln = 0; ln < max_lines && *s; ln++) {
        size_t len = 0, fit = 0;
        while (s[len] && len < sizeof line - 1) {
            memcpy(line, s, len + 1);
            line[len + 1] = '\0';
            if (render_text_width(scale, line) > max_w)
                break;
            len++;
            if (s[len] == ' ' || s[len] == '\0')
                fit = len;
        }
        if (fit == 0)
            fit = len ? len : 1; /* a single word wider than the box: hard-cut it */
        memcpy(line, s, fit);
        line[fit] = '\0';
        render_text(x, y + ln * 22, color, scale, line);
        s += fit;
        while (*s == ' ')
            s++;
    }
}

static void scene_updates(void)
{
    UpdState st = updater_state();
    int busy = (st == UPD_DOWNLOADING || st == UPD_INSTALLING);

    if (hit(SCE_CTRL_CROSS)) {
        if (st == UPD_AVAILABLE)
            updater_start_install();
        else if (st == UPD_IDLE || st == UPD_UP_TO_DATE || st == UPD_ERROR)
            updater_start_check();
    }
    if (hit(SCE_CTRL_CIRCLE) && !busy)
        scene = SCENE_MENU;

    render_steel_background();
    render_text_centered(SCREEN_W * 0.5f, 40, COL_TEXT, TEXT_BIG, "Updates");
    render_panel(130, 120, 700, 300, 0, 0);

    char buf[128];
    snprintf(buf, sizeof buf, "Installed version: v%s", GV_VERSION);
    render_text(160, 145, COL_DIM, TEXT_NORMAL, buf);
    const char *tag = updater_latest_tag();
    if (tag && tag[0]) {
        snprintf(buf, sizeof buf, "Latest release: %s", tag);
        render_text(160, 180, COL_DIM, TEXT_NORMAL, buf);
    }

    unsigned int scol = st == UPD_ERROR ? COL_RED : (st == UPD_AVAILABLE || st == UPD_DONE) ? COL_GREEN : COL_TEXT;
    render_text(160, 225, scol, TEXT_BIG * 0.8f, state_name(st));
    const char *msg = updater_message();
    if (msg && msg[0])
        draw_wrapped(160, 270, 640, 3, COL_DIM, TEXT_SMALL, msg);

    if (busy)
        render_progress_bar(160, 320, 640, 24, updater_progress());

    const char *hint;
    switch (st) {
    case UPD_AVAILABLE:  hint = "X: download and install   O: back"; break;
    case UPD_DONE:       hint = "Close and reopen Gyrovault to finish"; break;
    case UPD_CHECKING:   hint = "O: back"; break;
    default:             hint = busy ? "Please wait - do not close the app" : "X: check for updates   O: back"; break;
    }
    render_text_centered(SCREEN_W * 0.5f, 370, st == UPD_DONE ? COL_GREEN : COL_TEXT, TEXT_NORMAL, hint);
}

/* ---------- public ---------- */

void game_init(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    save_load(&save, SAVE_PATH); /* missing/corrupt -> defaults */
    for (int i = 0; i < LEVEL_COUNT; i++)
        level_ok[i] = level_load(i, &levels[i]) == 0;
    menu_sel = 0;
    scene = SCENE_MENU;
}

int game_frame(float dt)
{
    clock_s += dt;
    poll_input();
    sound_set_rolling(0.0f); /* PLAY_RUN below overrides this when actually rolling */

    if (pending_cache_index >= 0) {
        render_board_cache(&levels[pending_cache_index]); /* outside render_begin/render_end */
        pending_cache_index = -1;
    }

    render_begin();
    switch (scene) {
    case SCENE_MENU:    scene_menu();    break;
    case SCENE_SELECT:  scene_select();  break;
    case SCENE_PLAY:    scene_play(dt);  break;
    case SCENE_UPDATES: scene_updates(); break;
    }
    render_end();
    return quit_requested;
}

void game_shutdown(void)
{
    if (updater_started)
        updater_shutdown();
}
