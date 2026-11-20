/* Gweled for the SF3500: game objects, animation and the state machine (port of the original board loop). */
#ifndef GAME_H
#define GAME_H

#include "engine.h"

#define TILE 56
#define BX 12
#define BY 16
#define BSIZE (TILE * BW)
#define PANEL_X (BX + BSIZE + 8)          /* the score panel to the right of the board */
#define PANEL_W (640 - PANEL_X - 6)

/* easing modes (the Clutter ones the original used) */
enum { EZ_LINEAR, EZ_OUT_CUBIC, EZ_IN_OUT_CUBIC, EZ_IN_QUAD, EZ_IN_OUT_QUAD, EZ_OUT_BOUNCE, EZ_OUT_BACK };

typedef struct { float from, to; unsigned t0; int dur, delay, ease, active; } Tw;

enum { FX_NONE = 0, FX_BLINK = 1, FX_BOUNCE = 2 };

struct Gem {
    int alive, type, x, y;        /* x, y: logical cell (y may be negative above the board) */
    Tw px, py, sx, sy, op;        /* pixel position on the board, scale, opacity (0..255) */
    int anim;                     /* the original's "animating" flag */
    int fx; unsigned fx_t0;
    int dying; unsigned die_t0;
};

#endif
