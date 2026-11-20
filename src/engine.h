/* Gweled rules, ported from the original board_engine.c (Gweled 1.0-beta2, GPL-2.0-or-later):
 * same tile balancing, alignment detection, scoring, levels and timed-mode drain. The GLib/Clutter
 * glue is gone; objects (the animated gems) are created through gem_create() supplied by the game. */
#ifndef ENGINE_H
#define ENGINE_H

#define BW 8
#define BH 8

enum { MODE_NORMAL = 0, MODE_TIMED = 1, MODE_ENDLESS = 2 };

typedef struct Gem Gem;
extern int gem_y(const Gem* g);                          /* the gem's logical row (may be < 0 above the board) */
extern Gem* gem_create(int x, int y, int type);          /* y in rows; new gems start above the board */
extern void gem_destroy_anim(Gem* g);                    /* pop and vanish */
extern void score_popup(int score, int hot_x, int hot_y);/* hot spot in pixels relative to the board */

typedef struct { int dir, x, y, len; } Align;            /* dir 0 = horizontal, 1 = vertical, len 1 = a single bonus gem */

typedef struct {
    int board[BW][BH];       /* gem type 0..6, -1 empty */
    Gem* obj[BW][BH];
    int ntiles[7];
    int mode, score;
    float total_removed;
    int bonus_multiply, previous_bonus_at, next_bonus_at, level;
    float steps_for_timer;
    int score_per_move, do_not_score;
    Align al[96];
    int nal;
} Eng;

extern Eng E;

void rng_seed(unsigned s);
int rng_range(int lo, int hi);           /* [lo, hi) like g_rand_int_range */

int eng_new_tile(void);
int eng_part_of_alignment(int x, int y);                 /* bit 0 horizontal, bit 1 vertical */
int eng_find_move(int* px, int* py);                     /* 1 if a move exists; (px,py) = a gem to move */
void eng_swap(int x1, int y1, int x2, int y2);
void eng_refill(void);
int eng_check_alignments(void);                          /* fills E.al; 1 if any */
void eng_remove_gems_and_score(void);                    /* clears the cells, adds the score, popups */
void eng_take_down_gems(void);                           /* starts the pop animation of the aligned gems */
void eng_bonus_gems(void);                               /* level-up: 8 random single gems */
void eng_fill_new_board(void);
void eng_start_game(int mode);

#endif
