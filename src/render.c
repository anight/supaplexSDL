/* Tiny software blitter over 8-bit palette-indexed surfaces. */
#include "sp.h"
#include <string.h>

void sp_clear(Image *dst, uint8_t c)
{
    memset(dst->px, c, (size_t)dst->w * dst->h);
}

/* Blit with clipping; colour index 0 is opaque (Supaplex tiles are not masked). */
void sp_blit(Image *dst, const Image *src, int sx, int sy, int w, int h,
             int dx, int dy)
{
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if (dx + w > dst->w) w = dst->w - dx;
    if (dy + h > dst->h) h = dst->h - dy;
    if (sx < 0 || sy < 0 || sx + w > src->w || sy + h > src->h) {
        if (sx < 0) { w += sx; dx -= sx; sx = 0; }
        if (sy < 0) { h += sy; dy -= sy; sy = 0; }
        if (sx + w > src->w) w = src->w - sx;
        if (sy + h > src->h) h = src->h - sy;
    }
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++)
        memcpy(dst->px + (size_t)(dy + y) * dst->w + dx,
               src->px + (size_t)(sy + y) * src->w + sx, (size_t)w);
}

/* Draw a string.  The original writes each 8x8 cell opaquely: lit pixels get
 * `colour`, unlit ones get `bg` (pass SP_TRANSPARENT to leave them alone).
 * `adv` is the pen advance: 8 for CHARS8, 6 for CHARS6, whose glyphs are
 * stored in 8 pixel cells but only 5 pixels wide, so the cells overlap. */
void sp_text_adv(Image *dst, const Font *f, int x, int y, const char *s,
                 uint8_t colour, int bg, int adv)
{
    for (; *s; s++, x += adv) {
        int g = (unsigned char)*s - 0x20;
        if (g < 0 || g >= 64) g = 0;            /* unknown -> blank */
        for (int gy = 0; gy < 8; gy++) {
            int py = y + gy;
            if (py < 0 || py >= dst->h) continue;
            for (int gx = 0; gx < 8; gx++) {
                int px = x + gx;
                if (px < 0 || px >= dst->w) continue;
                if (f->bits[gy][g * 8 + gx])
                    dst->px[(size_t)py * dst->w + px] = colour;
                else if (bg != SP_TRANSPARENT)
                    dst->px[(size_t)py * dst->w + px] = (uint8_t)bg;
            }
        }
    }
}

void sp_text_bg(Image *dst, const Font *f, int x, int y, const char *s,
                uint8_t colour, int bg)
{
    sp_text_adv(dst, f, x, y, s, colour, bg, 8);
}

void sp_text(Image *dst, const Font *f, int x, int y, const char *s, uint8_t colour)
{
    sp_text_adv(dst, f, x, y, s, colour, 0, 8);
}
