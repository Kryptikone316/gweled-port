#include "engine.h"
#include <string.h>
#include <stdlib.h>

#define FIRST_BONUS_AT 100
#define NB_BONUS_GEMS 8
#define TOTAL_STEPS_FOR_TIMER 180
#include "game.h"

Eng E;

/* ---- own generator (the original used GRand): same sequence on every platform ---- */
static unsigned long long rs = 88172645463325252ULL;
void rng_seed(unsigned s) { rs = 88172645463325252ULL ^ ((unsigned long long)s * 2685821657736338717ULL); if (!rs) rs = 1; }
static unsigned rng_u32(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 16); }
int rng_range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(rng_u32() % (unsigned)(hi - lo)); }

static const unsigned char bit_n[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

int eng_new_tile(void) {
    int i, min, max, min_index = 0, max_index = 0, previous_min_index = 0;
    min = max = E.ntiles[0];
    for (i = 0; i < 7; i++) {
        if (E.ntiles[i] < min) { min = E.ntiles[i]; min_index = i; previous_min_index = min_index; }
        if (E.ntiles[i] > max) { max = E.ntiles[i]; max_index = i; }
    }
    i = rng_range(0, 2);
    switch (i) {
    case 0: return rng_range(0, 2) ? min_index : previous_min_index;
    default: return (max_index + (signed char)rng_range(1, 7)) % 7;
    }
}

int eng_part_of_alignment(int x, int y) {
    int i, result = 0;
    for (i = x - 2; i <= x; i++)
        if (i >= 0 && i + 2 < BW)
            if (bit_n[E.board[i][y] & 7] & bit_n[E.board[i + 1][y] & 7] & bit_n[E.board[i + 2][y] & 7]) { result |= 1; break; }
    for (i = y - 2; i <= y; i++)
        if (i >= 0 && i + 2 < BH)
            if (bit_n[E.board[x][i] & 7] & bit_n[E.board[x][i + 1] & 7] & bit_n[E.board[x][i + 2] & 7]) { result |= 2; break; }
    return result;
}

/* The original indexes gpc_bit_n[-1] (UB) for empty cells; a & 7 keeps the lookup in range. Empty cells (-1) only exist while
 * gems are being removed, never when alignments are tested for play, so behaviour is unchanged. */

int eng_find_move(int* pi, int* pj) {
    int i, j;
    for (j = 0; j <= BH - 1; j++)
        for (i = 0; i <= BW - 1; i++) {
            if (i > 0) {
                eng_swap(i - 1, j, i, j);
                if (eng_part_of_alignment(i, j)) { eng_swap(i - 1, j, i, j); i = i - 1; goto found; }
                eng_swap(i - 1, j, i, j);
            }
            if (i < 7) {
                eng_swap(i + 1, j, i, j);
                if (eng_part_of_alignment(i, j)) { eng_swap(i + 1, j, i, j); i = i + 1; goto found; }
                eng_swap(i + 1, j, i, j);
            }
            if (j > 0) {
                eng_swap(i, j - 1, i, j);
                if (eng_part_of_alignment(i, j)) { eng_swap(i, j - 1, i, j); j = j - 1; goto found; }
                eng_swap(i, j - 1, i, j);
            }
            if (j < 7) {
                eng_swap(i, j + 1, i, j);
                if (eng_part_of_alignment(i, j)) { eng_swap(i, j + 1, i, j); j = j + 1; goto found; }
                eng_swap(i, j + 1, i, j);
            }
        }
    return 0;
found:
    if (pi && pj) { *pi = i; *pj = j; }
    return 1;
}

void eng_swap(int x1, int y1, int x2, int y2) {
    Gem* o = E.obj[x1][y1];
    int t;
    E.obj[x1][y1] = E.obj[x2][y2]; E.obj[x2][y2] = o;
    t = E.board[x1][y1]; E.board[x1][y1] = E.board[x2][y2]; E.board[x2][y2] = t;
}

void eng_refill(void) {
    int i, j, k, last_tile = -1, same_tile_count = 1;
    for (i = 0; i < BW; i++)
        for (j = 0; j < BH; j++)
            if (E.board[i][j] == -1) {
                for (k = j; k > 0; k--) { E.board[i][k] = E.board[i][k - 1]; E.obj[i][k] = E.obj[i][k - 1]; }
                E.board[i][0] = eng_new_tile();
                if (last_tile == E.board[i][0]) same_tile_count++; else same_tile_count = 1;
                if (same_tile_count >= 3) {         /* never 3 of a kind in a row while refilling */
                    do { E.board[i][0] = eng_new_tile(); } while (last_tile == E.board[i][0]);
                }
                last_tile = E.board[i][0];
                E.ntiles[E.board[i][0]]++;
                if (j && E.obj[i][1]) E.obj[i][0] = gem_create(i, gem_y(E.obj[i][1]) - 1, E.board[i][0]);
                else E.obj[i][0] = gem_create(i, -1, E.board[i][0]);
            }
}

static void delete_alignment(const Align* a) {
    int i, total, removed = 0, hx, hy;
    if (a->dir == 0) {
        hx = a->x * TILE + a->len * TILE / 2; hy = a->y * TILE + TILE / 2;
        for (i = a->x; i < a->x + a->len; i++)
            if (E.board[i][a->y] != -1) { removed++; E.ntiles[E.board[i][a->y]]--; E.board[i][a->y] = -1; }
    } else {
        hx = a->x * TILE + TILE / 2; hy = a->y * TILE + a->len * TILE / 2;
        for (i = a->y; i < a->y + a->len; i++)
            if (E.board[a->x][i] != -1) { removed++; E.ntiles[E.board[a->x][i]]--; E.board[a->x][i] = -1; }
    }
    if (a->len == 1) {
        total = 10 * rng_range(1, 2);                       /* level-up bonus gem */
    } else {
        total = 10 * (E.bonus_multiply >> 1) * (a->len - 2) + E.score_per_move;
        if (E.do_not_score) E.score_per_move = total;
    }
    if (!E.do_not_score) {
        E.total_removed += (float)removed;
        E.score += total;
        score_popup(total, hx, hy);
    }
}

void eng_remove_gems_and_score(void) { int i; for (i = 0; i < E.nal; i++) delete_alignment(&E.al[i]); }

void eng_take_down_gems(void) {
    int i, k;
    for (i = 0; i < E.nal; i++) {
        const Align* a = &E.al[i];
        if (a->dir == 0) for (k = a->x; k < a->x + a->len; k++) { if (E.obj[k][a->y]) gem_destroy_anim(E.obj[k][a->y]); E.obj[k][a->y] = NULL; }
        else for (k = a->y; k < a->y + a->len; k++) { if (E.obj[a->x][k]) gem_destroy_anim(E.obj[a->x][k]); E.obj[a->x][k] = NULL; }
    }
}

void eng_bonus_gems(void) {
    int i;
    E.nal = 0;
    for (i = 0; i < NB_BONUS_GEMS; i++) {
        Align* a = &E.al[E.nal++];
        a->x = rng_range(0, 7); a->y = rng_range(0, 7); a->dir = 0; a->len = 1;
    }
}

static void add_align(int dir, int x, int y, int len) {
    if (E.nal < (int)(sizeof E.al / sizeof E.al[0])) { Align* a = &E.al[E.nal++]; a->dir = dir; a->x = x; a->y = y; a->len = len; }
}

int eng_check_alignments(void) {
    int i, j, n, sx = 0, sy = 0;
    E.nal = 0;
    n = 0;
    for (i = 0; i < BW; i++) {
        for (j = 0; j < BH; j++)
            if ((eng_part_of_alignment(i, j) & 2) == 2) { if (n == 0) { sx = i; sy = j; } n++; }
            else { if (n > 2) add_align(1, sx, sy, n); n = 0; }
        if (n > 2) add_align(1, sx, sy, n);
        n = 0;
    }
    n = 0;
    for (j = 0; j < BH; j++) {
        for (i = 0; i < BW; i++)
            if ((eng_part_of_alignment(i, j) & 1) == 1) { if (n == 0) { sx = i; sy = j; } n++; }
            else { if (n > 2) add_align(0, sx, sy, n); n = 0; }
        if (n > 2) add_align(0, sx, sy, n);
        n = 0;
    }
    return E.nal != 0;
}

void eng_fill_new_board(void) {
    int i, j;
    memset(E.ntiles, 0, sizeof E.ntiles);
    for (i = 0; i < BW; i++)
        for (j = 0; j < BH; j++) {
            E.board[i][j] = eng_new_tile();
            E.ntiles[E.board[i][j]]++;
            E.obj[i][j] = gem_create(i, j - BH, E.board[i][j]);
        }
    E.do_not_score = 1;
    while (eng_check_alignments()) {
        /* the starting board is settled silently: remove aligned gems and refill, no scoring, no animation objects kept */
        eng_remove_gems_and_score();
        eng_refill();
    }
    E.do_not_score = 0;
    /* objects created while settling are stale; the caller recreates them from the final board */
}

void eng_start_game(int mode) {
    memset(&E, 0, sizeof E);
    E.mode = mode;
    E.score = 0; E.score_per_move = 0; E.bonus_multiply = 3; E.level = 1;
    E.previous_bonus_at = 0; E.next_bonus_at = FIRST_BONUS_AT;
    E.steps_for_timer = FIRST_BONUS_AT / (float)TOTAL_STEPS_FOR_TIMER;
    E.total_removed = mode == MODE_TIMED ? FIRST_BONUS_AT / 2 : 0;
}
