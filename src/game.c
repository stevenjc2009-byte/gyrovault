/* Scene state machine: menu, level select, play (hold/run/fell/pause/cleared), updates. */
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
#include "updater.h"
#include "version.h"

#define HOLD_SECONDS  1.0f
#define FELL_SECONDS  0.8f

#define COL_TEXT   RGBA8(230, 234, 240, 255)
#define COL_DIM    RGBA8(140, 146, 156, 255)
#define COL_GREEN  RGBA8(110, 235, 150, 255)
#define COL_RED    RGBA8(240, 110, 100, 255)
#define COL_GREY   RGBA8(95, 98, 104, 255)

#define SELECT_COLS 5  /* level-select grid columns */

typedef enum { SCENE_MENU, SCENE_SELECT, SCENE_PLAY, SCENE_UPDATES } Scene;
typedef enum { PLAY_HOLD, PLAY_RUN, PLAY_FELL, PLAY_PAUSED, PLAY_CLEARED } PlayState;

static Scene     scene = SCENE_MENU;
static PlayState play_state = PLAY_HOLD;
static SaveData  save;
static Level     levels[LEVEL_COUNT];
static int       level_ok[LEVEL_COUNT];
static Ball      ball;
static int       cur_level = 0;
static int       menu_sel = 0, select_sel = 0, pause_sel = 0;
static float     state_timer = 0.0f, clock_s = 0.0f;
static int       save_failed = 0;
static int       updater_started = 0;
static unsigned  buttons_prev = 0, pressed = 0;

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
    save_failed = 0;
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
        if (menu_sel == 0) {
            enter_level(unlocked_count() - 1);
        } else if (menu_sel == 1) {
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
        int locked = !save_level_open(&save, i) || !level_ok[i];
        render_panel(x, y, cw, ch, i == select_sel, locked);

        char title[32];
        snprintf(title, sizeof title, "Vault %d", i + 1);
        render_text_centered(x + cw * 0.5f, y + 10, locked ? COL_GREY : COL_TEXT, TEXT_NORMAL, title);

        float sy = y + ch - 28;
        if (locked) {
            render_text_centered(x + cw * 0.5f, sy, COL_RED, TEXT_SMALL, "Locked");
        } else if (level_done(i)) {
            float tw = render_text_width(TEXT_SMALL, "Cleared");
            render_check(x + cw * 0.5f - tw * 0.5f - 20, sy - 2, 14, COL_GREEN);
            render_text_centered(x + cw * 0.5f, sy, COL_GREEN, TEXT_SMALL, "Cleared");
        }
    }

    int sel_locked = !save_level_open(&save, select_sel) || !level_ok[select_sel];
    const char *name = level_ok[select_sel] && levels[select_sel].name ? levels[select_sel].name : "(unavailable)";
    render_text_centered(SCREEN_W * 0.5f, 460, sel_locked ? COL_DIM : COL_TEXT, TEXT_NORMAL, name);
    if (sel_locked)
        render_text_centered(SCREEN_W * 0.5f, 482, COL_RED, TEXT_SMALL, "Clear the previous vault to unlock");

    render_text(12, SCREEN_H - 26, COL_DIM, TEXT_SMALL, "X: play   O: back");
}

static void draw_play_world(float ball_scale)
{
    render_board(&levels[cur_level], clock_s);
    render_ball(&ball, ball_scale);
    render_hud(&levels[cur_level], cur_level);
}

static void scene_play(float dt)
{
    const Level *lv = &levels[cur_level];
    float tx = 0.0f, ty = 0.0f;

    switch (play_state) {
    case PLAY_HOLD:
        state_timer += dt;
        draw_play_world(1.0f);
        render_dim(120);
        render_panel(280, 210, 400, 100, 0, 0);
        render_text_centered(SCREEN_W * 0.5f, 240, COL_TEXT, TEXT_BIG * 0.8f, "Hold your Vita level");
        if (state_timer >= HOLD_SECONDS) {
            motion_calibrate();
            play_state = PLAY_RUN;
        }
        break;

    case PLAY_RUN: {
        if (hit(SCE_CTRL_START)) {
            play_state = PLAY_PAUSED;
            pause_sel = 0;
            draw_play_world(1.0f);
            break;
        }
        motion_read(&tx, &ty);
        PhysResult r = physics_step(&ball, lv, tx, ty, dt);
        if (r == PHYS_FELL) {
            play_state = PLAY_FELL;
            state_timer = 0.0f;
        } else if (r == PHYS_GOAL) {
            save_mark_complete(&save, cur_level);
            save_failed = save_write(&save, SAVE_DIR, SAVE_PATH) != 0;
            play_state = PLAY_CLEARED;
            state_timer = 0.0f;
        }
        draw_play_world(1.0f);
        break;
    }

    case PLAY_FELL:
        state_timer += dt;
        {
            float k = 1.0f - state_timer / (FELL_SECONDS * 0.5f);
            draw_play_world(k < 0 ? 0 : k);
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
            else if (pause_sel == 1) { play_state = PLAY_HOLD; state_timer = 0.0f; }
            else scene = SCENE_MENU;
        }
        if (resume)
            play_state = PLAY_RUN;
        draw_play_world(1.0f);
        render_dim(150);
        render_text_centered(SCREEN_W * 0.5f, 110, COL_TEXT, TEXT_BIG, "Paused");
        draw_menu_list(items, 3, pause_sel, 190);
        break;
    }

    case PLAY_CLEARED: {
        int last = cur_level >= LEVEL_COUNT - 1;
        state_timer += dt;
        if (hit(SCE_CTRL_CROSS)) {
            if (last || !level_ok[cur_level + 1]) scene = SCENE_MENU;
            else enter_level(cur_level + 1);
        } else if (hit(SCE_CTRL_CIRCLE)) {
            scene = SCENE_MENU;
        }
        if (scene != SCENE_PLAY || play_state != PLAY_CLEARED) {
            /* left this state this frame; draw the destination next frame */
            render_steel_background();
            break;
        }
        draw_play_world(0.6f);
        render_dim(160);
        render_panel(230, 150, 500, 250, 0, 0);
        render_text_centered(SCREEN_W * 0.5f, 180, COL_GREEN, TEXT_BIG * 1.2f,
                             last ? "All vaults cleared!" : "Vault cleared!");
        render_check(SCREEN_W * 0.5f - 24, 250, 48, COL_GREEN);
        render_text_centered(SCREEN_W * 0.5f, 330, COL_TEXT, TEXT_NORMAL,
                             last ? "X: menu   O: menu" : "X: next vault   O: menu");
        if (save_failed)
            render_text_centered(SCREEN_W * 0.5f, 365, COL_RED, TEXT_SMALL, "Warning: progress could not be saved");
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

    render_begin();
    switch (scene) {
    case SCENE_MENU:    scene_menu();    break;
    case SCENE_SELECT:  scene_select();  break;
    case SCENE_PLAY:    scene_play(dt);  break;
    case SCENE_UPDATES: scene_updates(); break;
    }
    render_end();
    return 0;
}

void game_shutdown(void)
{
    if (updater_started)
        updater_shutdown();
}
