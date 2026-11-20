#include "draw.h"
#include "pak.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef DRAW_DEBUG
/* Test hook (drawtest): what the last d_img_s / d_img call saw and did, to find out why a gem is now and then not drawn on the device. */
typedef struct { int stage, w, h, opacity, dw, dh, x0, y0, cx0, cy0, cx1, cy1, x, y, cw, ch, sx0, sy0, fast, rows, a0_first, a1_first, o0_first, o1_first; float cxf, cyf, sxf, syf; } DbgRec;
DbgRec g_dbg;
#define DBG(f, v) (g_dbg.f = (v))
#else
#define DBG(f, v) ((void)0)
#endif
static uint16_t* g_buf;
static int cx0 = 0, cy0 = 0, cx1 = SCR_W, cy1 = SCR_H;

#define P565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void d_target(uint16_t* buf) { g_buf = buf; }

void d_clip(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > SCR_W ? SCR_W : x + w; cy1 = y + h > SCR_H ? SCR_H : y + h;
}

static inline void blend_px(uint16_t* d, int r, int g, int b, int a) {   /* a 1..255 */
    uint16_t p = *d;
    int dr = (p >> 8) & 0xF8, dg = (p >> 3) & 0xFC, db = (p << 3) & 0xF8;
    dr += ((r - dr) * a) >> 8; dg += ((g - dg) * a) >> 8; db += ((b - db) * a) >> 8;
    *d = P565(dr, dg, db);
}

void d_rect(int x, int y, int w, int h, uint32_t rgb) {
    uint16_t c = P565((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h, i, j;
    for (j = y0; j < y1; j++) { uint16_t* d = g_buf + j * SCR_W; for (i = x0; i < x1; i++) d[i] = c; }
}

void d_rect_a(int x, int y, int w, int h, uint32_t rgb, int alpha) {
    int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h, i, j;
    if (alpha <= 0) return;
    if (alpha >= 255) { d_rect(x, y, w, h, rgb); return; }
    for (j = y0; j < y1; j++) { uint16_t* d = g_buf + j * SCR_W; for (i = x0; i < x1; i++) blend_px(&d[i], r, g, b, alpha); }
}

void d_frame(int x, int y, int w, int h, uint32_t rgb) {
    d_rect(x, y, w, 1, rgb); d_rect(x, y + h - 1, w, 1, rgb);
    d_rect(x, y, 1, h, rgb); d_rect(x + w - 1, y, 1, h, rgb);
}

void d_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    int j;
    for (j = 0; j < h; j++) {
        int t = h > 1 ? j * 256 / (h - 1) : 0;
        int r = (int)((top >> 16) & 255) + ((((int)((bottom >> 16) & 255) - (int)((top >> 16) & 255)) * t) >> 8);
        int g = (int)((top >> 8) & 255) + ((((int)((bottom >> 8) & 255) - (int)((top >> 8) & 255)) * t) >> 8);
        int b = (int)(top & 255) + ((((int)(bottom & 255) - (int)(top & 255)) * t) >> 8);
        d_rect(x, y + j, w, 1, ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
}

/* rounded rectangle: corner pixels outside the radius are skipped, edge pixels get partial alpha */
void d_round_a(int x, int y, int w, int h, int r, uint32_t rgb, int alpha) {
    int j;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (j = 0; j < h; j++) {
        int inset = 0;
        if (j < r) { int dy = r - j; int k = r; while (k * k + dy * dy > r * r + r && k > 0) k--; inset = r - k; }
        else if (j >= h - r) { int dy = j - (h - r - 1); int k = r; while (k * k + dy * dy > r * r + r && k > 0) k--; inset = r - k; }
        d_rect_a(x + inset, y + j, w - 2 * inset, 1, rgb, alpha);
    }
}

void d_copy(const uint16_t* src) { memcpy(g_buf, src, (size_t)SCR_W * SCR_H * 2); }

/* ---- images ---- */
int img_get(Img* out, const char* name) {
    uint32_t sz;
    const uint8_t* p = pak_get(name, &sz);
    if (!p) { fprintf(stderr, "gweled: missing asset %s\n", name); memset(out, 0, sizeof *out); return -1; }
    out->w = p[0] | (p[1] << 8); out->h = p[2] | (p[3] << 8); out->rgba = p + 4; out->owned = 0;
    return 0;
}

void img_free(Img* im) {
    if (im->owned && im->rgba) free((void*)im->rgba);
    free(im->c565); free(im->a8); free(im->span);
    memset(im, 0, sizeof *im);
}

void img_prepare(Img* im) {
    int i, j, n = im->w * im->h;
    im->c565 = (uint16_t*)malloc((size_t)n * 2); im->a8 = (uint8_t*)malloc((size_t)n); im->span = (int16_t*)malloc((size_t)im->h * 8);
    if (!im->c565 || !im->a8 || !im->span) { free(im->c565); free(im->a8); free(im->span); im->c565 = NULL; im->a8 = NULL; im->span = NULL; return; }
    for (j = 0; j < im->h; j++) {
        int x0 = im->w, x1 = 0;
        for (i = 0; i < im->w; i++) {
            const uint8_t* p = im->rgba + ((size_t)j * im->w + i) * 4;
            im->c565[j * im->w + i] = P565(p[0], p[1], p[2]);
            im->a8[j * im->w + i] = p[3];
            if (p[3]) { if (i < x0) x0 = i; x1 = i + 1; }
        }
        if (x1 <= x0) { x0 = 0; x1 = 0; }
        {   /* longest run of fully opaque pixels: copied as a block */
            int best0 = x0, best1 = x0, r0 = -1;
            for (i = x0; i <= x1; i++) {
                int op = i < x1 && im->a8[j * im->w + i] == 255;
                if (op && r0 < 0) r0 = i;
                if (!op && r0 >= 0) { if (i - r0 > best1 - best0) { best0 = r0; best1 = i; } r0 = -1; }
            }
            im->span[j * 4 + 2] = (int16_t)best0; im->span[j * 4 + 3] = (int16_t)best1;
        }
        im->span[j * 4] = (int16_t)x0; im->span[j * 4 + 1] = (int16_t)x1;
    }
}

void d_img(const Img* im, int x, int y, int opacity) {
    int sx0 = 0, sy0 = 0, w = im->w, h = im->h, i, j;
    DBG(stage, 10); DBG(w, im->w); DBG(h, im->h); DBG(opacity, opacity); DBG(x, x); DBG(y, y); DBG(rows, 0);
    DBG(cx0, cx0); DBG(cy0, cy0); DBG(cx1, cx1); DBG(cy1, cy1); DBG(fast, im->c565 != NULL);
    if (opacity <= 0) { DBG(stage, 11); return; }
    if (x < cx0) { sx0 = cx0 - x; } if (y < cy0) { sy0 = cy0 - y; }
    if (x + w > cx1) w = cx1 - x;
    if (y + h > cy1) h = cy1 - y;
    DBG(cw, w); DBG(ch, h); DBG(sx0, sx0); DBG(sy0, sy0);
    if (im->c565) {
        for (j = sy0; j < h; j++) {
            int a0 = im->span[j * 4], a1 = im->span[j * 4 + 1], o0 = im->span[j * 4 + 2], o1 = im->span[j * 4 + 3];
            const uint16_t* c = im->c565 + (size_t)j * im->w;
            const uint8_t* al = im->a8 + (size_t)j * im->w;
            uint16_t* d = g_buf + (size_t)(y + j) * SCR_W + x;
            if (a0 < sx0) a0 = sx0;
            if (a1 > w) a1 = w;
#ifdef DRAW_DEBUG
            if (g_dbg.rows == 0) { g_dbg.a0_first = a0; g_dbg.a1_first = a1; g_dbg.o0_first = o0; g_dbg.o1_first = o1; }
            g_dbg.rows++;
#endif
            if (opacity >= 255) {
                if (o0 < a0 || o1 > a1 || o1 <= o0) { o0 = o1 = a0; }       /* clipped or no opaque run: per-pixel only */
                for (i = a0; i < o0; i++) {
                    int a = al[i];
                    if (a == 255) d[i] = c[i];
                    else if (a) { uint16_t p = c[i]; blend_px(&d[i], (p >> 8) & 0xF8, (p >> 3) & 0xFC, (p << 3) & 0xF8, a); }
                }
                if (o1 > o0) memcpy(d + o0, c + o0, (size_t)(o1 - o0) * 2);
                for (i = o1 > o0 ? o1 : o0; i < a1; i++) {
                    int a = al[i];
                    if (a == 255) d[i] = c[i];
                    else if (a) { uint16_t p = c[i]; blend_px(&d[i], (p >> 8) & 0xF8, (p >> 3) & 0xFC, (p << 3) & 0xF8, a); }
                }
            } else {
                for (i = a0; i < a1; i++) {
                    int a = al[i] * opacity / 255;
                    if (a >= 255) d[i] = c[i];
                    else if (a) { uint16_t p = c[i]; blend_px(&d[i], (p >> 8) & 0xF8, (p >> 3) & 0xFC, (p << 3) & 0xF8, a); }
                }
            }
        }
        return;
    }
    for (j = sy0; j < h; j++) {
        const uint8_t* s = im->rgba + ((size_t)j * im->w + sx0) * 4;
        uint16_t* d = g_buf + (size_t)(y + j) * SCR_W + x + sx0;
        for (i = sx0; i < w; i++, s += 4, d++) {
            int a = s[3];
            if (!a) continue;
            if (opacity < 255) a = a * opacity / 255;
            if (a >= 255) *d = P565(s[0], s[1], s[2]); else if (a > 0) blend_px(d, s[0], s[1], s[2], a);
        }
    }
}

void d_img_s(const Img* im, float cx, float cy, float sx, float sy, int opacity) {
    int dw = (int)(im->w * sx + 0.5f), dh = (int)(im->h * sy + 0.5f), x0, y0, i, j;
    int fx, fy;
    DBG(stage, 1); DBG(w, im->w); DBG(h, im->h); DBG(cxf, cx); DBG(cyf, cy); DBG(sxf, sx); DBG(syf, sy); DBG(opacity, opacity); DBG(dw, dw); DBG(dh, dh); DBG(rows, -1);
    if (dw <= 0 || dh <= 0 || opacity <= 0) { DBG(stage, 2); return; }
    x0 = (int)(cx - dw * 0.5f + 0.5f); y0 = (int)(cy - dh * 0.5f + 0.5f);
    DBG(x0, x0); DBG(y0, y0);
    if (dw == im->w && dh == im->h) { DBG(stage, 3); d_img(im, x0, y0, opacity); return; }
    DBG(stage, 4);
    fx = (int)((float)im->w * 65536.0f / (float)dw); fy = (int)((float)im->h * 65536.0f / (float)dh);
    for (j = 0; j < dh; j++) {
        int dy = y0 + j, v, vy, vf;
        uint16_t* d;
        if (dy < cy0 || dy >= cy1) continue;
        v = (int)(((int64_t)(2 * j + 1) * fy) / 2) - 32768;
        if (v < 0) v = 0;
        vy = v >> 16; vf = (v >> 8) & 255;
        if (vy >= im->h - 1) { vy = im->h - 1; vf = 0; }
        d = g_buf + (size_t)dy * SCR_W;
        for (i = 0; i < dw; i++) {
            int dx = x0 + i, u, ux, uf, a00, a01, a10, a11, w00, w01, w10, w11, a, r, g, b;
            const uint8_t *p00, *p01, *p10, *p11;
            if (dx < cx0 || dx >= cx1) continue;
            u = (int)(((int64_t)(2 * i + 1) * fx) / 2) - 32768;
            if (u < 0) u = 0;
            ux = u >> 16; uf = (u >> 8) & 255;
            if (ux >= im->w - 1) { ux = im->w - 1; uf = 0; }
            p00 = im->rgba + ((size_t)vy * im->w + ux) * 4;
            p01 = uf ? p00 + 4 : p00;
            p10 = vf ? p00 + (size_t)im->w * 4 : p00;
            p11 = vf ? p10 + (uf ? 4 : 0) : p01;
            w00 = (256 - uf) * (256 - vf); w01 = uf * (256 - vf); w10 = (256 - uf) * vf; w11 = uf * vf;   /* sum 65536 */
            if ((p00[3] & p01[3] & p10[3] & p11[3]) == 255) {            /* interior: no alpha weighting, no division */
                r = (p00[0] * w00 + p01[0] * w01 + p10[0] * w10 + p11[0] * w11) >> 16;
                g = (p00[1] * w00 + p01[1] * w01 + p10[1] * w10 + p11[1] * w11) >> 16;
                b = (p00[2] * w00 + p01[2] * w01 + p10[2] * w10 + p11[2] * w11) >> 16;
                if (opacity >= 255) d[dx] = P565(r, g, b); else blend_px(&d[dx], r, g, b, opacity);
                continue;
            }
            a00 = p00[3] * w00; a01 = p01[3] * w01; a10 = p10[3] * w10; a11 = p11[3] * w11;
            a = (a00 + a01 + a10 + a11) >> 16;
            if (a <= 0) continue;
            r = (p00[0] * (a00 >> 8) + p01[0] * (a01 >> 8) + p10[0] * (a10 >> 8) + p11[0] * (a11 >> 8)) / ((a00 + a01 + a10 + a11) >> 8);
            g = (p00[1] * (a00 >> 8) + p01[1] * (a01 >> 8) + p10[1] * (a10 >> 8) + p11[1] * (a11 >> 8)) / ((a00 + a01 + a10 + a11) >> 8);
            b = (p00[2] * (a00 >> 8) + p01[2] * (a01 >> 8) + p10[2] * (a10 >> 8) + p11[2] * (a11 >> 8)) / ((a00 + a01 + a10 + a11) >> 8);
            if (opacity < 255) a = a * opacity / 255;
            if (a >= 255) d[dx] = P565(r, g, b); else if (a > 0) blend_px(&d[dx], r, g, b, a);
        }
    }
}

/* ---- fonts ---- */
struct Font { const uint8_t* base; int height, ascent, first, count, outline; };
static Font g_fonts[16];
static char g_fnames[16][8];
static int g_nfonts;

static int rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static int rds16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

Font* font_get(const char* name) {
    int i;
    const uint8_t* p;
    for (i = 0; i < g_nfonts; i++) if (!strcmp(g_fnames[i], name)) return &g_fonts[i];
    if (g_nfonts >= 16) return NULL;
    p = pak_get(name, NULL);
    if (!p) { fprintf(stderr, "gweled: missing font %s\n", name); return NULL; }
    strncpy(g_fnames[g_nfonts], name, 7);
    g_fonts[g_nfonts].base = p; g_fonts[g_nfonts].height = rd16(p); g_fonts[g_nfonts].ascent = rd16(p + 2);
    g_fonts[g_nfonts].first = rd16(p + 4); g_fonts[g_nfonts].count = rd16(p + 6); g_fonts[g_nfonts].outline = rd16(p + 8);
    return &g_fonts[g_nfonts++];
}

int font_h(const Font* f) { return f->height; }

static const uint8_t* glyph(const Font* f, unsigned char c) {
    int idx = (int)c - f->first;
    if (idx < 0 || idx >= f->count) idx = '?' - f->first;
    return f->base + 12 + (size_t)idx * 20;
}

int font_w(const Font* f, const char* s) {
    int w = 0;
    for (; *s; s++) w += rds16(glyph(f, (unsigned char)*s) + 8);
    return w;
}

static void draw_plane(const Font* f, const uint8_t* g, int pen, int y, uint32_t off, uint32_t rgb, int opacity) {
    int w = rd16(g), h = rd16(g + 2), xo = rds16(g + 4), yo = rds16(g + 6), i, j;
    int r = (rgb >> 16) & 255, gg = (rgb >> 8) & 255, b = rgb & 255;
    const uint8_t* a = f->base + off;
    if (!w || !off) return;
    for (j = 0; j < h; j++) {
        int dy = y + f->ascent + yo + j;
        uint16_t* d;
        if (dy < cy0 || dy >= cy1) continue;
        d = g_buf + (size_t)dy * SCR_W;
        for (i = 0; i < w; i++) {
            int dx = pen + xo + i, al = a[j * w + i];
            if (!al || dx < cx0 || dx >= cx1) continue;
            if (opacity < 255) al = al * opacity / 255;
            if (al >= 255) d[dx] = P565(r, gg, b); else if (al > 0) blend_px(&d[dx], r, gg, b, al);
        }
    }
}

void d_text(const Font* f, int x, int y, uint32_t rgb, uint32_t outline, int opacity, const char* s) {
    int pen = x;
    const char* t;
    if (!f || opacity <= 0) return;
    if (f->outline) {
        pen = x;
        for (t = s; *t; t++) { const uint8_t* g = glyph(f, (unsigned char)*t); draw_plane(f, g, pen, y, rd32(g + 16), outline, opacity); pen += rds16(g + 8); }
    }
    pen = x;
    for (t = s; *t; t++) { const uint8_t* g = glyph(f, (unsigned char)*t); draw_plane(f, g, pen, y, rd32(g + 12), rgb, opacity); pen += rds16(g + 8); }
}

void d_text_c(const Font* f, int cx, int y, uint32_t rgb, uint32_t outline, int opacity, const char* s) { if (f) d_text(f, cx - font_w(f, s) / 2, y, rgb, outline, opacity, s); }
void d_text_r(const Font* f, int rx, int y, uint32_t rgb, uint32_t outline, int opacity, const char* s) { if (f) d_text(f, rx - font_w(f, s), y, rgb, outline, opacity, s); }

/* Text as an RGBA image (outline drawn under the fill), padded by 2 px. */
int text_img(Img* out, const Font* f, uint32_t rgb, uint32_t outline, const char* s) {
    int w = font_w(f, s) + 8, h = f->height + 4, pen = 4, k;
    uint8_t* px = (uint8_t*)calloc((size_t)w * h, 4);
    const char* t;
    if (!px) return -1;
    for (k = 0; k < 2; k++) {
        uint32_t col = k == 0 ? outline : rgb;
        pen = 4;
        if (k == 0 && !f->outline) continue;
        for (t = s; *t; t++) {
            const uint8_t* g = glyph(f, (unsigned char)*t);
            int gw = rd16(g), gh = rd16(g + 2), xo = rds16(g + 4), yo = rds16(g + 6), i, j;
            uint32_t off = rd32(g + (k == 0 ? 16 : 12));
            if (gw && off) {
                const uint8_t* a = f->base + off;
                for (j = 0; j < gh; j++) {
                    int dy = 2 + f->ascent + yo + j;
                    if (dy < 0 || dy >= h) continue;
                    for (i = 0; i < gw; i++) {
                        int dx = pen + xo + i, al = a[j * gw + i];
                        uint8_t* d;
                        if (!al || dx < 0 || dx >= w) continue;
                        d = px + ((size_t)dy * w + dx) * 4;
                        /* source-over in straight alpha */
                        {
                            int da = d[3], oa = al + da * (255 - al) / 255;
                            if (oa > 0) {
                                d[0] = (uint8_t)((((col >> 16) & 255) * al + d[0] * da * (255 - al) / 255) / oa);
                                d[1] = (uint8_t)((((col >> 8) & 255) * al + d[1] * da * (255 - al) / 255) / oa);
                                d[2] = (uint8_t)(((col & 255) * al + d[2] * da * (255 - al) / 255) / oa);
                            }
                            d[3] = (uint8_t)oa;
                        }
                    }
                }
            }
            pen += rds16(g + 8);
        }
    }
    out->w = w; out->h = h; out->rgba = px; out->owned = 1;
    return 0;
}
