/* Loading and decoding of the original Supaplex .DAT files. */
#include "sp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *dir, const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

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

static bool load_img(GameData *gd, const char *dir, const char *name,
                     int w, int h, Image *out)
{
    (void)gd;
    size_t len; uint8_t *d = read_file(dir, name, &len);
    if (!d) return false;
    bool ok = sp_decode_planar(d, len, w, h, out);
    free(d);
    return ok;
}

/* PALETTES.DAT: 4 palettes x 16 colours x 4 bytes (R,G,B,unused).
 * Channels are 4-bit; the VGA DAC gets value*4, which reads back as
 * (v<<2)|(v>>4) in 8-bit terms -> (n<<4)|(n>>2). */
static bool load_palettes(GameData *gd, const char *dir)
{
    size_t len; uint8_t *d = read_file(dir, "PALETTES.DAT", &len);
    if (!d || len < 256) { free(d); return false; }
    for (int p = 0; p < 4; p++)
        for (int c = 0; c < 16; c++) {
            const uint8_t *e = d + p * 64 + c * 4;
            gd->pal[p].r[c] = (uint8_t)((e[0] << 4) | (e[0] >> 2));
            gd->pal[p].g[c] = (uint8_t)((e[1] << 4) | (e[1] >> 2));
            gd->pal[p].b[c] = (uint8_t)((e[2] << 4) | (e[2] >> 2));
        }
    free(d);
    return true;
}

/* LEVELS.DAT: 111 levels of 1536 bytes: 60x24 tile bytes + a 96 byte trailer. */
static bool load_levels(GameData *gd, const char *dir)
{
    size_t len; uint8_t *d = read_file(dir, "LEVELS.DAT", &len);
    if (!d) return false;
    int n = (int)(len / LEVEL_BYTES);
    if (n > NUM_LEVELS) n = NUM_LEVELS;
    for (int i = 0; i < n; i++) {
        const uint8_t *L = d + (size_t)i * LEVEL_BYTES;
        Level *lv = &gd->levels[i];
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
    free(d);

    /* LEVEL.LST: 111 fixed 28-byte records "NNN <23 char title>\n" */
    d = read_file(dir, "LEVEL.LST", &len);
    if (d) {
        for (int i = 0; i < NUM_LEVELS && (size_t)(i * 28 + 28) <= len; i++) {
            memcpy(gd->level_names[i], d + i * 28, 27);
            gd->level_names[i][27] = 0;
            char *nl = strchr(gd->level_names[i], '\n'); if (nl) *nl = 0;
        }
        free(d);
    } else {
        for (int i = 0; i < NUM_LEVELS; i++)
            snprintf(gd->level_names[i], 28, "%03d %s", i + 1, gd->levels[i].title);
    }
    return true;
}

/* CHARSn.DAT: 512 bytes = a 512x8 1bpp strip of 64 consecutive 8x8 glyphs. */
static bool load_font(const char *dir, const char *name, Font *f)
{
    size_t len; uint8_t *d = read_file(dir, name, &len);
    if (!d || len < 512) { free(d); return false; }
    for (int y = 0; y < 8; y++)
        for (int xb = 0; xb < 64; xb++) {
            uint8_t b = d[y * 64 + xb];
            for (int bit = 0; bit < 8; bit++)
                f->bits[y][xb * 8 + bit] = (uint8_t)((b >> (7 - bit)) & 1);
        }
    free(d);
    return true;
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
    v->px = malloc(256); h->px = malloc(256); c->px = malloc(256);
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
    ok = ok && load_img(gd, dir, "FIXED.DAT",    640,  16, &gd->fixed);
    ok = ok && load_img(gd, dir, "MOVING.DAT",   320, 462, &gd->moving);
    ok = ok && load_img(gd, dir, "PANEL.DAT",    320,  24, &gd->panel);
    ok = ok && load_img(gd, dir, "TITLE.DAT",    320, 200, &gd->title);
    ok = ok && load_img(gd, dir, "TITLE1.DAT",   320, 200, &gd->title1);
    ok = ok && load_img(gd, dir, "TITLE2.DAT",   320, 200, &gd->title2);
    ok = ok && load_img(gd, dir, "MENU.DAT",     320, 200, &gd->menu);
    ok = ok && load_img(gd, dir, "BACK.DAT",     320, 200, &gd->back);
    ok = ok && load_img(gd, dir, "GFX.DAT",      320, 200, &gd->gfx);
    ok = ok && load_img(gd, dir, "CONTROLS.DAT", 320, 200, &gd->controls);
    ok = ok && load_font(dir, "CHARS6.DAT", &gd->chars6);
    ok = ok && load_font(dir, "CHARS8.DAT", &gd->chars8);
    if (ok) build_border(gd);
    return ok;
}

void sp_free_all(GameData *gd)
{
    Image *imgs[] = { &gd->border_v, &gd->border_h, &gd->border_c,
                      &gd->fixed, &gd->moving, &gd->panel, &gd->title,
                      &gd->title1, &gd->title2, &gd->menu, &gd->back,
                      &gd->gfx, &gd->controls };
    for (size_t i = 0; i < sizeof imgs / sizeof *imgs; i++)
        { free(imgs[i]->px); imgs[i]->px = NULL; }
}
