/* Gweled for the SF3500 (MIPS32r2 handheld): game loop, animation, screens.
 *
 * A fresh SDL2 front end for the rules of Gweled 1.0-beta2 (engine.c is a port of the original board_engine.c).
 * The original is GPL-2.0-or-later, so is this. Gem art and sounds are the original's.
 * Pad only: D-pad moves the cursor, A selects, then a D-pad direction swaps (or A on a neighbour).
 */
#include "game.h"
#include "draw.h"
#include "plat.h"
#include "pak.h"
#include "snd.h"
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>


/* ------------------------------------------------------------------ strings (one place, for translations) */
#define S_TITLE_NORMAL   "Normal"
#define S_TITLE_TIMED    "Timed"
#define S_TITLE_ENDLESS  "Endless"
#define S_DESC_NORMAL    "Get as many points as you can. The game ends if you run out of moves."
#define S_DESC_TIMED     "Get as many points as you can. The game ends if you run out of time."
#define S_DESC_ENDLESS   "The game never ends, but your score will not be saved."

/* ------------------------------------------------------------------ time */
static unsigned g_now;
static unsigned now_ms(void) {
    static unsigned step;
    if (!step) step = getenv("GWL_FRAME_MS") ? (unsigned)atoi(getenv("GWL_FRAME_MS")) : 20u;   /* test hook: headless frame length */
    if (plat_headless() && getenv("GWL_JITTER")) {           /* test hook: uneven frame times, 5..GWL_JITTER ms */
        static unsigned t, lastf = ~0u, rs = 12345;
        if (plat_frame() != lastf) { lastf = plat_frame(); rs = rs * 1664525u + 1013904223u; t += 5 + (rs >> 16) % (unsigned)(atoi(getenv("GWL_JITTER")) - 4); }
        return t;
    }
    return plat_headless() ? plat_frame() * step : plat_ticks();
}

/* ------------------------------------------------------------------ tweens */
static float ease(int mode, float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    switch (mode) {
    case EZ_OUT_CUBIC: t -= 1; return t * t * t + 1;
    case EZ_IN_OUT_CUBIC: t *= 2; if (t < 1) return 0.5f * t * t * t; t -= 2; return 0.5f * (t * t * t + 2);
    case EZ_IN_QUAD: return t * t;
    case EZ_IN_OUT_QUAD: t *= 2; if (t < 1) return 0.5f * t * t; t -= 1; return -0.5f * (t * (t - 2) - 1);
    case EZ_OUT_BOUNCE:
        if (t < 1 / 2.75f) return 7.5625f * t * t;
        if (t < 2 / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
        if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
        t -= 2.625f / 2.75f; return 7.5625f * t * t + 0.984375f;
    case EZ_OUT_BACK: { float s = 1.70158f; t -= 1; return t * t * ((s + 1) * t + s) + 1; }
    default: return t;
    }
}

static float tw_get(const Tw* w) {
    float t;
    if (!w->active) return w->to;
    if (g_now < w->t0 + (unsigned)w->delay) return w->from;
    t = w->dur > 0 ? (float)(g_now - w->t0 - (unsigned)w->delay) / (float)w->dur : 1.0f;
    if (t >= 1.0f) return w->to;
    return w->from + (w->to - w->from) * ease(w->ease, t);
}
static int tw_busy(const Tw* w) { return w->active && g_now < w->t0 + (unsigned)w->delay + (unsigned)w->dur; }
static void tw_go(Tw* w, float to, int dur, int delay, int mode) {
    w->from = tw_get(w); w->to = to; w->t0 = g_now; w->dur = dur; w->delay = delay; w->ease = mode; w->active = 1;
}
static void tw_set(Tw* w, float v) { w->from = w->to = v; w->active = 0; }

/* ------------------------------------------------------------------ gems */
#define MAXGEM 320
static struct Gem g_gem[MAXGEM];
static int g_silent;
static int g_cache_valid;                     /* board cache (see draw_board_cached) */
static Img g_gemimg[7], g_gemzoom[7], g_cursor_img;

int gem_y(const Gem* g) { return g->y; }

static Gem* gem_new(void) {
    int i;
    for (i = 0; i < MAXGEM; i++) if (!g_gem[i].alive) { memset(&g_gem[i], 0, sizeof g_gem[i]); g_gem[i].alive = 1; return &g_gem[i]; }
    return NULL;
}

Gem* gem_create(int x, int y, int type) {
    Gem* g;
    if (g_silent) return NULL;
    g = gem_new();
    if (!g) return NULL;
    g->type = type; g->x = x; g->y = y;
    tw_set(&g->px, (float)(x * TILE)); tw_set(&g->py, (float)(y * TILE));
    tw_set(&g->sx, 1); tw_set(&g->sy, 1); tw_set(&g->op, 255);
    return g;
}

static void gem_clear_all(void) { memset(g_gem, 0, sizeof g_gem); }

static void gem_move_to(Gem* g, int dx, int dy) {            /* swap animation */
    g->x = dx; g->y = dy; g->anim = 1;
    tw_go(&g->px, (float)(dx * TILE), 200, 0, EZ_IN_OUT_CUBIC);
    tw_go(&g->py, (float)(dy * TILE), 200, 0, EZ_IN_OUT_CUBIC);
}
static void gem_fall_to(Gem* g, int ypos, int delay, int speed) {
    g->anim = 1;
    tw_go(&g->py, (float)(ypos * TILE), speed, delay, EZ_OUT_CUBIC);
    g->y = ypos;
}
static void gem_fall_effect(Gem* g, int ypos, int delay) {   /* new game: falls in, no 'moving' flag in the original */
    g->y = ypos;
    tw_go(&g->py, (float)(ypos * TILE), 500, delay, EZ_IN_QUAD);
}
void gem_destroy_anim(Gem* g) {
    if (!g) return;
    g->anim = 1; g->dying = 1; g->die_t0 = g_now;
    tw_go(&g->sx, 1.2f, 80, 0, EZ_OUT_CUBIC);
    tw_go(&g->sy, 1.2f, 80, 0, EZ_OUT_CUBIC);
}
static void gem_fx(Gem* g, int fx) { if (!g) return; g->fx = fx; g->fx_t0 = g_now; }

static void gems_update(void) {
    int i;
    for (i = 0; i < MAXGEM; i++) {
        Gem* g = &g_gem[i];
        if (!g->alive) continue;
        if (g->dying) {
            unsigned t = g_now - g->die_t0;
            if (t >= 80 && g->dying == 1) { g->dying = 2; tw_go(&g->sx, 0, 150, 0, EZ_LINEAR); tw_go(&g->sy, 0, 150, 0, EZ_LINEAR); }
            if (t >= 230) g->alive = 0;
        } else if (g->anim && !tw_busy(&g->px) && !tw_busy(&g->py)) g->anim = 0;
    }
}
static int gems_moving(void) {
    int i;
    for (i = 0; i < MAXGEM; i++) if (g_gem[i].alive && g_gem[i].anim) return 1;
    return 0;
}

/* ------------------------------------------------------------------ cursor objects (the gold brackets) and popups */
typedef struct { int used; Tw px, py, op; } Cur;
static Cur g_cur[4];
static int cur_new(int x, int y) {
    int i;
    for (i = 0; i < 4; i++) if (!g_cur[i].used) {
        g_cur[i].used = 1; tw_set(&g_cur[i].px, (float)(x * TILE)); tw_set(&g_cur[i].py, (float)(y * TILE)); tw_set(&g_cur[i].op, 255);
        return i;
    }
    return -1;
}
static void cur_kill(int i) { if (i >= 0) g_cur[i].used = 0; }
static void cur_move(int i, int x, int y) { if (i < 0) return; tw_go(&g_cur[i].px, (float)(x * TILE), 200, 0, EZ_IN_OUT_CUBIC); tw_go(&g_cur[i].py, (float)(y * TILE), 200, 0, EZ_IN_OUT_CUBIC); }
static void cur_fade(int i, int ms) { if (i < 0) return; tw_go(&g_cur[i].op, 0, ms, 0, EZ_LINEAR); }

typedef struct {
    int used, kind;               /* 0 score popup, 1 message */
    Img img;
    float x, y;                   /* centre, pixels on the board */
    unsigned t0;
    int life;                     /* ms until fade starts (kind 1; <= 0 stays) */
    int zoom_ms, ease;
} Pop;
static Pop g_pop[24];

static void pop_free(Pop* p) { img_free(&p->img); p->used = 0; }
static void pops_clear(int kind) { int i; for (i = 0; i < 24; i++) if (g_pop[i].used && (kind < 0 || g_pop[i].kind == kind)) pop_free(&g_pop[i]); }
static Pop* pop_new(void) {
    int i;
    for (i = 0; i < 24; i++) if (!g_pop[i].used) { memset(&g_pop[i], 0, sizeof g_pop[i]); g_pop[i].used = 1; return &g_pop[i]; }
    return NULL;
}

void score_popup(int score, int hx, int hy) {
    char b[16];
    Pop* p = pop_new();
    if (!p) return;
    snprintf(b, sizeof b, "%d", score);
    if (text_img(&p->img, font_get("o36"), 0xffd700, 0x000000, b) != 0) { p->used = 0; return; }
    p->kind = 0; p->x = (float)hx; p->y = (float)hy; p->t0 = g_now; p->zoom_ms = 500; p->ease = EZ_OUT_BOUNCE;
}

static void message(const char* text, int life_s) {
    Pop* p = pop_new();
    if (!p) return;
    if (font_w(font_get("o52"), text) > BSIZE - 24) { if (text_img(&p->img, font_get("o36"), 0xffd700, 0x000000, text) != 0) { p->used = 0; return; } }
    else if (text_img(&p->img, font_get("o52"), 0xffd700, 0x000000, text) != 0) { p->used = 0; return; }
    p->kind = 1; p->x = BSIZE / 2.0f; p->y = BSIZE / 2.0f; p->t0 = g_now; p->zoom_ms = 500; p->ease = EZ_OUT_BACK;
    p->life = life_s > 0 ? life_s * 1000 : 0;
}

static void pops_update(void) {
    int i;
    for (i = 0; i < 24; i++) {
        Pop* p = &g_pop[i];
        if (!p->used) continue;
        if (p->kind == 0 && g_now - p->t0 >= 800) pop_free(p);
        else if (p->kind == 1 && p->life > 0 && g_now - p->t0 >= (unsigned)p->life + 500) pop_free(p);
    }
}

/* ------------------------------------------------------------------ game state (port of board_engine.c) */
typedef enum { ST_IDLE, ST_FIRST, ST_SECOND, ST_ILLEGAL, ST_MARK, ST_REFILL } GState;
static GState g_state = ST_IDLE;
static int g_running, g_paused, g_over_dim, g_cur_score, g_time_slice;
static int g_click, g_click_x, g_click_y;     /* "gem clicked" request from the pad */
static int g_x1, g_y1, g_x2, g_y2, g_cursor0 = -1;
static unsigned g_hint_at, g_hint_stop_at, g_over_at;
static Gem* g_hint_gem;
static int g_game_over_reported;

/* options saved to gweled.cfg */
static int g_sound = 1, g_hints = 1, g_music = 1, g_musicvol = 50, g_sfxvol = 100;
static char g_name[16] = "Player";

static void clear_cursors(void) { int i; for (i = 0; i < 4; i++) g_cur[i].used = 0; g_cursor0 = -1; }

static void fall_into_place(int new_board_anim);
static int g_fall_pending = -1;               /* the original re-checks every 10 ms until nothing moves */

static void fall_into_place(int new_board_anim) {
    int i, j, delay_incr, max_height = 0;
    if (gems_moving()) { g_fall_pending = new_board_anim; return; }
    g_fall_pending = -1;
    for (i = BW - 1; i >= 0; i--) {
        delay_incr = 0;
        for (j = BH - 1; j >= 0; j--) {
            Gem* g = E.obj[i][j];
            if (!g || g->y == j) continue;
            if (max_height < j) max_height = j;
            if (new_board_anim) gem_fall_effect(g, j, i * 100 + (BH - j) * 50);
            else gem_fall_to(g, j, delay_incr * 25, 100 + 20 * max_height);
            delay_incr++;
        }
    }
}

static void new_board(void) {                 /* fills E with a settled board and creates the gem objects above the screen */
    int i, j;
    g_silent = 1;
    eng_fill_new_board();
    g_silent = 0;
    gem_clear_all();
    for (i = 0; i < BW; i++) for (j = 0; j < BH; j++) E.obj[i][j] = gem_create(i, j - BH, E.board[i][j]);
}

static int bonus_fraction_256(void) {
    float f;
    if (E.next_bonus_at == E.previous_bonus_at) return 0;
    f = (E.total_removed - (float)E.previous_bonus_at) / (float)(E.next_bonus_at - E.previous_bonus_at);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return (int)(f * 256.0f);
}

static void hints_arm(void) { if (g_hints && g_running && !g_paused) g_hint_at = g_now + 15000; else g_hint_at = 0; }
static void hints_off(void) { g_hint_at = 0; }
static void bounce_stop(void) { if (g_hint_gem) { gem_fx(g_hint_gem, FX_NONE); g_hint_gem = NULL; } g_hint_stop_at = 0; }
static void show_hint(void) {
    int x, y;
    if (!g_running || g_paused) return;
    if (eng_find_move(&x, &y) && E.obj[x][y]) { bounce_stop(); g_hint_gem = E.obj[x][y]; gem_fx(g_hint_gem, FX_BOUNCE); g_hint_stop_at = g_now + 2000; }
}

static void engine_tick(void);
static void game_over(const char* msg) {
    fprintf(stderr, "gweled: game over (%s) frame %u score %d level %d mode %d\n", msg, plat_frame(), E.score, E.level, E.mode);
    message(msg, 0);
    g_over_dim = 1;
    g_running = 0; g_paused = 1; g_state = ST_IDLE;
    g_over_at = E.score > 0 ? g_now + 1000 : 0;
    g_game_over_reported = E.score <= 0;     /* nothing to record: the caller leaves for the menu on A */
    hints_off();
}

static void engine_tick(void) {
    char b[48];
    g_time_slice++;
    if (g_cur_score < E.score) g_cur_score += 10;

    if (E.mode == MODE_TIMED && g_running && !g_paused && g_time_slice % 25 == 0) {
        E.total_removed -= E.steps_for_timer;
        if (E.total_removed <= (float)E.previous_bonus_at) {
            if (g_state == ST_FIRST && E.obj[g_x1][g_y1]) gem_fx(E.obj[g_x1][g_y1], FX_NONE);
            clear_cursors();
            game_over("Time's up!");
        }
    }

    if (g_hint_at && g_click) hints_off();

    switch (g_state) {
    case ST_IDLE:
        if (g_click) {
            g_x1 = g_click_x; g_y1 = g_click_y;
            g_state = ST_FIRST;
            bounce_stop();
            gem_fx(E.obj[g_x1][g_y1], FX_BLINK);
            if (g_cursor0 >= 0) cur_kill(g_cursor0);
            g_cursor0 = cur_new(g_x1, g_y1);
            g_click = 0;
        }
        break;

    case ST_FIRST:
        if (g_click) {
            g_x2 = g_click_x; g_y2 = g_click_y;
            g_click = 0;
            if ((g_x1 == g_x2 && abs(g_y1 - g_y2) == 1) || (g_y1 == g_y2 && abs(g_x1 - g_x2) == 1)) {
                gem_fx(E.obj[g_x1][g_y1], FX_NONE);
                gem_move_to(E.obj[g_x1][g_y1], g_x2, g_y2);
                gem_move_to(E.obj[g_x2][g_y2], g_x1, g_y1);
                cur_move(g_cursor0, g_x2, g_y2);
                snd_play(SND_SWAP);
                g_state = ST_SECOND;
            } else if (g_x1 == g_x2 && g_y1 == g_y2) {
                if (g_cursor0 >= 0) { cur_kill(g_cursor0); g_cursor0 = -1; }
                gem_fx(E.obj[g_x1][g_y1], FX_NONE);
                g_state = ST_IDLE;
            } else {
                gem_fx(E.obj[g_x1][g_y1], FX_NONE);
                gem_fx(E.obj[g_x2][g_y2], FX_BLINK);
                g_x1 = g_x2; g_y1 = g_y2;
                if (g_cursor0 >= 0) cur_kill(g_cursor0);
                g_cursor0 = cur_new(g_x1, g_y1);
            }
        }
        break;

    case ST_SECOND:
        if (!E.obj[g_x1][g_y1]->anim && !E.obj[g_x2][g_y2]->anim) {
            eng_swap(g_x1, g_y1, g_x2, g_y2);
            if (!eng_part_of_alignment(g_x1, g_y1) && !eng_part_of_alignment(g_x2, g_y2)) {
                gem_move_to(E.obj[g_x1][g_y1], g_x2, g_y2);
                gem_move_to(E.obj[g_x2][g_y2], g_x1, g_y1);
                cur_move(g_cursor0, g_x1, g_y1);
                g_state = ST_ILLEGAL;
            } else {
                E.score_per_move = 0;
                g_state = ST_MARK;
            }
            if (g_cursor0 >= 0) cur_fade(g_cursor0, 200);
            g_cursor0 = -1;
        }
        break;

    case ST_ILLEGAL:
        if (!E.obj[g_x1][g_y1]->anim && !E.obj[g_x2][g_y2]->anim) { eng_swap(g_x1, g_y1, g_x2, g_y2); g_state = ST_IDLE; }
        break;

    case ST_MARK:
        if (eng_check_alignments()) {
            eng_take_down_gems();
            eng_remove_gems_and_score();
            snd_play(SND_EXPLODE);
            g_state = ST_REFILL;
            eng_refill();
            fall_into_place(0);
        } else if (!eng_find_move(NULL, NULL)) {
            if (E.mode == MODE_ENDLESS || E.mode == MODE_TIMED) {
                message("No moves left!", 2);
                fprintf(stderr, "gweled: reshuffle at frame %u\n", plat_frame());
                new_board();
                fall_into_place(0);
                g_state = ST_MARK;
            } else {
                game_over("No moves left!");
            }
        } else {
            E.do_not_score = 0;
            g_state = ST_IDLE;
        }
        break;

    case ST_REFILL:
        if (!gems_moving()) {
            if (E.total_removed >= (float)E.next_bonus_at && E.mode != MODE_ENDLESS) {
                E.previous_bonus_at = E.next_bonus_at;
                E.next_bonus_at *= 2;
                if (E.mode == MODE_TIMED) E.steps_for_timer = (float)(E.next_bonus_at - E.previous_bonus_at) / 180 + 1;
                E.bonus_multiply++; E.level++;
                fprintf(stderr, "gweled: level %d at frame %u score %d\n", E.level, plat_frame(), E.score);
                snprintf(b, sizeof b, "Bonus x%d", E.bonus_multiply >> 1);
                message(b, 2);
                eng_bonus_gems();
                eng_take_down_gems();
                eng_remove_gems_and_score();
                if (E.mode == MODE_TIMED) E.total_removed = (float)(E.next_bonus_at + E.previous_bonus_at) / 2;
                eng_refill();
                fall_into_place(0);
                E.do_not_score = 1;
            } else g_state = ST_MARK;
        }
        break;
    }

    if (g_state == ST_IDLE && !g_click && !g_hint_at && g_hints && g_running && !g_paused) hints_arm();
}

/* ------------------------------------------------------------------ files: options, scores, saved game */
#define CFG_FILE "gweled.cfg"
#define SCORE_FILE "gweled.scores"
#define SAVE_FILE "gweled.sav"

static void cfg_load(void) {
    FILE* f = fopen(CFG_FILE, "r");
    char line[96];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "sound=%d", &v) == 1) g_sound = v != 0;
        else if (sscanf(line, "hints=%d", &v) == 1) g_hints = v != 0;
        else if (sscanf(line, "music=%d", &v) == 1) g_music = v != 0;
        else if (sscanf(line, "sfxvol=%d", &v) == 1) g_sfxvol = v < 0 ? 0 : v > 100 ? 100 : v / 10 * 10;
        else if (sscanf(line, "musicvol=%d", &v) == 1) g_musicvol = v < 0 ? 0 : v > 100 ? 100 : v / 10 * 10;
        else if (!strncmp(line, "name=", 5)) {
            size_t n;
            snprintf(g_name, sizeof g_name, "%.12s", line + 5);
            n = strlen(g_name);
            while (n && (g_name[n - 1] == '\n' || g_name[n - 1] == '\r')) g_name[--n] = 0;
            if (!g_name[0]) strcpy(g_name, "Player");
        }
    }
    fclose(f);
}
static void cfg_save(void) {
    FILE* f = fopen(CFG_FILE, "w");
    if (!f) return;
    fprintf(f, "sound=%d\nsfxvol=%d\nmusic=%d\nmusicvol=%d\nhints=%d\nname=%s\n", g_sound, g_sfxvol, g_music, g_musicvol, g_hints, g_name);
    fclose(f);
}

#define NSC 10
typedef struct { char name[16]; int score; } Score;
static Score g_scores[2][NSC];

static int score_cmp(const void* a, const void* b) { return ((const Score*)b)->score - ((const Score*)a)->score; }   /* best first */

/* Only the ten best of each list are kept, whatever the file holds. */
static void scores_load(void) {
    FILE* f = fopen(SCORE_FILE, "r");
    char line[96];
    static Score tmp[2][256];
    int n[2] = { 0, 0 }, m, i;
    memset(g_scores, 0, sizeof g_scores);
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int mm, sc;
        char nm[16] = "";
        if (sscanf(line, "%d|%d|%15[^\r\n]", &mm, &sc, nm) >= 2 && (mm == 0 || mm == 1) && sc > 0 && n[mm] < 256) {
            tmp[mm][n[mm]].score = sc; snprintf(tmp[mm][n[mm]].name, 16, "%s", nm); n[mm]++;
        }
    }
    fclose(f);
    for (m = 0; m < 2; m++) {
        qsort(tmp[m], (size_t)n[m], sizeof(Score), score_cmp);
        for (i = 0; i < NSC && i < n[m]; i++) g_scores[m][i] = tmp[m][i];
    }
}
static void scores_save(void);
static void scores_clear(int mode) {          /* one list (0 Normal, 1 Timed); the file is removed when both are empty */
    int i, any = 0;
    memset(g_scores[mode], 0, sizeof g_scores[mode]);
    for (i = 0; i < NSC; i++) if (g_scores[0][i].score || g_scores[1][i].score) any = 1;
    if (any) scores_save(); else remove(SCORE_FILE);
}
static void scores_save(void) {
    FILE* f = fopen(SCORE_FILE, "w");
    int m, i;
    if (!f) return;
    for (m = 0; m < 2; m++) for (i = 0; i < NSC; i++) if (g_scores[m][i].score) fprintf(f, "%d|%d|%s\n", m, g_scores[m][i].score, g_scores[m][i].name);
    fclose(f);
}
static int score_rank(int mode, int score) {        /* 0..9 or -1 */
    int i;
    if (mode > 1 || score <= 0) return -1;
    for (i = 0; i < NSC; i++) if (score > g_scores[mode][i].score) return i;
    return -1;
}
static int score_insert(int mode, int score, const char* name) {
    int r = score_rank(mode, score), i;
    if (r < 0) return -1;
    for (i = NSC - 1; i > r; i--) g_scores[mode][i] = g_scores[mode][i - 1];
    g_scores[mode][r].score = score; snprintf(g_scores[mode][r].name, 16, "%s", name);
    scores_save();
    return r;
}

static int save_exists(void) { FILE* f = fopen(SAVE_FILE, "r"); if (f) fclose(f); return f != NULL; }
static void save_remove(void) { remove(SAVE_FILE); }
static void save_game(void) {
    FILE* f = fopen(SAVE_FILE, "w");
    int i, j;
    if (!f) return;
    fprintf(f, "gweled-save 1\n%d %d %.3f %d %d %d %d %.3f\n", E.mode, E.score, E.total_removed, E.bonus_multiply, E.previous_bonus_at, E.next_bonus_at, E.level, E.steps_for_timer);
    for (j = 0; j < BH; j++) { for (i = 0; i < BW; i++) fprintf(f, "%d ", E.board[i][j]); fprintf(f, "\n"); }
    fclose(f);
}
static int save_read(void) {                         /* validates everything; 0 = not usable */
    FILE* f = fopen(SAVE_FILE, "r");
    char hdr[32];
    int i, j, mode, score, bm, pb, nb, lv, ok = 0;
    float tr, st;
    int board[BW][BH];
    if (!f) return 0;
    if (fscanf(f, "%31s %*d", hdr) == 1 && !strcmp(hdr, "gweled-save") &&
        fscanf(f, "%d %d %f %d %d %d %d %f", &mode, &score, &tr, &bm, &pb, &nb, &lv, &st) == 8 &&
        mode >= 0 && mode <= 2 && score >= 0 && score < 100000000 && bm >= 3 && bm < 1000 && pb >= 0 && nb > pb && nb < (1 << 28) && lv >= 1 && lv < 1000 && st > 0 && st < 1e6f) {
        ok = 1;
        for (j = 0; j < BH && ok; j++) for (i = 0; i < BW; i++) { if (fscanf(f, "%d", &board[i][j]) != 1 || board[i][j] < 0 || board[i][j] > 6) { ok = 0; break; } }
    }
    fclose(f);
    if (!ok) return 0;
    eng_start_game(mode);
    E.score = score; E.total_removed = tr; E.bonus_multiply = bm; E.previous_bonus_at = pb; E.next_bonus_at = nb; E.level = lv; E.steps_for_timer = st;
    memset(E.ntiles, 0, sizeof E.ntiles);
    for (i = 0; i < BW; i++) for (j = 0; j < BH; j++) { E.board[i][j] = board[i][j]; E.ntiles[board[i][j]]++; }
    return 1;
}

/* ------------------------------------------------------------------ screens */
typedef enum { SC_TITLE, SC_GAME, SC_PAUSE, SC_CONFIRM, SC_NAME, SC_SCORES, SC_RESET, SC_HELP } Screen;
static Screen g_screen = SC_TITLE;
static int g_reset_sel;                      /* 0 Yes, 1 No (starts on No) */
static char g_toast_text[40];
static unsigned g_toast_until;               /* 'Scores cleared' notice on the scores screen */
static int g_menu, g_sel_main, g_help_page;     /* g_menu: 0 main, 1 Game, 2 Settings */
static int g_sel, g_pause_sel, g_conf_sel, g_scores_tab, g_scores_hl = -1, g_scores_from_game;
static int g_mode_playing;
static unsigned g_last_tick;
static int g_confirm_kind;                    /* 0: save this game?  1: abort this game? */
static int g_focus_x = 3, g_focus_y = 3;

static void start_game(int mode, int resume) {
    int i, j;
    gem_clear_all(); pops_clear(-1); clear_cursors();
    g_hint_gem = NULL; g_over_dim = 0; g_game_over_reported = 0; g_over_at = 0; g_cache_valid = 0;
    g_click = 0; g_paused = 0;
    if (resume) {
        /* objects for the saved board, in place; the state machine first re-checks for alignments */
        for (i = 0; i < BW; i++) for (j = 0; j < BH; j++) E.obj[i][j] = gem_create(i, j, E.board[i][j]);
        g_cur_score = E.score;
        g_state = ST_MARK;
    } else {
        eng_start_game(mode);
        g_cur_score = 0;
        new_board();
        fall_into_place(1);
        g_state = ST_IDLE;
    }
    g_running = 1;
    g_mode_playing = E.mode;
    g_last_tick = g_now;
    g_focus_x = 3; g_focus_y = 3;
    hints_off();
    g_screen = SC_GAME;
}

/* ---- drawing helpers ---- */
#define COL_GOLD 0xf8c700
#define COL_TEXT 0xe8dfd0
#define COL_DIM  0x9a8f88
#define COL_BG1  0x170b0d
#define COL_BG2  0x2c1a1c

static uint16_t g_static[SCR_W * SCR_H];      /* cached background + board + panel frame */
static uint16_t g_backdrop[SCR_W * SCR_H];    /* cached gradient + faint checker for the menu screens */
static uint16_t* g_screenbuf;                 /* the device/desktop frame buffer itself */

static void draw_panel_static(void);
static void build_static(void) {
    int i, j, px = PANEL_X, pw = PANEL_W;
    d_target(g_static);
    d_clip(0, 0, SCR_W, SCR_H);
    d_vgrad(0, 0, SCR_W, SCR_H, COL_BG2, COL_BG1);
    d_rect(BX - 3, BY - 3, BSIZE + 6, BSIZE + 6, 0x7a5a10);
    d_rect(BX - 2, BY - 2, BSIZE + 4, BSIZE + 4, COL_GOLD);
    for (j = 0; j < BH; j++) for (i = 0; i < BW; i++) d_rect(BX + i * TILE, BY + j * TILE, TILE, TILE, ((i + j) & 1) ? 0x4a3839 : 0x211010);
    d_round_a(px, BY - 3, pw, BSIZE + 6, 8, 0x000000, 110);
    d_round_a(px, BY - 3, pw, 1, 0, COL_GOLD, 90);
    draw_panel_static();
    d_target(g_screenbuf);
}

static void build_backdrop(void) {
    int i;
    d_target(g_backdrop);
    d_clip(0, 0, SCR_W, SCR_H);
    d_vgrad(0, 0, SCR_W, SCR_H, COL_BG2, COL_BG1);
    for (i = 0; i < 12 * 9; i++) { int cx = i % 12, cy = i / 12; if ((cx + cy) & 1) d_rect_a(cx * 56 - 16, cy * 56 - 16, 56, 56, 0xffffff, 6); }
    d_target(g_screenbuf);
}

static void draw_bg_only(void) {
    d_target(g_screenbuf);
    d_clip(0, 0, SCR_W, SCR_H);
    d_copy(g_backdrop);
}

/* Where and how each gem is drawn this frame (effects included). */
typedef struct { float x, y, sx, sy; int op, type; } GemDraw;
static int gem_params(GemDraw* out, int dim_all) {
    int i, n = 0;
    for (i = 0; i < MAXGEM; i++) {
        Gem* g = &g_gem[i];
        GemDraw* d;
        float op;
        if (!g->alive) continue;
        d = &out[n++];
        d->x = tw_get(&g->px) + TILE / 2.0f; d->y = tw_get(&g->py) + TILE / 2.0f;
        d->sx = tw_get(&g->sx); d->sy = tw_get(&g->sy); op = tw_get(&g->op);
        if (g->fx == FX_BLINK) {
            float ph = (float)((g_now - g->fx_t0) % 400u) / 200.0f;
            if (ph > 1) ph = 2 - ph;
            op = 255 - 100 * ph;
        } else if (g->fx == FX_BOUNCE) {
            unsigned t = (g_now - g->fx_t0) % 500u;
            float k = t < 350 ? ease(EZ_IN_OUT_QUAD, t / 350.0f) : 1.0f - ease(EZ_IN_OUT_QUAD, (t - 350) / 150.0f);
            d->y += TILE * 0.07f * k; d->sx *= 1.0f + 0.1f * k; d->sy *= 1.0f - 0.1f * k;
        }
        d->op = (int)op;
        if (dim_all) d->op /= 2;
        d->type = g->type;
    }
    return n;
}

static void draw_gems_from(const GemDraw* gd, int n) {
    int i;
    d_clip(BX, BY, BSIZE, BSIZE);
    for (i = 0; i < n; i++) {
        const GemDraw* d = &gd[i];
        if (d->sx > 1.01f || d->sy > 1.01f) d_img_s(&g_gemzoom[d->type], BX + d->x, BY + d->y, d->sx * TILE / 72.0f, d->sy * TILE / 72.0f, d->op);
        else d_img_s(&g_gemimg[d->type], BX + d->x, BY + d->y, d->sx, d->sy, d->op);
    }
    d_clip(0, 0, SCR_W, SCR_H);
}

/* The board with its gems is rendered into a cache and only redrawn when a gem moved, changed size or
 * opacity: most frames (waiting for a move) are then one memcpy. */
static uint16_t g_boardcache[SCR_W * SCR_H];
static unsigned g_cache_hash;
static uint16_t g_boardverify[SCR_W * SCR_H];
static unsigned g_verify_at, g_blank_at;
static int g_cache_heals;

/* Idle board check: every cell of the logical board must hold a live gem that sits in that cell, is fully opaque and unscaled. */
static int board_problems(int log) {
    int cx, cy, bad = 0;
    for (cx = 0; cx < BW; cx++) for (cy = 0; cy < BH; cy++) {
        Gem* o = E.obj[cx][cy];
        int ok = o && o->alive && !o->dying && o->x == cx && o->y == cy && o->type == E.board[cx][cy] &&
                 tw_get(&o->px) == (float)(cx * TILE) && tw_get(&o->py) == (float)(cy * TILE) && tw_get(&o->sx) == 1.0f && tw_get(&o->sy) == 1.0f && tw_get(&o->op) == 255.0f;
        if (ok) continue;
        bad++;
        if (log) fprintf(stderr, "gweled: CHECK frame %u: cell (%d,%d) board %d obj %p alive %d at (%d,%d) type %d drawn %.1f,%.1f scale %.2f,%.2f op %.0f\n", plat_frame(), cx, cy,
                         E.board[cx][cy], (void*)o, o ? o->alive : -1, o ? o->x : -1, o ? o->y : -1, o ? o->type : -1,
                         o ? tw_get(&o->px) : 0.0f, o ? tw_get(&o->py) : 0.0f, o ? tw_get(&o->sx) : 0.0f, o ? tw_get(&o->sy) : 0.0f, o ? tw_get(&o->op) : 0.0f);
    }
    return bad;
}

/* Cells of a rendered board that show the bare background tile (an idle board has a gem in every cell, so none may). Bit n = cell n. */
static unsigned long long blank_cells(const uint16_t* buf) {
    unsigned long long m = 0;
    int cx, cy, r;
    for (cx = 0; cx < BW; cx++) for (cy = 0; cy < BH; cy++) {
        int same = 1;
        for (r = 0; r < TILE && same; r++) {
            size_t o = (size_t)(BY + cy * TILE + r) * SCR_W + BX + cx * TILE;
            if (memcmp(buf + o, g_static + o, TILE * 2)) same = 0;
        }
        if (same) m |= 1ULL << (cy * BW + cx);
    }
    return m;
}

static void draw_one_gem(const GemDraw* d) {
    d_clip(BX, BY, BSIZE, BSIZE);
    if (d->sx > 1.01f || d->sy > 1.01f) d_img_s(&g_gemzoom[d->type], BX + d->x, BY + d->y, d->sx * TILE / 72.0f, d->sy * TILE / 72.0f, d->op);
    else d_img_s(&g_gemimg[d->type], BX + d->x, BY + d->y, d->sx, d->sy, d->op);
    d_clip(0, 0, SCR_W, SCR_H);
}

/* On the device a gem is now and then not drawn at all (the log says so: a fresh render with a bare cell). The cause is not known yet, so look:
 * the middle of every gem that is drawn solidly must differ from the bare background; if it does not, draw that gem again. */
static int g_miss_logged;
static int fix_missing_gems(uint16_t* dst, const GemDraw* gd, int n) {
    int i, tries, fixed = 0;
    for (i = 0; i < n; i++) {
        const GemDraw* d = &gd[i];
        int px, py, bad;
        if (d->op < 128 || d->sx < 0.5f || d->sy < 0.5f) continue;          /* a gem that is shrinking away has no solid middle */
        px = (int)(BX + d->x); py = (int)(BY + d->y);
        if (px < BX + 4 || px >= BX + BSIZE - 4 || py < BY + 4 || py >= BY + BSIZE - 4) continue;
        bad = dst[(size_t)py * SCR_W + px] == g_static[(size_t)py * SCR_W + px];
        for (tries = 0; bad && tries < 3; tries++) {
            draw_one_gem(d);
            bad = dst[(size_t)py * SCR_W + px] == g_static[(size_t)py * SCR_W + px];
            if (g_miss_logged < 40) fprintf(stderr, "gweled: CHECK frame %u: gem %d (type %d at %.1f,%.1f scale %.3f,%.3f op %d) was not drawn; draw again %d: %s\n", plat_frame(), i, d->type, d->x, d->y, d->sx, d->sy, d->op, tries + 1, bad ? "still missing" : "ok");
            g_miss_logged++;
        }
        if (!bad && tries) fixed++;
    }
    return fixed;
}

static void render_board(uint16_t* dst, const GemDraw* gd, int n) {
    d_target(dst); d_clip(0, 0, SCR_W, SCR_H); d_copy(g_static); draw_gems_from(gd, n);
    fix_missing_gems(dst, gd, n);
    d_target(g_screenbuf);
}

static unsigned sum_words(const void* p, size_t bytes) { const unsigned* w = (const unsigned*)p; unsigned a = 0; size_t i; for (i = 0; i < bytes / 4; i++) a = a * 31u + w[i]; return a; }
static unsigned data_sum(void) {          /* the static background and the gem pictures: they must never change */
    unsigned a = sum_words(g_static, sizeof g_static), i;
    for (i = 0; i < 7; i++) a = a * 31u + sum_words(g_gemimg[i].c565, (size_t)g_gemimg[i].w * g_gemimg[i].h * 2) + sum_words(g_gemimg[i].a8, (size_t)g_gemimg[i].w * g_gemimg[i].h / 4 * 4);
    return a;
}
static unsigned g_data_sum;

static void describe_cells(const char* what, unsigned long long m) {
    char b[400]; int i, n = 0;
    for (i = 0; i < BW * BH && n < (int)sizeof b - 12; i++) if (m >> i & 1) n += snprintf(b + n, sizeof b - (size_t)n, " (%d,%d)", i % BW, i / BW);
    fprintf(stderr, "gweled: CHECK frame %u: %s:%s\n", plat_frame(), what, b);
}

/* The board looked wrong on the device: gems that vanish for a while from their cells while the board is idle. Whatever the cause, an idle
 * board has a gem in every cell, so a cell showing the bare tile is wrong: draw the board again until none does, and say so in the log. */
static void heal_blank_cells(const GemDraw* gd, int n, unsigned long long blank) {
    int attempt;
    unsigned long long m = blank;
    if (g_cache_heals < 30) {
        unsigned now_sum = data_sum();
        describe_cells("blank cells in the board picture", blank);
        fprintf(stderr, "gweled: CHECK   background+gem pictures %s (sum %08x, was %08x)\n", now_sum == g_data_sum ? "unchanged" : "CHANGED", now_sum, g_data_sum);
    }
    g_cache_heals++;
    for (attempt = 0; attempt < 4 && m; attempt++) {
        render_board(g_boardverify, gd, n);
        m = blank_cells(g_boardverify);
        if (g_cache_heals <= 30) fprintf(stderr, "gweled: CHECK   fresh render %d: %s\n", attempt + 1, m ? "still has blank cells" : "complete");
        if (!m) break;
    }
    if (!m) memcpy(g_boardcache, g_boardverify, sizeof g_boardcache);
}

static void draw_board_cached(int dim_all) {
    GemDraw gd[MAXGEM];
    int n = gem_params(gd, dim_all), i, idle;
    unsigned h = 2166136261u + (unsigned)dim_all;
    static int s_check = -1;
    if (s_check < 0) s_check = getenv("GWL_CHECK") != NULL;
    for (i = 0; i < n; i++) {
        unsigned v[6];
        int k;
        v[0] = (unsigned)(int)(gd[i].x * 4); v[1] = (unsigned)(int)(gd[i].y * 4); v[2] = (unsigned)(int)(gd[i].sx * 256); v[3] = (unsigned)(int)(gd[i].sy * 256);
        v[4] = (unsigned)gd[i].op; v[5] = (unsigned)gd[i].type;
        for (k = 0; k < 6; k++) h = (h ^ v[k]) * 16777619u;
    }
    h ^= (unsigned)n << 20;
    idle = g_state == ST_IDLE && !gems_moving() && !dim_all && g_running && n == BW * BH && (!s_check || plat_frame() > 90);
    if (!g_data_sum) g_data_sum = data_sum();
    if (!g_cache_valid || h != g_cache_hash) {
        render_board(g_boardcache, gd, n);
        g_cache_hash = h; g_cache_valid = 1; g_verify_at = g_now + 2000; g_blank_at = g_now + 120;
    } else if (idle && g_now >= g_blank_at) {                    /* a few times a second: any blank cell in the picture? */
        unsigned long long m = blank_cells(g_boardcache);
        g_blank_at = g_now + 120;
        if (m) heal_blank_cells(gd, n, m);
    }
    if (s_check && getenv("GWL_FAULT") && idle && plat_frame() % 300 == 0) {        /* test hook: wipe one cell of the picture, as the device did */
        int cx = (int)(plat_frame() / 300) % BW, cy = (int)(plat_frame() / 600) % BH, r;
        for (r = 0; r < TILE; r++) { size_t o = (size_t)(BY + cy * TILE + r) * SCR_W + BX + cx * TILE; memcpy(g_boardcache + o, g_static + o, TILE * 2); }
    }
    if (idle && (s_check || g_now >= g_verify_at)) {
        /* The picture has not changed for a while: it must still be what a fresh render gives. */
        render_board(g_boardverify, gd, n);
        g_verify_at = g_now + 2000;
        if (memcmp(g_boardverify, g_boardcache, sizeof g_boardcache)) {
            unsigned long long mc = blank_cells(g_boardcache), mf = blank_cells(g_boardverify);
            if (g_cache_heals++ < 30) {
                fprintf(stderr, "gweled: CHECK frame %u: the board picture differs from a fresh render (state %d)\n", plat_frame(), g_state);
                if (mc) describe_cells("  blank in the picture", mc);
                if (mf) describe_cells("  blank in the fresh render", mf);
            }
            if (!mf) memcpy(g_boardcache, g_boardverify, sizeof g_boardcache);
        }
        if (board_problems(0) && g_cache_heals++ < 30) { fprintf(stderr, "gweled: CHECK idle board is wrong (state %d):\n", g_state); board_problems(1); }
    }
    d_copy(g_boardcache);
}

static void draw_cursors(void) {
    int i;
    d_clip(BX, BY, BSIZE, BSIZE);
    for (i = 0; i < 4; i++) {
        if (!g_cur[i].used) continue;
        if (!tw_busy(&g_cur[i].op) && g_cur[i].op.active && g_cur[i].op.to == 0) { g_cur[i].used = 0; continue; }
        d_img(&g_cursor_img, BX + (int)tw_get(&g_cur[i].px), BY + (int)tw_get(&g_cur[i].py), (int)tw_get(&g_cur[i].op));
    }
    d_clip(0, 0, SCR_W, SCR_H);
}

static void draw_focus(void) {
    int x = BX + g_focus_x * TILE, y = BY + g_focus_y * TILE;
    int pulse = 150 + (int)(90 * (0.5f + 0.5f * sinf((float)g_now / 160.0f)));
    d_clip(BX, BY, BSIZE, BSIZE);
    d_rect_a(x + 2, y + 2, TILE - 4, 2, 0xffffff, pulse); d_rect_a(x + 2, y + TILE - 4, TILE - 4, 2, 0xffffff, pulse);
    d_rect_a(x + 2, y + 4, 2, TILE - 8, 0xffffff, pulse); d_rect_a(x + TILE - 4, y + 4, 2, TILE - 8, 0xffffff, pulse);
    d_clip(0, 0, SCR_W, SCR_H);
}

static void draw_pops(void) {
    int i;
    d_clip(BX, BY, BSIZE, BSIZE);
    for (i = 0; i < 24; i++) {
        Pop* p = &g_pop[i];
        float age, z, cx, cy;
        int op = 255;
        if (!p->used) continue;
        age = (float)(g_now - p->t0);
        z = 0.8f + 0.2f * ease(p->ease, age / (float)p->zoom_ms);
        cx = BX + p->x; cy = BY + p->y;
        if (p->kind == 0) {
            cy -= TILE / 3.0f * (age > 800 ? 1.0f : age / 800.0f);
            if (age > 700) op = (int)(255 * (1.0f - (age - 700) / 100.0f));
        } else if (p->life > 0 && age > p->life) op = (int)(255 * (1.0f - (age - p->life) / 500.0f));
        if (op > 0) d_img_s(&p->img, cx, cy, z, z, op);
    }
    d_clip(0, 0, SCR_W, SCR_H);
}

/* The panel's fixed parts (title, labels, key legend) are painted once into the static layer. */
static void draw_panel_static(void) {
    int px = PANEL_X, pw = PANEL_W, cx = px + pw / 2, y = BY + 6;
    Font *f14 = font_get("f14"), *f24 = font_get("f24");
    static const char* const keys[5] = { "A", "B", "D-pad", "SELECT", "START" };
    static const char* const what[5] = { "Pick gem", "Cancel", "Move", "Hint", "Menu" };
    int k;
    d_text_c(f24, cx, y, COL_GOLD, 0, 255, "Gweled");
    y += 34;
    d_rect(px + 12, y, pw - 24, 1, 0x5a4020);
    y += 10;
    d_text_c(f14, cx, y, COL_DIM, 0, 255, "SCORE");
    y = BY + BSIZE - 128;
    d_rect(px + 12, y - 8, pw - 24, 1, 0x5a4020);
    for (k = 0; k < 5; k++, y += 20) { d_text(f14, px + 10, y, COL_GOLD, 0, 255, keys[k]); d_text(f14, px + 74, y, COL_TEXT, 0, 255, what[k]); }
}

static void draw_panel(void) {
    int px = PANEL_X, pw = PANEL_W, cx = px + pw / 2, y = BY + 6 + 34 + 10 + 16;
    char b[48];
    Font *f14 = font_get("f14"), *f18 = font_get("f18"), *f32 = font_get("f32");
    snprintf(b, sizeof b, "%06d", g_cur_score);
    if (font_w(f32, b) > pw - 24) d_text_c(font_get("f24"), cx, y + 6, 0xffffff, 0, 255, b);     /* 7+ digits */
    else d_text_c(f32, cx, y, 0xffffff, 0, 255, b);
    y += 50;
    if (g_mode_playing != MODE_ENDLESS) {
        int bw = pw - 24, fill = bonus_fraction_256() * bw / 256;
        snprintf(b, sizeof b, "Level %d", E.level);
        d_text_c(f18, cx, y, COL_TEXT, 0, 255, b);
        y += 26;
        d_round_a(px + 12, y, bw, 16, 7, 0x000000, 150);
        if (fill > 3) d_round_a(px + 12, y, fill, 16, 7, g_mode_playing == MODE_TIMED ? 0xd9531e : 0x3fae4a, 255);
        d_frame(px + 12, y, bw, 16, 0x5a4020);
        y += 26;
    } else {
        d_text_c(f18, cx, y, COL_TEXT, 0, 255, "Endless");
        y += 34;
    }
    d_text_c(f14, cx, y, COL_DIM, 0, 255, g_mode_playing == MODE_NORMAL ? "NORMAL GAME" : g_mode_playing == MODE_TIMED ? "TIMED GAME" : "ENDLESS GAME");
}

static void draw_game_screen(int hide_gems) {
    d_target(g_screenbuf);
    d_clip(0, 0, SCR_W, SCR_H);
    if (hide_gems) d_copy(g_static);
    else {
        draw_board_cached(g_over_dim);
        if (g_running) {
            draw_cursors();
            if (g_state == ST_IDLE || g_state == ST_FIRST) draw_focus();
        }
    }
    draw_pops();
    draw_panel();
}

/* ---- generic widgets ---- */
static void panel_box(int x, int y, int w, int h) {
    d_round_a(x, y, w, h, 10, 0x000000, 190);
    d_frame(x + 1, y + 1, w - 2, h - 2, 0x5a4020);
}

static void draw_menu_rows(int cx, int y, int w, int rowh, const char** rows, int n, int sel) {
    Font* f = font_get("f24");
    int i;
    for (i = 0; i < n; i++) {
        int ry = y + i * rowh;
        if (i == sel) { d_round_a(cx - w / 2, ry - 2, w, rowh - 4, 8, COL_GOLD, 70); d_frame(cx - w / 2, ry - 2, w, rowh - 4, COL_GOLD); }
        d_text_c(f, cx, ry + (rowh - 4 - font_h(f)) / 2 - 2, i == sel ? 0xffffff : COL_TEXT, 0, 255, rows[i]);
    }
}

/* ---- name entry ---- */
static char g_osk_buf[16];
static int g_osk_len, g_osk_cx, g_osk_cy;
static const char* const k_osk_rows[] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-_. ", "<!" };
static int g_osk_shift;      /* 1 = lower case */

/* names must fit the entry box (f24) and a high-score row (f18) */
#define NAME_W18 196
#define NAME_W24 272
static int name_fits(const char* s) { return font_w(font_get("f18"), s) <= NAME_W18 && font_w(font_get("f24"), s) <= NAME_W24; }

static void osk_open(const char* initial) {
    snprintf(g_osk_buf, sizeof g_osk_buf, "%s", initial);
    while (g_osk_buf[0] && !name_fits(g_osk_buf)) g_osk_buf[strlen(g_osk_buf) - 1] = 0;
    g_osk_len = (int)strlen(g_osk_buf); g_osk_cx = 0; g_osk_cy = 0; g_osk_shift = 0;
}

/* returns 1 when the name is accepted */
static int osk_input(unsigned press) {
    int w = (int)strlen(k_osk_rows[g_osk_cy]);
    if (press & B_LEFT) g_osk_cx = (g_osk_cx + w - 1) % w;
    if (press & B_RIGHT) g_osk_cx = (g_osk_cx + 1) % w;
    if (press & B_UP) { g_osk_cy = (g_osk_cy + 4) % 5; if (g_osk_cx >= (int)strlen(k_osk_rows[g_osk_cy])) g_osk_cx = (int)strlen(k_osk_rows[g_osk_cy]) - 1; }
    if (press & B_DOWN) { g_osk_cy = (g_osk_cy + 1) % 5; if (g_osk_cx >= (int)strlen(k_osk_rows[g_osk_cy])) g_osk_cx = (int)strlen(k_osk_rows[g_osk_cy]) - 1; }
    if (press & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) snd_play(SND_CLICK);
    if (press & B_X) g_osk_shift = !g_osk_shift;
    if (press & B_A) {
        char ch = k_osk_rows[g_osk_cy][g_osk_cx];
        if (ch == '<') { if (g_osk_len) g_osk_buf[--g_osk_len] = 0; snd_play(SND_CLICK); }
        else if (ch == '!') { if (g_osk_len) return 1; }
        else if (g_osk_len < 12) {
            if (g_osk_shift && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
            g_osk_buf[g_osk_len] = ch; g_osk_buf[g_osk_len + 1] = 0;
            if (name_fits(g_osk_buf)) { g_osk_len++; snd_play(SND_CLICK); }
            else g_osk_buf[g_osk_len] = 0;                       /* no room for another letter */
        }
    }
    if ((press & B_B) && g_osk_len) { g_osk_buf[--g_osk_len] = 0; snd_play(SND_CLICK); }
    if ((press & B_START) && g_osk_len) return 1;
    return 0;
}

static void osk_draw(int score) {
    Font *f14 = font_get("f14"), *f18 = font_get("f18"), *f24 = font_get("f24");
    int bx = 120, by = 40, bw = 400, bh = 400, r, c;
    char b[64];
    panel_box(bx, by, bw, bh);
    d_text_c(f24, SCR_W / 2, by + 14, COL_GOLD, 0, 255, "New High Score!");
    snprintf(b, sizeof b, "%d points", score);
    d_text_c(f18, SCR_W / 2, by + 46, COL_TEXT, 0, 255, b);
    d_round_a(bx + 50, by + 82, bw - 100, 40, 8, 0x000000, 160);
    d_frame(bx + 50, by + 82, bw - 100, 40, COL_GOLD);
    snprintf(b, sizeof b, "%s%s", g_osk_buf, (g_now / 400) & 1 ? "_" : " ");
    d_text_c(f24, SCR_W / 2, by + 88, 0xffffff, 0, 255, b);
    for (r = 0; r < 5; r++) {
        const char* row = k_osk_rows[r];
        int n = (int)strlen(row);
        for (c = 0; c < n; c++) {
            int kw = 30, kx = r == 4 ? bx + 60 + c * 150 : bx + 46 + c * 34, ky = by + 140 + r * 40;
            char lab[8] = { row[c], 0 };
            if (r == 4 && c == 0) strcpy(lab, "Del");
            if (r == 4 && c == 1) strcpy(lab, "OK");
            if (r == 4) kw = 130;
            if (g_osk_shift && lab[0] >= 'A' && lab[0] <= 'Z' && !lab[1]) lab[0] = (char)(lab[0] + 32);
            d_round_a(kx, ky, kw, 34, 6, r == g_osk_cy && c == g_osk_cx ? COL_GOLD : 0xffffff, r == g_osk_cy && c == g_osk_cx ? 200 : 40);
            d_text_c(f18, kx + kw / 2, ky + 6, r == g_osk_cy && c == g_osk_cx ? 0x000000 : COL_TEXT, 0, 255, lab);
        }
    }
    d_text_c(f14, SCR_W / 2, by + bh - 26, COL_DIM, 0, 255, "A type   B delete   X case   START accept");
}

/* ---- screens: title ---- */

typedef struct { const char* label; int id; } Row;
enum { R_CONTINUE = 1, R_NORMAL, R_TIMED, R_ENDLESS, R_SOUND, R_HINTS, R_MUSIC, R_MVOL, R_SVOL, R_GAME, R_SETTINGS, R_SCORES, R_HELP, R_EXIT };
static Row g_rows[12];
static char g_rowtxt[5][32];
static int g_nrows;

/* Main menu: Game, Settings, Scores, Help, Exit Game.  Game: [Continue Saved Game], Normal, Timed, Endless.
 * Settings: Sound Effects, Effects Volume, Music, Music Volume, Show Hints. */
static void build_title_rows(void) {
    g_nrows = 0;
    if (g_menu == 1) {
        if (save_exists()) g_rows[g_nrows++] = (Row){ "Continue Saved Game", R_CONTINUE };
        g_rows[g_nrows++] = (Row){ S_TITLE_NORMAL, R_NORMAL };
        g_rows[g_nrows++] = (Row){ S_TITLE_TIMED, R_TIMED };
        g_rows[g_nrows++] = (Row){ S_TITLE_ENDLESS, R_ENDLESS };
    } else if (g_menu == 2) {
        snprintf(g_rowtxt[0], sizeof g_rowtxt[0], "Sound Effects: %s", g_sound ? "On" : "Off");
        snprintf(g_rowtxt[1], sizeof g_rowtxt[1], "Show Hints: %s", g_hints ? "On" : "Off");
        snprintf(g_rowtxt[2], sizeof g_rowtxt[2], "Music: %s", g_music ? "On" : "Off");
        snprintf(g_rowtxt[3], sizeof g_rowtxt[3], "Music Volume: %d%%", g_musicvol);
        snprintf(g_rowtxt[4], sizeof g_rowtxt[4], "Effects Volume: %d%%", g_sfxvol);
        g_rows[g_nrows++] = (Row){ g_rowtxt[0], R_SOUND };
        g_rows[g_nrows++] = (Row){ g_rowtxt[4], R_SVOL };
        g_rows[g_nrows++] = (Row){ g_rowtxt[2], R_MUSIC };
        g_rows[g_nrows++] = (Row){ g_rowtxt[3], R_MVOL };
        g_rows[g_nrows++] = (Row){ g_rowtxt[1], R_HINTS };
    } else {
        g_rows[g_nrows++] = (Row){ "Game", R_GAME };
        g_rows[g_nrows++] = (Row){ "Settings", R_SETTINGS };
        g_rows[g_nrows++] = (Row){ "Scores", R_SCORES };
        g_rows[g_nrows++] = (Row){ "Help", R_HELP };
        g_rows[g_nrows++] = (Row){ "Exit Game", R_EXIT };
    }
}

static void menu_goto(int menu, int sel) { g_menu = menu; build_title_rows(); g_sel = sel < g_nrows ? sel : 0; }

/* Word-wraps s into maxw pixels; centred on x (centre != 0) or starting at x. Returns the y below the last line. */
static int wrap_lines(const Font* f, int x, int y, int maxw, uint32_t col, const char* s, int centre) {
    char line[200];
    const char* p = s;
    while (*p) {
        int n = 0, last = -1;
        while (p[n] && n < (int)sizeof line - 1) {
            line[n] = p[n]; line[n + 1] = 0;
            if (p[n] == ' ') last = n;
            if (font_w(f, line) > maxw) { if (last > 0) n = last; break; }
            n++;
        }
        memcpy(line, p, (size_t)n); line[n] = 0;
        if (centre) d_text_c(f, x, y, col, 0, 255, line); else d_text(f, x, y, col, 0, 255, line);
        y += font_h(f) + 2;
        p += n; while (*p == ' ') p++;
    }
    return y;
}
static void wrap_text(const Font* f, int cx, int y, int maxw, uint32_t col, const char* s) { wrap_lines(f, cx, y, maxw, col, s, 1); }

static const char* row_description(int id) {
    switch (id) {
    case R_GAME: return "Start a new game or pick up a saved one.";
    case R_SETTINGS: return "Sound effects, music, their volumes and hints.";
    case R_SCORES: return "The best scores of each game type.";
    case R_HELP: return "How to play, the controls and the game types.";
    case R_EXIT: return "Leave Gweled and go back to the menu.";
    case R_CONTINUE: return "Pick up the game you saved earlier.";
    case R_NORMAL: return S_DESC_NORMAL;
    case R_TIMED: return S_DESC_TIMED;
    case R_ENDLESS: return S_DESC_ENDLESS;
    case R_SOUND: return "Turn the sound effects on or off.";
    case R_SVOL: return "Left and Right change how loud the sound effects are.";
    case R_MUSIC: return "Turn the background music on or off.";
    case R_MVOL: return "Left and Right change how loud the music is.";
    case R_HINTS: return "Show a possible move after 15 seconds without one.";
    default: return "";
    }
}

static void draw_title(void) {
    int i, y, rowh = 40, pw = 260, head = g_menu ? 34 : 0, ph, maxw = 0;
    Font *f14 = font_get("f14");
    const char* rows[12];
    d_target(g_screenbuf);
    d_clip(0, 0, SCR_W, SCR_H);
    d_copy(g_backdrop);
    d_text_c(font_get("o52"), SCR_W / 2, 2, COL_GOLD, 0x000000, 255, "Gweled");
    /* the seven gems along the top, bouncing in a wave */
    for (i = 0; i < 7; i++) {
        float k = 0.5f + 0.5f * sinf((float)g_now / 380.0f - i * 0.7f);
        d_img_s(&g_gemimg[i], 320 + (i - 3) * 62, 100 - 5 * k, 0.9f, 0.9f, 255);
    }
    for (i = 0; i < g_nrows; i++) { rows[i] = g_rows[i].label; if (font_w(font_get("f24"), rows[i]) > maxw) maxw = font_w(font_get("f24"), rows[i]); }
    if (maxw + 56 > pw) pw = maxw + 56;
    ph = head + g_nrows * rowh + 16;
    y = 150;
    panel_box(320 - pw / 2, y - 10, pw, ph);
    if (g_menu) d_text_c(font_get("f18"), 320, y - 2, COL_GOLD, 0, 255, g_menu == 1 ? "Game" : "Settings");
    draw_menu_rows(320, y + head, pw - 24, rowh, rows, g_nrows, g_sel);
    wrap_text(f14, SCR_W / 2, 432, 600, COL_DIM, row_description(g_rows[g_sel].id));
    d_text_c(f14, SCR_W / 2, 460, 0x6a5a50, 0, 255, g_menu ? "Up/Down  choose     A  select     B  back" : "Up/Down  choose     A  select");
}

static void draw_scores(void) {
    Font *f14 = font_get("f14"), *f18 = font_get("f18"), *f24 = font_get("f24");
    int i, y = 108;
    char b[48];
    draw_bg_only();
    d_text_c(font_get("o36"), SCR_W / 2, 14, COL_GOLD, 0x000000, 255, "High Scores");
    for (i = 0; i < 3; i++) {
        int tx = 130 + i * 130;
        d_round_a(tx, 66, 120, 32, 8, g_scores_tab == i ? COL_GOLD : 0xffffff, g_scores_tab == i ? 200 : 30);
        d_text_c(f18, tx + 60, 71, g_scores_tab == i ? 0x000000 : COL_TEXT, 0, 255, i == 0 ? S_TITLE_NORMAL : i == 1 ? S_TITLE_TIMED : S_TITLE_ENDLESS);
    }
    panel_box(120, y - 6, 400, 10 * 32 + 12);
    if (g_scores_tab == 2) {
        d_text_c(f18, SCR_W / 2, y + 120, COL_TEXT, 0, 255, "Endless games are not scored.");
        d_text_c(f14, SCR_W / 2, y + 156, COL_DIM, 0, 255, "Play Normal or Timed to get on the board.");
    }
    for (i = 0; i < NSC && g_scores_tab < 2; i++) {
        const Score* s = &g_scores[g_scores_tab][i];
        int hl = (g_scores_hl == i && g_scores_tab == g_scores_from_game);
        if (hl) d_round_a(130, y + i * 32 - 1, 380, 30, 6, COL_GOLD, 60);
        snprintf(b, sizeof b, "%d.", i + 1);
        d_text_r(f18, 168, y + i * 32 + 2, COL_DIM, 0, 255, b);
        if (s->score) {
            char nm[16];
            int room;
            snprintf(b, sizeof b, "%d", s->score);
            room = 500 - font_w(f18, b) - 14 - 180;
            snprintf(nm, sizeof nm, "%s", s->name);
            while (nm[0] && font_w(f18, nm) > room) nm[strlen(nm) - 1] = 0;
            d_text(f18, 180, y + i * 32 + 2, hl ? 0xffffff : COL_TEXT, 0, 255, nm);
            d_text_r(f18, 500, y + i * 32 + 2, COL_GOLD, 0, 255, b);
        } else d_text(f18, 180, y + i * 32 + 2, 0x5a4a44, 0, 255, "-");
    }
    (void)f24;
    if (g_now < g_toast_until) d_text_c(f18, SCR_W / 2, 440, COL_GOLD, 0, 255, g_toast_text);
    else if (g_scores_tab < 2) d_text_c(f14, SCR_W / 2, 454, COL_DIM, 0, 255, g_scores_tab == 0 ? "Left/Right  game type     X  reset Normal scores     A or B  back" : "Left/Right  game type     X  reset Timed scores     A or B  back");
    else d_text_c(f14, SCR_W / 2, 454, COL_DIM, 0, 255, "Left/Right  game type     A or B  back");
}

static void draw_reset(void) {
    const char* rows[2] = { "Yes, clear them", "No, keep them" };
    char t[48], q[200];
    const char* nm = g_scores_tab == 0 ? S_TITLE_NORMAL : S_TITLE_TIMED;
    draw_scores();
    d_rect_a(0, 0, SCR_W, SCR_H, 0x000000, 170);
    panel_box(110, 120, 420, 250);
    snprintf(t, sizeof t, "Clear the %s Scores?", nm);
    d_text_c(font_get("f24"), SCR_W / 2, 134, COL_GOLD, 0, 255, t);
    snprintf(q, sizeof q, "This erases all the %s high scores. Once they are cleared they are gone for good and cannot be brought back. The other lists are not touched.", nm);
    wrap_text(font_get("f14"), SCR_W / 2, 176, 380, COL_TEXT, q);
    draw_menu_rows(SCR_W / 2, 262, 260, 40, rows, 2, g_reset_sel);
}

#define HELP_PAGES 5
static const char* const k_help_title[HELP_PAGES] = { "How to Play", "Controls", "Game Types", "Scoring and Levels", "About" };

static void help_key(int y_key, const char* key, const char* what, int* y) {
    Font* f = font_get("f14");
    d_text(f, 74, *y, COL_GOLD, 0, 255, key);
    *y = wrap_lines(f, 220, *y, 340, COL_TEXT, what, 0) + 7;
    (void)y_key;
}

static void draw_help(void) {
    Font *f14 = font_get("f14"), *f18 = font_get("f18"), *f24 = font_get("f24");
    int y = 100, i, W = 500;
    char b[32];
    draw_bg_only();
    d_text_c(font_get("o36"), SCR_W / 2, 10, COL_GOLD, 0x000000, 255, "Help");
    panel_box(50, 62, 540, 380);
    d_text_c(f24, SCR_W / 2, 72, COL_GOLD, 0, 255, k_help_title[g_help_page]);
    d_rect(80, 104, 480, 1, 0x5a4020);
    y = 116;
    switch (g_help_page) {
    case 0:
        y = wrap_lines(f18, 74, y, W, COL_TEXT, "Swap two neighbouring gems so that three or more of the same kind line up in a row or a column.", 0) + 8;
        y = wrap_lines(f18, 74, y, W, COL_TEXT, "The matched gems burst, the gems above fall down and new ones drop in from the top. Chain reactions keep scoring.", 0) + 8;
        y = wrap_lines(f18, 74, y, W, COL_TEXT, "A swap that makes no match slides back.", 0) + 8;
        y = wrap_lines(f18, 74, y, W, COL_TEXT, "The game ends when there are no moves left.", 0) + 12;
        for (i = 0; i < 7; i++) d_img_s(&g_gemimg[i], 320 + (i - 3) * 50, y + 26, 0.7f, 0.7f, 255);
        break;
    case 1:
        help_key(0, "D-pad", "Move the cursor around the board.", &y);
        help_key(0, "A", "Pick the gem under the cursor.", &y);
        help_key(0, "A, then D-pad", "Swap the gem with its neighbour that way.", &y);
        help_key(0, "B", "Put a picked gem down again.", &y);
        help_key(0, "SELECT", "Show a hint (a gem bounces).", &y);
        help_key(0, "START", "Pause menu: settings, save and quit.", &y);
        help_key(0, "In the menus", "D-pad chooses, A selects, B goes back.", &y);
        help_key(0, "SELECT + START", "Leave Gweled from any screen.", &y);
        break;
    case 2:
        d_text(f18, 74, y, COL_GOLD, 0, 255, S_TITLE_NORMAL); y = wrap_lines(f14, 74, y + 24, W, COL_TEXT, "Play until no moves are left. Fill the bar to reach the next level.", 0) + 10;
        d_text(f18, 74, y, COL_GOLD, 0, 255, S_TITLE_TIMED); y = wrap_lines(f14, 74, y + 24, W, COL_TEXT, "The bar drains as time passes and matches refill it. When it is empty the game is over. A new level refills it to the middle.", 0) + 10;
        d_text(f18, 74, y, COL_GOLD, 0, 255, S_TITLE_ENDLESS); y = wrap_lines(f14, 74, y + 24, W, COL_TEXT, "No levels and no end: when no moves are left the board is shuffled. Scores are not recorded.", 0) + 10;
        d_text(f18, 74, y, COL_GOLD, 0, 255, "Saving"); wrap_lines(f14, 74, y + 24, W, COL_TEXT, "Quit to Menu in the pause menu can save a Normal or Timed game. Continue it from the Game menu.", 0);
        break;
    case 3:
        y = wrap_lines(f14, 74, y, W, COL_TEXT, "A match scores 10 points for each gem beyond the second: three gems 10, four 20, five 30. The points are multiplied by the level's multiplier.", 0) + 8;
        y = wrap_lines(f14, 74, y, W, COL_TEXT, "Clear 100 gems for level 2, 200 for level 3, 400 for level 4, and so on. The bar shows how far you are.", 0) + 8;
        y = wrap_lines(f14, 74, y, W, COL_TEXT, "Each new level starts with a Bonus: eight gems burst for 10 points each. The multiplier goes up every second level: x2 at level 2, x3 at level 4, x4 at level 6.", 0) + 8;
        y = wrap_lines(f14, 74, y, W, COL_TEXT, "Hints: after 15 seconds without a move, a gem that can make a match bounces. SELECT shows one at once. Hints can be turned off in Settings.", 0) + 8;
        break;
    default:
        d_text_c(f18, SCR_W / 2, y, COL_TEXT, 0, 255, "A puzzle game with gems"); y += 30;
        d_text_c(f14, SCR_W / 2, y, COL_DIM, 0, 255, "Version 1.0-beta2 for the Datafrog SF3500"); y += 30;
        d_text_c(f14, SCR_W / 2, y, COL_TEXT, 0, 255, "Original game by Sebastien Delestaing and Daniele Napolitano"); y += 22;
        d_text_c(f14, SCR_W / 2, y, COL_TEXT, 0, 255, "With Wesley Ellis    Explode sound by Crunchpix"); y += 22;
        d_text_c(f14, SCR_W / 2, y, COL_TEXT, 0, 255, "gweled.org"); y += 34;
        d_text_c(f14, SCR_W / 2, y, COL_TEXT, 0, 255, "SF3500 version and music by AthruZ (Kryptikone316)"); y += 34;
        d_text_c(f14, SCR_W / 2, y, COL_DIM, 0, 255, "GNU General Public License, version 2 or later."); y += 22;
        d_text_c(f14, SCR_W / 2, y, COL_DIM, 0, 255, "Text set in DejaVu Sans Bold (free licence)."); y += 34;
        break;
    }
    snprintf(b, sizeof b, "Page %d of %d", g_help_page + 1, HELP_PAGES);
    d_text_c(f14, SCR_W / 2, 416, COL_DIM, 0, 255, b);
    if (g_help_page > 0) d_text(f14, 70, 416, COL_GOLD, 0, 255, "< Left");
    if (g_help_page < HELP_PAGES - 1) d_text_r(f14, 570, 416, COL_GOLD, 0, 255, "Right >");
    d_text_c(f14, SCR_W / 2, 456, COL_DIM, 0, 255, "Left/Right  page     B  back");
}

enum { P_RESUME, P_HINTS, P_SOUND, P_SVOL, P_MUSIC, P_MVOL, P_SCORES, P_QUIT, PAUSE_ROWS };
static void draw_pause(void) {
    char t[4][32];
    const char* rows[PAUSE_ROWS];
    d_target(g_screenbuf);
    d_clip(0, 0, SCR_W, SCR_H);
    d_copy(g_static);
    draw_pops();
    draw_panel();
    d_rect_a(BX, BY, BSIZE, BSIZE, 0x000000, 150);
    snprintf(t[0], sizeof t[0], "Show Hints: %s", g_hints ? "On" : "Off");
    snprintf(t[1], sizeof t[1], "Sound Effects: %s", g_sound ? "On" : "Off");
    snprintf(t[2], sizeof t[2], "Effects Volume: %d%%", g_sfxvol);
    snprintf(t[3], sizeof t[3], "Music: %s", g_music ? "On" : "Off");
    rows[P_RESUME] = "Resume"; rows[P_HINTS] = t[0]; rows[P_SOUND] = t[1]; rows[P_SVOL] = t[2]; rows[P_MUSIC] = t[3];
    { static char mv[32]; snprintf(mv, sizeof mv, "Music Volume: %d%%", g_musicvol); rows[P_MVOL] = mv; }
    rows[P_SCORES] = "Scores"; rows[P_QUIT] = "Quit to Menu";
    panel_box(BX + 74, BY + 24, 300, PAUSE_ROWS * 36 + 66);
    d_text_c(font_get("o36"), BX + BSIZE / 2, BY + 32, COL_GOLD, 0x000000, 255, "Paused");
    draw_menu_rows(BX + BSIZE / 2, BY + 86, 276, 36, rows, PAUSE_ROWS, g_pause_sel);
}

static void draw_confirm(void) {
    const char* q = g_confirm_kind == 0 ? "Save this game to continue later?" : "Do you really want to abort this game?";
    const char* rows[2] = { "Yes", "No" };
    d_target(g_screenbuf);
    d_clip(0, 0, SCR_W, SCR_H);
    d_copy(g_static);
    draw_panel();
    d_rect_a(BX, BY, BSIZE, BSIZE, 0x000000, 170);
    panel_box(BX + 40, BY + 110, 368, 200);
    wrap_text(font_get("f18"), BX + BSIZE / 2, BY + 130, 330, COL_TEXT, q);
    draw_menu_rows(BX + BSIZE / 2, BY + 200, 160, 38, rows, 2, g_conf_sel);
}

/* ---- audio / settings glue ---- */
static void apply_sound(void) { snd_enable(g_sound); snd_set_volume(g_sfxvol); }
static void apply_music(void) { music_set_volume(g_musicvol); music_set_enabled(g_music); }

/* ---- input with repeat ---- */
static unsigned g_prev_btn, g_rep_until[4];
static unsigned input_press(unsigned held) {
    static const unsigned dirs[4] = { B_LEFT, B_RIGHT, B_UP, B_DOWN };
    unsigned press = held & ~g_prev_btn;
    int i;
    for (i = 0; i < 4; i++) {
        if (!(held & dirs[i])) { g_rep_until[i] = 0; continue; }
        if (press & dirs[i]) g_rep_until[i] = g_now + 320;
        else if (g_now >= g_rep_until[i]) { press |= dirs[i]; g_rep_until[i] = g_now + 110; }
    }
    g_prev_btn = held;
    return press;
}

/* ---- play-field input ---- */
static void field_input(unsigned press) {
    int dx = 0, dy = 0;
    if (press & B_LEFT) dx = -1; else if (press & B_RIGHT) dx = 1; else if (press & B_UP) dy = -1; else if (press & B_DOWN) dy = 1;
    if (!g_running) return;
    if ((dx || dy) && g_state == ST_FIRST && !g_click) {
        /* a gem is selected: the direction swaps it with that neighbour */
        int tx = g_x1 + dx, ty = g_y1 + dy;
        if (tx >= 0 && tx < BW && ty >= 0 && ty < BH) { g_click = 1; g_click_x = tx; g_click_y = ty; g_focus_x = tx; g_focus_y = ty; hints_off(); }
        return;
    }
    if (dx || dy) {
        int nx = g_focus_x + dx, ny = g_focus_y + dy;
        if (nx >= 0 && nx < BW && ny >= 0 && ny < BH) { g_focus_x = nx; g_focus_y = ny; }
    }
    if (press & B_A) {
        if (g_state == ST_IDLE) { g_click = 1; g_click_x = g_focus_x; g_click_y = g_focus_y; bounce_stop(); hints_off(); snd_play(SND_CLICK); }
        else if (g_state == ST_FIRST) { g_click = 1; g_click_x = g_x1; g_click_y = g_y1; }          /* A again: deselect */
    }
    if ((press & B_B) && g_state == ST_FIRST) { g_click = 1; g_click_x = g_x1; g_click_y = g_y1; }
    if (press & B_SELECT) { if (g_state == ST_IDLE) show_hint(); }
}

/* the self-playing bot used by the headless tests (GWL_BOT=1): picks a gem, then its partner on the next tick */
static int g_bot, g_bot_frames, g_bot_start, g_bot_nx = -1, g_bot_ny;
static void bot_think(void) {
    int x, y, k;
    static const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
    if (g_state == ST_FIRST && !g_click && g_bot_nx >= 0) { g_click = 1; g_click_x = g_bot_nx; g_click_y = g_bot_ny; g_bot_nx = -1; return; }
    if (g_state == ST_FIRST && !g_click && g_bot_nx < 0) { g_click = 1; g_click_x = g_x1; g_click_y = g_y1; return; }     /* picked another gem: put it down */
    if (g_state != ST_IDLE || g_click || !g_running) return;
    if (g_bot > 1 && rng_range(0, 3) == 0) {                  /* GWL_BOT=2: sloppy human: illegal swaps, picking another gem, deselecting */
        int kind = rng_range(0, 3);
        x = rng_range(0, BW); y = rng_range(0, BH);
        g_click = 1; g_click_x = x; g_click_y = y;
        if (kind == 0) { g_bot_nx = x + (x < BW - 1 ? 1 : -1); g_bot_ny = y; }                 /* a neighbour, probably no match */
        else if (kind == 1) { g_bot_nx = (x + 3) % BW; g_bot_ny = (y + 2) % BH; }              /* not a neighbour: just selects that one */
        else { g_bot_nx = x; g_bot_ny = y; }                                                   /* the same gem: deselects */
        return;
    }
    if (!eng_find_move(&x, &y)) return;
    for (k = 0; k < 4; k++) {
        int nx = x + dx[k], ny = y + dy[k];
        if (nx < 0 || nx >= BW || ny < 0 || ny >= BH) continue;
        eng_swap(x, y, nx, ny);
        if (eng_part_of_alignment(x, y) || eng_part_of_alignment(nx, ny)) {
            eng_swap(x, y, nx, ny);
            g_click = 1; g_click_x = x; g_click_y = y; g_bot_nx = nx; g_bot_ny = ny;
            return;
        }
        eng_swap(x, y, nx, ny);
    }
}

/* ------------------------------------------------------------------ main */
static void leave_to_title(void) {
    g_running = 0; g_paused = 0; g_over_dim = 0;
    gem_clear_all(); pops_clear(-1); clear_cursors();
    menu_goto(0, 0); g_screen = SC_TITLE;
}
static void finish_game_to_scores(void) {
    /* the game is over: score entry (if it ranks) then the table */
    int mode = g_mode_playing;
    g_scores_from_game = mode;
    g_scores_tab = mode > 1 ? 0 : mode;
    if (mode <= 1 && score_rank(mode, E.score) >= 0) { osk_open(g_name); g_screen = SC_NAME; }
    else if (mode <= 1 && E.score > 0) { g_scores_hl = -1; g_screen = SC_SCORES; }
    else leave_to_title();
}

/* Which music fits what is on screen: the menu screens get the menu music, a game in progress (also while paused, or looking at
 * the scores or help from the pause menu) gets its game type's music. After a game is over the name entry and scores are menu again. */
static int music_scene(void) {
    int in_game = g_screen == SC_GAME || g_screen == SC_PAUSE || g_screen == SC_CONFIRM ||
                  ((g_screen == SC_SCORES || g_screen == SC_HELP || g_screen == SC_RESET) && (g_running || g_paused) && !g_over_dim);
    return in_game ? MS_NORMAL + (E.mode >= 0 && E.mode <= 2 ? E.mode : 0) : MS_MENU;
}

int main(void) {
    int i;
    unsigned seed;
    if (plat_init() != 0) return 1;
    plat_set_fps(50);
    for (i = 0; i < 7; i++) { char n[8]; snprintf(n, sizeof n, "g%d", i); img_get(&g_gemimg[i], n); snprintf(n, sizeof n, "gz%d", i); img_get(&g_gemzoom[i], n); }
    img_get(&g_cursor_img, "cursor");
    for (i = 0; i < 7; i++) { img_prepare(&g_gemimg[i]); img_prepare(&g_gemzoom[i]); }
    img_prepare(&g_cursor_img);
    snd_init();
    music_init();
    cfg_load(); apply_sound(); apply_music(); scores_load();
    seed = getenv("GWL_SEED") ? (unsigned)atoi(getenv("GWL_SEED")) : (unsigned)time(NULL) ^ (unsigned)plat_ticks();
    rng_seed(seed);
    g_bot = getenv("GWL_BOT") ? atoi(getenv("GWL_BOT")) : 0;
    g_bot_frames = getenv("GWL_BOT_FRAMES") ? atoi(getenv("GWL_BOT_FRAMES")) : 0;
    g_bot_start = getenv("GWL_BOT_START") ? atoi(getenv("GWL_BOT_START")) : 0;
    g_screenbuf = plat_fb();
    d_target(g_screenbuf);
    build_static();
    build_backdrop();
    build_title_rows();
    g_now = now_ms();
    if (getenv("GWL_AUTOSTART")) start_game(atoi(getenv("GWL_AUTOSTART")), 0);
    if (getenv("GWL_HELP")) { g_help_page = atoi(getenv("GWL_HELP")); g_screen = SC_HELP; }                 /* test hook */
    if (getenv("GWL_MSG")) message(getenv("GWL_MSG"), 0);                                   /* test hook: a game message */
    if (getenv("GWL_OSKTEST")) { E.score = atoi(getenv("GWL_OSKTEST")); g_mode_playing = 0; osk_open("WWWWWWWWWWWW"); g_screen = SC_NAME; }   /* test hook */

    for (;;) {
        unsigned held = plat_buttons(), press;
        g_now = now_ms();
        {   /* frame time statistics, one line per 20 s of play, for the log */
            static unsigned prev, win0, frames, worst;
            if (prev) { unsigned d = g_now - prev; if (d > worst) worst = d; frames++; }
            prev = g_now;
            if (!win0) win0 = g_now;
            if (g_now - win0 >= 20000) {
                if (g_running && frames) fprintf(stderr, "gweled: frames %u in %u ms (avg %u ms, worst %u ms)\n", frames, g_now - win0, (g_now - win0) / frames, worst);
                win0 = g_now; frames = 0; worst = 0;
            }
        }
        /* SELECT + START together leave the port from any screen: options are already on disk, then the display, the mixer
         * and SDL are shut down (plat_quit) and the process ends with 0, back to the front-end. */
        if ((held & (B_SELECT | B_START)) == (B_SELECT | B_START)) { fprintf(stderr, "gweled: SELECT+START, leaving\n"); cfg_save(); plat_quit(0); }
        press = input_press(held);
        music_set_scene(music_scene());
        music_update(g_now);

        gems_update();
        pops_update();
        if (g_fall_pending >= 0 && g_screen == SC_GAME) fall_into_place(g_fall_pending);

        switch (g_screen) {
        case SC_TITLE: {
            int id;
            if (press & B_UP) { g_sel = (g_sel + g_nrows - 1) % g_nrows; snd_play(SND_CLICK); }
            if (press & B_DOWN) { g_sel = (g_sel + 1) % g_nrows; snd_play(SND_CLICK); }
            id = g_rows[g_sel].id;
            if ((id == R_MVOL || id == R_SVOL) && (press & (B_LEFT | B_RIGHT | B_A | B_START))) {     /* 10% steps; A wraps round from 100 to 0 */
                int* vol = id == R_MVOL ? &g_musicvol : &g_sfxvol;
                int v = *vol + ((press & B_LEFT) ? -10 : 10);
                if (v > 100) v = (press & B_RIGHT) ? 100 : 0;
                *vol = v < 0 ? 0 : v;
                if (id == R_MVOL) music_set_volume(g_musicvol); else snd_set_volume(g_sfxvol);
                cfg_save(); build_title_rows(); snd_play(SND_CLICK);
                press &= ~(B_LEFT | B_RIGHT | B_A | B_START);
            }
            if ((press & (B_LEFT | B_RIGHT)) && (id == R_SOUND || id == R_HINTS || id == R_MUSIC)) press |= B_A;
            if (press & (B_A | B_START)) {
                snd_play(SND_CLICK);
                if (id == R_GAME) { g_sel_main = g_sel; menu_goto(1, 0); }
                else if (id == R_SETTINGS) { g_sel_main = g_sel; menu_goto(2, 0); }
                else if (id == R_SCORES) { g_sel_main = g_sel; g_scores_tab = 0; g_scores_hl = -1; g_scores_from_game = -1; g_screen = SC_SCORES; }
                else if (id == R_HELP) { g_sel_main = g_sel; g_help_page = 0; g_screen = SC_HELP; }
                else if (id == R_EXIT) plat_quit(0);                     /* clean exit: display, mixer and SDL are shut down there */
                else if (id == R_CONTINUE) { if (save_read()) { start_game(E.mode, 1); save_remove(); } else { save_remove(); menu_goto(1, 0); } }
                else if (id >= R_NORMAL && id <= R_ENDLESS) start_game(id - R_NORMAL, 0);
                else if (id == R_SOUND) { g_sound = !g_sound; apply_sound(); cfg_save(); build_title_rows(); }
                else if (id == R_HINTS) { g_hints = !g_hints; cfg_save(); build_title_rows(); }
                else if (id == R_MUSIC) { g_music = !g_music; apply_music(); cfg_save(); build_title_rows(); }
            }
            if (press & B_B) { if (g_menu) menu_goto(0, g_sel_main); else g_sel = g_nrows - 1; }
            draw_title();
            break;
        }
        case SC_GAME:
            if (press & B_START) {
                if (g_running) { g_paused = 1; g_pause_sel = 0; g_screen = SC_PAUSE; hints_off(); bounce_stop(); snd_play(SND_CLICK); break; }
            }
            if (getenv("GWL_TRACE") && plat_frame() % 100 == 0) fprintf(stderr, "TRACE frame %u state %d click %d bot_n %d score %d moving %d\n", plat_frame(), g_state, g_click, g_bot_nx, E.score, gems_moving());
            if (g_bot && (int)plat_frame() >= g_bot_start && (!g_bot_frames || (int)plat_frame() < g_bot_frames)) bot_think();
            field_input(press);
            /* the original board loop ran every 50 ms */
            { int n = 0; while (g_now - g_last_tick >= 50 && n < 4) { engine_tick(); g_last_tick += 50; n++; } if (n == 4) g_last_tick = g_now; }
            if (g_hint_at && g_now >= g_hint_at) { show_hint(); g_hint_at = g_now + 15000; }
            if (g_hint_stop_at && g_now >= g_hint_stop_at) bounce_stop();
            if (!g_running && g_over_dim) {
                if (g_over_at && g_now >= g_over_at) { g_over_at = 0; finish_game_to_scores(); }
                else if (!g_over_at && (press & (B_A | B_B | B_START))) finish_game_to_scores();
            }
            if (g_screen == SC_GAME) draw_game_screen(0);
            else if (g_screen == SC_NAME) { draw_bg_only(); osk_draw(E.score); }
            else if (g_screen == SC_SCORES) draw_scores();
            else if (g_screen == SC_TITLE) draw_title();
            break;
        case SC_PAUSE:
            if (press & (B_START | B_B)) { g_paused = 0; g_last_tick = g_now; g_screen = SC_GAME; g_click = 0; if (E.mode == MODE_TIMED) g_last_tick = g_now; hints_arm(); }
            if (press & B_UP) { g_pause_sel = (g_pause_sel + PAUSE_ROWS - 1) % PAUSE_ROWS; snd_play(SND_CLICK); }
            if (press & B_DOWN) { g_pause_sel = (g_pause_sel + 1) % PAUSE_ROWS; snd_play(SND_CLICK); }
            if (press & (B_A | B_LEFT | B_RIGHT)) {
                int sel = g_pause_sel;
                if (sel == P_RESUME && (press & B_A)) { g_paused = 0; g_last_tick = g_now; g_screen = SC_GAME; g_click = 0; hints_arm(); }
                else if (sel == P_HINTS) { g_hints = !g_hints; cfg_save(); snd_play(SND_CLICK); }
                else if (sel == P_SOUND) { g_sound = !g_sound; apply_sound(); cfg_save(); snd_play(SND_CLICK); }
                else if (sel == P_MUSIC) { g_music = !g_music; apply_music(); cfg_save(); snd_play(SND_CLICK); }
                else if (sel == P_SVOL || sel == P_MVOL) {                           /* 10% steps; A wraps round from 100 to 0 */
                    int* vol = sel == P_MVOL ? &g_musicvol : &g_sfxvol;
                    int v = *vol + ((press & B_LEFT) ? -10 : 10);
                    if (v > 100) v = (press & B_RIGHT) ? 100 : 0;
                    *vol = v < 0 ? 0 : v;
                    if (sel == P_MVOL) music_set_volume(g_musicvol); else snd_set_volume(g_sfxvol);
                    cfg_save(); snd_play(SND_CLICK);
                }
                else if (sel == P_SCORES && (press & B_A)) { g_scores_tab = g_mode_playing; g_scores_hl = -1; g_scores_from_game = -1; g_screen = SC_SCORES; }
                else if (sel == P_QUIT && (press & B_A)) { g_confirm_kind = (E.mode == MODE_ENDLESS) ? 1 : 0; g_conf_sel = 1; g_screen = SC_CONFIRM; if (g_confirm_kind == 0) g_conf_sel = 0; snd_play(SND_CLICK); }
            }
            draw_pause();
            break;
        case SC_CONFIRM:
            if (press & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) { g_conf_sel = !g_conf_sel; snd_play(SND_CLICK); }
            if (press & B_B) g_screen = SC_PAUSE;
            if (press & B_A) {
                if (g_conf_sel == 0 && g_confirm_kind == 0) save_game();
                if (g_conf_sel == 1 && g_confirm_kind == 0) save_remove();
                if (g_confirm_kind == 1 && g_conf_sel == 1) { g_screen = SC_PAUSE; }
                else leave_to_title();
            }
            draw_confirm();
            break;
        case SC_NAME:
            if (osk_input(press)) {
                snprintf(g_name, sizeof g_name, "%s", g_osk_buf); cfg_save();
                g_scores_hl = score_insert(g_mode_playing, E.score, g_name);
                g_scores_tab = g_mode_playing; g_scores_from_game = g_mode_playing;
                g_screen = SC_SCORES;
            }
            draw_bg_only(); osk_draw(E.score);
            break;
        case SC_SCORES:
            if (press & B_LEFT) { g_scores_tab = (g_scores_tab + 2) % 3; snd_play(SND_CLICK); }
            if (press & B_RIGHT) { g_scores_tab = (g_scores_tab + 1) % 3; snd_play(SND_CLICK); }
            if ((press & B_X) && g_scores_tab < 2) { g_reset_sel = 1; g_screen = SC_RESET; snd_play(SND_CLICK); }
            else if (press & (B_A | B_B | B_START)) {
                if (g_running || g_paused) { if (g_over_dim) leave_to_title(); else g_screen = SC_PAUSE; }
                else menu_goto(0, g_sel_main), g_screen = SC_TITLE;
            }
            if (g_screen == SC_SCORES) draw_scores();
            break;
        case SC_RESET:
            if (press & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) { g_reset_sel = !g_reset_sel; snd_play(SND_CLICK); }
            if (press & B_B) g_screen = SC_SCORES;
            if (press & B_A) {
                if (g_reset_sel == 0) {
                    scores_clear(g_scores_tab); g_scores_hl = -1; g_toast_until = g_now + 2500; snd_play(SND_EXPLODE);
                    snprintf(g_toast_text, sizeof g_toast_text, "%s scores cleared.", g_scores_tab == 0 ? S_TITLE_NORMAL : S_TITLE_TIMED);
                }
                g_screen = SC_SCORES;
            }
            if (g_screen == SC_RESET) draw_reset(); else draw_scores();
            break;
        case SC_HELP:
            if (press & B_LEFT) { if (g_help_page > 0) { g_help_page--; snd_play(SND_CLICK); } }
            if (press & B_RIGHT) { if (g_help_page < HELP_PAGES - 1) { g_help_page++; snd_play(SND_CLICK); } }
            if (press & B_B) { menu_goto(0, g_sel_main); g_screen = SC_TITLE; }
            draw_help();
            break;
        }
        plat_present();
    }
}
