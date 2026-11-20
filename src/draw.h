/* Software drawing on the 640x480 RGB565 screen: rectangles, alpha-blended sprites (also scaled),
 * and baked DejaVu text with optional outline. Colours are 0xRRGGBB. */
#ifndef DRAW_H
#define DRAW_H
#include <stdint.h>

#define SCR_W 640
#define SCR_H 480

typedef struct {
    int w, h; const uint8_t* rgba; int owned;                 /* straight RGBA8888 */
    uint16_t* c565; uint8_t* a8; int16_t* span;               /* optional fast planes made by img_prepare: RGB565, alpha, per-row [x0,x1) */
} Img;
typedef struct Font Font;

void d_target(uint16_t* buf);
void d_clip(int x, int y, int w, int h);        /* 0,0,SCR_W,SCR_H clears the clip */
void d_rect(int x, int y, int w, int h, uint32_t rgb);
void d_rect_a(int x, int y, int w, int h, uint32_t rgb, int alpha);
void d_frame(int x, int y, int w, int h, uint32_t rgb);
void d_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void d_round_a(int x, int y, int w, int h, int r, uint32_t rgb, int alpha);   /* rounded rectangle */
void d_copy(const uint16_t* src);               /* whole screen from a cached layer */

int img_get(Img* out, const char* pakname);     /* 0 = ok */
void img_free(Img* im);
void img_prepare(Img* im);                      /* builds the fast planes (for sprites drawn many times a frame) */
void d_img(const Img* im, int x, int y, int opacity);                                   /* 1:1, opacity 0..255 */
void d_img_s(const Img* im, float cx, float cy, float sx, float sy, int opacity);       /* centred, bilinear */

Font* font_get(const char* pakname);
int font_h(const Font* f);
int font_w(const Font* f, const char* s);
void d_text(const Font* f, int x, int y, uint32_t rgb, uint32_t outline, int opacity, const char* s);
void d_text_c(const Font* f, int cx, int y, uint32_t rgb, uint32_t outline, int opacity, const char* s);
void d_text_r(const Font* f, int rx, int y, uint32_t rgb, uint32_t outline, int opacity, const char* s);
int text_img(Img* out, const Font* f, uint32_t rgb, uint32_t outline, const char* s);   /* for scaled drawing */

#endif
