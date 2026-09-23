/* Loading and decoding of the original Supaplex .DAT files. */
#include "sp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every image row is 4 EGA bitplanes of w/8 bytes each; plane p supplies bit p
 * of the 4-bit colour index, MSB = leftmost pixel. */
bool sp_decode_planar(const uint8_t *data, size_t len, int w, int h, Image *out)
{
    int bpr = w / 8, stride = bpr * 4;
    if (len < (size_t)(stride * h)) {
        fprintf(stderr, "planar: need %d bytes, have %zu\n", stride * h, len);
        return false;
    }
    out->w = w; out->h = h;
    out->px = calloc((size_t)w * h, 1);
    out->owned = true;
    if (!out->px) return false;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = data + (size_t)y * stride;
        uint8_t *dst = out->px + (size_t)y * w;
        for (int p = 0; p < 4; p++) {
            const uint8_t *pl = row + p * bpr;
            for (int xb = 0; xb < bpr; xb++) {
                uint8_t b = pl[xb];
                for (int bit = 0; bit < 8; bit++)
                    dst[xb * 8 + bit] |= (uint8_t)(((b >> (7 - bit)) & 1) << p);
            }
        }
    }
    return true;
}

/* An image comes either already decoded - one byte a pixel, blitted where it
 * lies - or planar, as in the original file, and is decoded here. */
static bool load_img(const char *dir, const char *name, int w, int h, Image *out)
{
    Asset a;
    if (!sp_asset_open(dir, name, &a)) { fprintf(stderr, "cannot open %s\n", name); return false; }
    if (a.w == w && a.h == h && a.len >= (size_t)w * h) {
        out->w = w; out->h = h; out->px = (uint8_t *)a.data; out->owned = false;
        return true;                  /* not released: the image is the asset */
    }
    bool ok = sp_decode_planar(a.data, a.len, w, h, out);
    sp_asset_release(&a);
    return ok;
}

/* PALETTES.DAT: 4 palettes x 16 colours x 4 bytes (R,G,B,unused).
 * Channels are 4-bit; the VGA DAC gets value*4, which reads back as
 * (v<<2)|(v>>4) in 8-bit terms -> (n<<4)|(n>>2). */
static bool load_palettes(GameData *gd, const char *dir)
{
    Asset a;
    if (!sp_asset_open(dir, "PALETTES.DAT", &a)) { fprintf(stderr, "cannot open PALETTES.DAT\n"); return false; }
    bool ok = a.len >= 256;
    for (int p = 0; ok && p < 4; p++)
        for (int c = 0; c < 16; c++) {
            const uint8_t *e = a.data + p * 64 + c * 4;
            gd->pal[p].r[c] = (uint8_t)((e[0] << 4) | (e[0] >> 2));
            gd->pal[p].g[c] = (uint8_t)((e[1] << 4) | (e[1] >> 2));
            gd->pal[p].b[c] = (uint8_t)((e[2] << 4) | (e[2] >> 2));
        }
    sp_asset_release(&a);
    return ok;
}

/* One LEVELS.DAT record: 60x24 tile bytes and a 96 byte trailer. */
void sp_parse_level(const uint8_t *L, Level *lv)
{
    memset(lv, 0, sizeof *lv);
    memcpy(lv->tiles, L, LVL_TILES);
    const uint8_t *tr = L + LVL_TILES;
    lv->gravity          = tr[4];
    memcpy(lv->title, tr + 6, 23); lv->title[23] = 0;
    lv->freeze_zonks     = tr[29];
    lv->infotrons_needed = tr[30];
    lv->n_special_ports  = tr[31] > 10 ? 10 : tr[31];
    for (int p = 0; p < lv->n_special_ports; p++) {
        const uint8_t *q = tr + 32 + p * 6;
        lv->ports[p].pos            = (uint16_t)((q[0] << 8) | q[1]);
        lv->ports[p].gravity        = q[2];
        lv->ports[p].freeze_zonks   = q[3];
        lv->ports[p].freeze_enemies = q[4];
    }
}

/* Levels are parsed when one is started rather than all held decoded: 111 of
 * them would be 170 KB, which a microcontroller does not have to spare. */
bool sp_level(const GameData *gd, int i, Level *out)
{
    if (i < 0 || i >= gd->n_levels) return false;
    sp_parse_level(gd->levels_dat.data + (size_t)i * LEVEL_BYTES, out);
    return true;
}

/* LEVELS.DAT: 111 levels of 1536 bytes.  LEVEL.LST: 111 fixed 28-byte
 * records "NNN <23 char title>\n". */
static bool load_levels(GameData *gd, const char *dir)
{
    if (!sp_asset_open(dir, "LEVELS.DAT", &gd->levels_dat)) {
        fprintf(stderr, "cannot open LEVELS.DAT\n"); return false;
    }
    gd->n_levels = (int)(gd->levels_dat.len / LEVEL_BYTES);
    if (gd->n_levels > NUM_LEVELS) gd->n_levels = NUM_LEVELS;

    Asset a;
    if (sp_asset_open(dir, "LEVEL.LST", &a)) {
        for (int i = 0; i < NUM_LEVELS && (size_t)(i * 28 + 28) <= a.len; i++) {
            memcpy(gd->level_names[i], a.data + i * 28, 27);
            gd->level_names[i][27] = 0;
            char *nl = strchr(gd->level_names[i], '\n'); if (nl) *nl = 0;
        }
        sp_asset_release(&a);
    } else {
        for (int i = 0; i < gd->n_levels; i++) {
            Level lv; sp_level(gd, i, &lv);
            snprintf(gd->level_names[i], 28, "%03u %.23s", (unsigned)(i + 1) % 1000u, lv.title);
        }
    }
    return gd->n_levels > 0;
}

/* CHARSn.DAT: 512 bytes = a 512x8 1bpp strip of 64 consecutive 8x8 glyphs. */
static bool load_font(const char *dir, const char *name, Font *f)
{
    Asset a;
    if (!sp_asset_open(dir, name, &a)) { fprintf(stderr, "cannot open %s\n", name); return false; }
    bool ok = a.len >= 512;
    for (int y = 0; ok && y < 8; y++)
        for (int xb = 0; xb < 64; xb++) {
            uint8_t b = a.data[y * 64 + xb];
            for (int bit = 0; bit < 8; bit++)
                f->bits[y][xb * 8 + bit] = (uint8_t)((b >> (7 - bit)) & 1);
        }
    sp_asset_release(&a);
    return ok;
}

/* The original does not draw the level's own outermost ring of tiles; it draws
 * a fixed metal frame whose pieces live in MOVING.DAT:
 *   (304,388) vertical bar   (304,396) horizontal bar   (296,396) corner
 * Only the 8 pixels nearest the interior are ever on screen, because the
 * viewport clamps half a tile inside the level. */
static void build_border(GameData *gd)
{
    const Image *m = &gd->moving;
    Image *v = &gd->border_v, *h = &gd->border_h, *c = &gd->border_c;
    v->w = v->h = h->w = h->h = c->w = c->h = 16;
    v->px = gd->border_px[0]; h->px = gd->border_px[1]; c->px = gd->border_px[2];
    v->owned = h->owned = c->owned = false;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            v->px[y*16+x] = m->px[388 * m->w + 304 + (x & 7)];
            h->px[y*16+x] = m->px[(396 + (y & 7)) * m->w + 304];
            c->px[y*16+x] = m->px[(396 + (y & 7)) * m->w + 296 + (x & 7)];
        }
}

bool sp_load_all(GameData *gd, const char *dir)
{
    memset(gd, 0, sizeof *gd);
    bool ok = load_palettes(gd, dir) && load_levels(gd, dir);
    ok = ok && load_img(dir, "FIXED.DAT",    640,  16, &gd->fixed);
    ok = ok && load_img(dir, "MOVING.DAT",   320, 462, &gd->moving);
    ok = ok && load_img(dir, "PANEL.DAT",    320,  24, &gd->panel);
    ok = ok && load_img(dir, "TITLE.DAT",    320, 200, &gd->title);
    ok = ok && load_img(dir, "TITLE1.DAT",   320, 200, &gd->title1);
    ok = ok && load_img(dir, "TITLE2.DAT",   320, 200, &gd->title2);
    ok = ok && load_img(dir, "MENU.DAT",     320, 200, &gd->menu);
    ok = ok && load_img(dir, "BACK.DAT",     320, 200, &gd->back);
    ok = ok && load_img(dir, "GFX.DAT",      320, 200, &gd->gfx);
    ok = ok && load_img(dir, "CONTROLS.DAT", 320, 200, &gd->controls);
    ok = ok && load_font(dir, "CHARS6.DAT", &gd->chars6);
    ok = ok && load_font(dir, "CHARS8.DAT", &gd->chars8);
    if (ok) build_border(gd);
    return ok;
}

void sp_free_all(GameData *gd)
{
    Image *imgs[] = { &gd->fixed, &gd->moving, &gd->panel, &gd->title,
                      &gd->title1, &gd->title2, &gd->menu, &gd->back,
                      &gd->gfx, &gd->controls };
    for (size_t i = 0; i < sizeof imgs / sizeof *imgs; i++) {
        if (imgs[i]->owned) free(imgs[i]->px);
        imgs[i]->px = NULL; imgs[i]->owned = false;
    }
    sp_asset_release(&gd->levels_dat);
}
