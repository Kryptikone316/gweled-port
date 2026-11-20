/* Platform interface. The game never touches SDL or the device drivers directly;
 * plat_sdl.c implements this for the SF3500 (display/gamepad/audio drivers) and for
 * a desktop SDL2 window (development, screenshots, scripted tests). */
#ifndef PLAT_H
#define PLAT_H

#include <stdint.h>

#define SCR_W 640
#define SCR_H 480

/* Button bits returned by plat_buttons() (same order as the device's pad). */
enum {
    B_LEFT = 1 << 0, B_RIGHT = 1 << 1, B_UP = 1 << 2, B_DOWN = 1 << 3,
    B_A = 1 << 4, B_B = 1 << 5, B_L1 = 1 << 6, B_R1 = 1 << 7, B_X = 1 << 8,
    B_Y = 1 << 9, B_START = 1 << 10, B_SELECT = 1 << 11, B_L2 = 1 << 12, B_R2 = 1 << 13
};

int plat_init(void);                 /* 0 = ok */
uint16_t* plat_fb(void);             /* the 640x480 RGB565 screen, rows are SCR_W pixels apart */
void plat_present(void);             /* sends the frame, then sleeps to hold the frame rate */
unsigned plat_buttons(void);         /* held buttons */
unsigned plat_ticks(void);           /* milliseconds */
void plat_set_fps(int fps);
int plat_headless(void);
unsigned plat_frame(void);           /* frames presented so far */

/* Sounds are 48 kHz interleaved stereo S16; the buffer must stay valid. */
void plat_snd_set(int id, const short* pcm, int frames);
void plat_snd_play(int id, float gain);

void plat_quit(int code) __attribute__((noreturn));

#endif
