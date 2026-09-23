/* Supaplex simulation, ported from the original's 16-bit code.
 *
 * Structure follows SPFIX62.EXE exactly where it matters:
 *   - one 16-bit word per cell (low = tile, high = state), as at DS:0x1834
 *   - each frame runs Murphy first, then scans cells 61..1378 collecting
 *     (cell, handler) pairs from the tile jump table at DS:0x160a, then
 *     dispatches them (original FUN_46c2_3013)
 *   - object states use the original's encoding: 0x10+n falling,
 *     0x20+n / 0x30+n rolling, 0x40 pre-fall, 0x50/0x60 roll start,
 *     0x70 keep falling
 */
#include "game.h"
#include "anim.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

#define FPS_GAME 35

static inline int cx(int i) { return i % LVL_W; }
static inline int cy(int i) { return i / LVL_W; }

/* DS:0x6cb9 seed, seed = seed*0x5E5 + 0x31, returns seed>>1 (FUN_46c2_33e1) */
uint16_t game_rand(Game *g)
{
    g->rng = (uint16_t)(g->rng * 0x5E5u + 0x31u);
    return (uint16_t)(g->rng >> 1);
}

void game_start(Game *g, const Level *lv, int level_no)
{
    memset(g, 0, sizeof *g);
    for (int i = 0; i < LVL_TILES; i++) g->f[i] = g->look[i] = lv->tiles[i];
    g->level_no = level_no;
    memcpy(g->title, lv->title, sizeof g->title);
    memcpy(g->player, "        ", 9);
    g->gravity      = lv->gravity != 0;
    g->freeze_zonks = lv->freeze_zonks;   /* only the value 2 actually freezes */
    g->rng          = 0;
    g->term_mask    = 0x7f;                 /* DS:0x165a at level init */
    g->term_armed   = false;
    g->n_ports      = lv->n_special_ports;
    for (int p = 0; p < g->n_ports; p++) {
        g->ports[p].pos            = lv->ports[p].pos;
        g->ports[p].gravity        = lv->ports[p].gravity;
        g->ports[p].freeze_zonks   = lv->ports[p].freeze_zonks;
        g->ports[p].freeze_enemies = lv->ports[p].freeze_enemies;
    }
    g->m_dir        = DIR_NONE;

    /* Port of FUN_46c2_3519: the level bytes are rewritten into physics tiles
     * before play starts.  The original renders the level from the raw bytes,
     * so the decorative multi-tile chips and hardware still look different on
     * screen, but as far as the simulation is concerned they are RAM chips and
     * plain hardware.  Missing this makes zonks refuse to roll off chips. */
    for (int i = 0; i < LVL_TILES; i++) {
        uint16_t w = g->f[i];
        if (LO(w) == 0xF1) { g->f[i] = MK(T_EXPLOSION, HI(w)); continue; }
        if (w == T_INFOTRON) continue;
        if (w == T_SNIKSNAK || w == T_ELECTRON) {      /* initial orientation */
            uint8_t t = LO(w);
            if (g->f[i - 1] == 0)        g->f[i] = MK(t, 1);
            else if (g->f[i - ROW] == 0) { g->f[i - ROW] = MK(t, 0x10); g->f[i] = W_VACATE; }
            else if (g->f[i + 1] == 0)   { g->f[i + 1] = MK(t, 0x28); g->f[i] = W_VACATE; }
            continue;
        }
        if (w == T_CHIP_L || w == T_CHIP_R || w == T_CHIP_T || w == T_CHIP_B)
            g->f[i] = T_RAM_CHIP;
        else if (w >= T_HW0 && w <= T_HW9)
            g->f[i] = T_HARDWARE;
        else if (w > T_SPORT_R - 1 && w < T_SNIKSNAK)   /* 13..16 special ports */
            g->f[i] = MK(LO(w) - 4, 1);
    }
    for (int i = 0; i < LVL_TILES; i++)
        if (g->f[i] == W_VACATE) g->f[i] = 0;

    int total = 0;
    for (int i = 0; i < LVL_TILES; i++) {
        if (LO(g->f[i]) == T_MURPHY)   g->murphy = i;
        if (LO(g->f[i]) == T_INFOTRON) total++;
    }
    g->m_from = g->murphy;
    g->infotrons_needed = lv->infotrons_needed ? lv->infotrons_needed : total;
}

/* ------------------------------------------------------------ Murphy pixels */
/* Murphy walks one tile over eight frames.  While pushing an object or moving
 * through a port he is still standing in his old cell as far as the field is
 * concerned, so interpolate towards m_dst rather than towards g->murphy. */
void game_murphy_px(const Game *g, int *px, int *py)
{
    int to = (g->m_step > 0 && g->m_dst >= 0) ? g->m_dst : g->murphy;
    int tx = cx(to) * TILE, ty = cy(to) * TILE;
    if (g->m_delay > 0) { *px = cx(g->m_from) * TILE; *py = cy(g->m_from) * TILE; return; }
    if (g->m_step > 0) {
        int ox = cx(g->m_from) * TILE, oy = cy(g->m_from) * TILE;
        tx = ox + (tx - ox) * g->m_step / MOVE_FRAMES;
        ty = oy + (ty - oy) * g->m_step / MOVE_FRAMES;
    }
    *px = tx; *py = ty;
}

/* ---------------------------------------------------------------- viewport */
void game_viewport(const Game *g, int *sx, int *sy)
{
    int px, py;
    game_murphy_px(g, &px, &py);
    int x = px + TILE / 2 - VIEW_W / 2;
    int y = py + TILE / 2 - VIEW_H / 2;
    const int lo = TILE / 2;
    const int hx = LVL_W * TILE - VIEW_W - TILE / 2;
    const int hy = LVL_H * TILE - VIEW_H - TILE / 2;
    *sx = x < lo ? lo : (x > hx ? hx : x);
    *sy = y < lo ? lo : (y > hy ? hy : y);
}

/* ------------------------------------------------------------- explosions */
/* An explosion replaces a 3x3 block.  Cells become tile 31; state 0..7 decays
 * to empty space, state 0x80..0x88 decays to an infotron (electrons). */
static bool explodable(uint16_t w)
{
    uint8_t t = LO(w);
    return t != T_HARDWARE && !(t >= T_HW0 && t <= T_HW9) &&
           t != T_CHIP_L && t != T_CHIP_R && t != T_CHIP_T && t != T_CHIP_B &&
           t != T_RAM_CHIP && t != T_EXIT &&
           !(t >= T_PORT_R && t <= T_SPORT_U) &&
           t != T_PORT_V && t != T_PORT_H && t != T_PORT_X;
}

/* Port of FUN_46c2_37c1.  Each of the nine cells becomes an explosion; objects
 * that can chain (disks, snik snaks, electrons) and Murphy additionally get a
 * countdown in the DS:0x2434 timer array, which FUN_46c2_3782 ticks down and
 * turns into a further explosion - that is how chain reactions work. */
static void explode(Game *g, int i, bool infotron_kind)
{
    uint8_t centre = LO(g->f[i]);
    if (centre == T_HARDWARE) return;
    sp_sound_play(SFX_EXPLODE);
    if (centre == T_MURPHY) { g->dead = true; g->death = "caught in explosion"; }
    bool elec = infotron_kind || centre == T_ELECTRON;
    uint16_t base_word = elec ? MK(T_EXPLOSION, 0x80) : MK(T_EXPLOSION, 0);
    int8_t   base_tmr  = elec ? -13 : 13;

    int x = cx(i), y = cy(i);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx < 1 || nx >= LVL_W - 1 || ny < 1 || ny >= LVL_H - 1) continue;
            int c = ny * LVL_W + nx;
            uint8_t t = LO(g->f[c]);
            if (t == T_HARDWARE) continue;
            if (!explodable(g->f[c])) continue;
            uint16_t val = base_word; int8_t tmr = base_tmr;
            if (t == T_ORANGE_DISK || t == T_YELLOW_DISK || t == T_SNIKSNAK) {
                g->timer[c] = tmr;
            } else if (t == T_ELECTRON) {
                val = MK(T_EXPLOSION, 0x80); tmr = (int8_t)-tmr;
                g->timer[c] = tmr;
            } else if (t == T_MURPHY) {
                g->dead = true; g->death = "caught in explosion";
                g->timer[c] = tmr;
            }
            g->f[c] = val;
            g->explosions++;
        }
}

/* FUN_46c2_3782: tick the delayed-explosion timers once per frame. */
static void tick_explosion_timers(Game *g)
{
    for (int i = 0; i < LVL_TILES; i++) {
        int8_t v = g->timer[i];
        if (v == 0) continue;
        if (v > 0) {
            if (--g->timer[i] == 0) explode(g, i, false);
        } else {
            if (++g->timer[i] == 0) {
                g->f[i] = MK(T_ELECTRON, 0xff);
                explode(g, i, true);
            }
        }
    }
}

static void h_explosion(Game *g, int i)
{
    if (LO(g->f[i]) != T_EXPLOSION) return;
    if (g->frame & 3) return;
    uint8_t st = HI(g->f[i]);
    if (!(st & 0x80)) {
        g->f[i] = MK(T_EXPLOSION, st + 1);
        if ((uint8_t)(st + 1) == 8) { g->f[i] = 0; g->explosions--; }
    } else {
        if ((uint8_t)(st + 1) == 0x89) { g->f[i] = T_INFOTRON; g->explosions--; return; }
        g->f[i] = MK(T_EXPLOSION, st + 1);
    }
}

/* -------------------------------------------------------------- vacating */
/* Exact port of FUN_46c2_1bce.  Clearing a cell an object has just left also
 * wakes whatever can now move into it: a zonk or infotron directly above
 * starts to fall, and one sitting diagonally above on something round rolls
 * across.  Note the roll reserves the cell ABOVE the vacated one (that is the
 * cell the object rolls into), not the vacated cell itself. */
static bool round_support(uint16_t w)
{
    return w == T_ZONK || w == T_INFOTRON || w == T_RAM_CHIP;
}

/* FUN_46c2_1cac (zonk) and FUN_46c2_1d27 (infotron): the variant an object
 * calls part-way through its own animation to release the cell it came out of.
 * Unlike Murphy's, these never wake an object directly above - they require
 * that cell to be free - and they only look for their own kind diagonally. */
static void vacate_obj(Game *g, int i, uint8_t tile)
{
    uint16_t *f = g->f;
    const uint8_t other = (tile == T_ZONK) ? T_INFOTRON : T_ZONK;
    if (LO(f[i]) != T_EXPLOSION) f[i] = 0;

    const int up = i - ROW, ul = up - 1, ur = up + 1;
    if (f[up] == W_FALLRES) {
        if (LO(f[up - ROW]) != other) return;      /* someone else is coming in */
    } else if (f[up] != 0) {
        return;
    }
    if (f[ul] == tile && round_support(f[i - 1])) {
        f[ul] = MK(tile, 0x60); f[up] = W_ROLLRES; return;
    }
    if (f[ur] == tile && round_support(f[i + 1])) {
        f[ur] = MK(tile, 0x50); f[up] = W_ROLLRES;
    }
}

static void vacate(Game *g, int i)
{
    uint16_t *f = g->f;
    if (LO(f[i]) != T_EXPLOSION) f[i] = 0;

    const int up = i - ROW, ul = up - 1, ur = up + 1;
    uint16_t a = f[up];
    if (a != 0 && a != W_FALLRES) {
        if (a == T_ZONK || a == T_INFOTRON) f[up] = MK(LO(a), 0x40);
        return;
    }
    if (f[ul] == T_ZONK || f[ul] == T_INFOTRON) {
        if (round_support(f[i - 1])) {
            f[ul] = MK(LO(f[ul]), 0x60);          /* roll right into `up` */
            f[up] = W_ROLLRES;
            return;
        }
        /* otherwise fall through and try the other diagonal */
    }
    if (f[ur] == T_ZONK || f[ur] == T_INFOTRON) {
        if (round_support(f[i + 1])) {
            f[ur] = MK(LO(f[ur]), 0x50);          /* roll left into `up` */
            f[up] = W_ROLLRES;
        }
    }
}

/* ------------------------------------------------------- zonks & infotrons */
/* Faithful port of tile_ZONK (46c2:1360) and tile_INFOTRON (46c2:17d8).  The
 * two routines are the same shape but differ in several details, all of which
 * matter for timing, so they are parameterised rather than merged. */

static bool diag_free(uint16_t w)           /* diagonal cell usable for a roll */
{
    return w == 0 || w == W_ROLLRES || w == W_ROLLFREE;
}

/* Murphy survives being landed on while he is pushing something. */
static bool murphy_safe_from_crush(uint8_t st)
{
    return st == 0x0e || st == 0x0f || st == 0x28 ||
           st == 0x29 || st == 0x25 || st == 0x26;
}

/* What a falling object hits.  Returns true if it just came to rest. */
static bool land_on(Game *g, int i, uint8_t tile)
{
    uint16_t below = g->f[i + ROW];
    uint8_t bt = LO(below);
    if (bt == T_MURPHY) {
        if (!murphy_safe_from_crush(HI(below))) {
            g->dead = true; g->death = "crushed by falling object";
        }
        return false;
    }
    if (bt == T_SNIKSNAK || bt == T_ELECTRON) {
        explode(g, i + ROW, bt == T_ELECTRON);
        return false;
    }
    if (below == T_ORANGE_DISK) {            /* nudged, starts to fall */
        g->f[i + ROW] = MK(T_ORANGE_DISK, 6);
        return false;
    }
    if (tile == T_INFOTRON && (below == T_RED_DISK || below == T_YELLOW_DISK)) {
        explode(g, i + ROW, false);
        return false;
    }
    return true;
}

/* Try to start rolling.  `strict_side` is the infotron's simpler test. */
static bool try_roll(Game *g, int i, uint8_t tile, bool strict_side)
{
    uint16_t *f = g->f;
    if (diag_free(f[i + ROW - 1]) && f[i - 1] == 0) {
        f[i] = MK(tile, 0x50); f[i - 1] = W_ROLLRES; return true;
    }
    if (!diag_free(f[i + ROW + 1])) return false;
    if (strict_side) {
        if (f[i + 1] != 0) return false;
    } else {
        /* a zonk may also roll into a cell reserved by a zonk falling beside it */
        if (f[i + 1] != 0 &&
            !(f[i + 1] == W_FALLRES && LO(f[i - ROW + 1]) == T_ZONK)) return false;
    }
    f[i] = MK(tile, 0x60); f[i + 1] = W_ROLLRES;
    return true;
}

static void h_falling(Game *g, int i, uint8_t tile, bool freezable)
{
    uint16_t *f = g->f;
    const bool info = (tile == T_INFOTRON);
    if (LO(f[i]) != tile) return;

    if (f[i] == tile) {                       /* at rest */
        if (freezable && g->freeze_zonks == 2) return;
        uint16_t below = f[i + ROW];
        if (below == 0) {
            f[i] = MK(tile, 0x40);
        } else {
            if (!round_support(below)) return;
            if (!try_roll(g, i, tile, info)) return;
        }
        /* The original does not return here: it drops straight into the state
         * machine below, so the first animation frame happens immediately.
         * Returning instead makes every fall and roll start one frame late. */
    }

    for (;;) {
        uint8_t st = HI(f[i]), top = st & 0xf0;

        if (top == 0x20 || top == 0x30) {     /* rolling, after relocating */
            uint8_t nx = st + 1;
            int src = i + (top == 0x20 ? 1 : -1);      /* cell it rolled out of */
            if (nx == (top | 4)) f[src] = W_ROLLFREE;
            if (nx == (top | 6)) { f[i] = MK(tile, nx); vacate_obj(g, src, tile); return; }
            if (nx < (top | 8)) { f[i] = MK(tile, nx); return; }
            if (info) f[i] = MK(T_INFOTRON, 0x70);
            else { f[i] = W_VACATE; f[i + ROW] = MK(T_ZONK, 0x10); }
            return;
        }
        if (top == 0x40) {                    /* two frames of wobble */
            if (freezable && g->freeze_zonks == 2) return;
            if ((uint8_t)(st + 1) < 0x42) { f[i] = MK(tile, st + 1); return; }
            if (f[i + ROW] != 0) { f[i] = MK(tile, st); return; }
            f[i] = W_VACATE;
            f[i + ROW] = MK(tile, 0x10);
            return;
        }
        if (top == 0x50 || top == 0x60) {     /* rolling, first two frames */
            int d = (top == 0x50) ? -1 : +1;
            uint8_t nx = st + 1;
            if (nx < (uint8_t)(top | 2)) { f[i] = MK(tile, nx); return; }
            uint16_t diag = f[i + ROW + d], side = f[i + d];
            if (diag == 0 && (side == 0 || side == W_ROLLRES)) {
                f[i] = W_VACATE;
                f[i + d] = MK(tile, (top == 0x50) ? 0x22 : 0x32);
                /* the cell it will drop into: a zonk frees it, an infotron
                 * reserves it for its own fall */
                f[i + ROW + d] = info ? W_FALLRES : W_VACATE;
            } else {
                f[i] = MK(tile, st);
            }
            return;
        }
        if (top == 0x70) {                    /* carry on falling */
            uint16_t below = f[i + ROW];
            if (below != 0 && below != W_FALLRES) return;
            f[i] = W_VACATE;
            f[i + ROW] = MK(tile, 0x10);
            i += ROW;
            continue;
        }
        if (top != 0x10) return;

        /* ---- falling, eight two-pixel steps ---- */
        uint8_t nx = st + 1;
        if (nx == 0x16) {                  /* release the cell it fell out of */
            f[i] = MK(tile, nx);
            vacate_obj(g, i - ROW, tile);
            return;
        }
        if (nx < 0x18) { f[i] = MK(tile, nx); return; }
        f[i] = MK(tile, 0);
        if (freezable && g->freeze_zonks == 2) return;
        uint16_t below = f[i + ROW];
        if (below == 0 || below == W_FALLRES) {
            f[i] = MK(tile, 0x70);
            f[i + ROW] = W_FALLRES;
            return;
        }
        if (!land_on(g, i, tile)) return;
        sp_sound_play(SFX_LAND);
        below = f[i + ROW];
        if (!round_support(below)) return;
        try_roll(g, i, tile, info);
        return;
    }
}

static void h_zonk(Game *g, int i)     { h_falling(g, i, T_ZONK, true); }
static void h_infotron(Game *g, int i) { h_falling(g, i, T_INFOTRON, false); }

/* ------------------------------------------------------------------- bug */
/* tile_BUG: cycles every 4th frame; states 0..13 are the visible "blink",
 * after which it sleeps for a random interval (negative state).  It only
 * kills Murphy while awake. */
static void h_bug(Game *g, int i)
{
    if (LO(g->f[i]) != T_BUG) return;
    if (g->frame & 3) return;
    int8_t st = (int8_t)(HI(g->f[i]) + 1);
    if (st > 13) st = (int8_t)(-(int)((game_rand(g) & 0x3f) + 0x20));
    g->f[i] = MK(T_BUG, (uint8_t)st);
    /* While awake the original only plays a warning sound if Murphy is in any
     * of the eight neighbouring cells (FUN_46c2_6e5b); a bug is lethal only
     * when Murphy walks into it. */
    static const int nb[8] = { -ROW-1, -ROW, -ROW+1, -1, +1, ROW-1, ROW, ROW+1 };
    for (int k = 0; k < 8; k++)
        if (LO(g->f[i + nb[k]]) == T_MURPHY) { sp_sound_play(SFX_BUG); break; }
}

/* --------------------------------------------------------- orange disks */
/* Port of tile_ORANGE_DISK (46c2:362b).  A disk with nothing under it shakes
 * for two frames (states 0x20/0x21) and then falls in eight-frame steps
 * (0x30+); landing on anything solid makes it explode. */
static void h_orange(Game *g, int i)
{
    uint16_t *f = g->f;
    if (LO(f[i]) != T_ORANGE_DISK) return;
    uint16_t w = f[i];

    if (w >= 0x3008) {                           /* falling */
        uint8_t nx = HI(w) + 1;
        if ((nx & 7) != 0) { f[i] = MK(T_ORANGE_DISK, nx); return; }
        f[i] = 0;
        f[i + ROW] = T_ORANGE_DISK;
        if (f[i + 2 * ROW] == 0) {               /* keep going */
            f[i + ROW] = MK(T_ORANGE_DISK, 0x30);
            f[i + 2 * ROW] = MK(T_SPACE, 8);
            return;
        }
        if (LO(f[i + 2 * ROW]) == T_EXPLOSION) return;
        explode(g, i + ROW, false);
        return;
    }
    if (w > 0x2007) {                            /* shaking */
        if (f[i + ROW] == 0) { f[i] = T_ORANGE_DISK; return; }
        uint8_t nx = HI(w) + 1;
        if (nx == 0x22) nx = 0x30;
        f[i] = MK(T_ORANGE_DISK, nx);
        return;
    }
    if (f[i + ROW] != 0) return;                 /* supported: nothing to do */
    f[i] = MK(T_ORANGE_DISK, 0x20);
    f[i + ROW] = MK(T_SPACE, 8);
}

/* ------------------------------------------------- snik snaks & electrons */
/* Direct port of the per-state jump tables at DS:0x154a (snik snak, 46c2:8610)
 * and DS:0x15aa (electron, 46c2:8acf).  Both run the same six routines:
 *
 *   0x00..0x07  turning on the spot, anticlockwise: 0 up, 2 left, 4 down, 6 right
 *   0x08..0x0f  turning on the spot, clockwise:     8 up, 10 right, 12 down, 14 left
 *   0x10..0x17  moving up      0x18..0x1f  moving left
 *   0x20..0x27  moving down    0x28..0x2f  moving right
 *
 * While moving, the vacated cell holds T_ENEMY_TAIL so nothing else may use it.
 * They are left-hand wall followers: after a step they try left, then straight
 * on, then right.
 */
static const int ENEMY_D[4] = { -ROW, -1, +ROW, +1 };   /* up, left, down, right */

/* direction a turning state faces, or -1 if it is between cardinals */
static int enemy_facing(uint8_t st)
{
    switch (st) {
    case 0x00: case 0x08: return 0;                 /* up    */
    case 0x02:            return 1;                 /* left  */
    case 0x04: case 0x0c: return 2;                 /* down  */
    case 0x06:            return 3;                 /* right */
    case 0x0a:            return 3;                 /* right (clockwise group) */
    case 0x0e:            return 1;                 /* left  (clockwise group) */
    default:              return -1;
    }
}

/* Murphy is safe from a touch while he is inside a port (states 0x18..0x1b). */
static void enemy_touch(Game *g, uint16_t murphy_word)
{
    uint8_t ms = HI(murphy_word);
    if (ms < 0x18 || ms > 0x1b) { g->dead = true; g->death = "caught by enemy"; }
}

static bool enemy_try_step(Game *g, int i, uint8_t tile, int d)
{
    int t = i + ENEMY_D[d];
    if (g->f[t] == 0) {
        g->f[i] = MK(T_ENEMY_TAIL, (uint8_t)(d + 1));
        g->f[t] = MK(tile, (uint8_t)(0x10 + d * 8));
        return true;
    }
    if (LO(g->f[t]) == T_MURPHY) enemy_touch(g, g->f[t]);
    return false;
}

static void h_enemy(Game *g, int i, uint8_t tile)
{
    if (g->freeze_enemies) return;
    if (LO(g->f[i]) != tile) return;
    uint8_t st = HI(g->f[i]);

    if (st < 0x10) {                          /* turning on the spot */
        if ((g->frame & 3) == 0) {            /* advance the rotation */
            g->f[i] = MK(tile, (uint8_t)(((st + 1) & 7) | (st & 8)));
            return;
        }
        if ((g->frame & 3) != 3) return;
        int d = enemy_facing(st);
        if (d >= 0) enemy_try_step(g, i, tile, d);
        return;
    }

    int d = (st - 0x10) >> 3;                 /* 0 up, 1 left, 2 down, 3 right */
    uint8_t n = (uint8_t)((st & 7) + 1);
    if (n == 7 && LO(g->f[i - ENEMY_D[d]]) != T_EXPLOSION)
        g->f[i - ENEMY_D[d]] = 0;             /* release the tail cell */
    if (n < 8) { g->f[i] = MK(tile, (uint8_t)(0x10 + d * 8 + n)); return; }

    /* arrived: prefer turning left, then straight on, then right */
    static const int LEFT_OF[4]  = { 1, 2, 3, 0 };
    static const int RIGHT_OF[4] = { 3, 0, 1, 2 };
    g->f[i] = MK(tile, 0);
    int lt = i + ENEMY_D[LEFT_OF[d]];
    if (g->f[lt] == 0 || LO(g->f[lt]) == T_MURPHY) { g->f[i] = MK(tile, 1); return; }
    if (g->f[i + ENEMY_D[d]] == 0) { enemy_try_step(g, i, tile, d); return; }
    if (LO(g->f[i + ENEMY_D[d]]) == T_MURPHY) { enemy_touch(g, g->f[i + ENEMY_D[d]]); return; }
    int rt = i + ENEMY_D[RIGHT_OF[d]];
    if (g->f[rt] == 0 || LO(g->f[rt]) == T_MURPHY) { g->f[i] = MK(tile, 9); return; }
    g->f[i] = MK(tile, 1);
}

static void h_sniksnak(Game *g, int i) { h_enemy(g, i, T_SNIKSNAK); }
static void h_electron(Game *g, int i) { h_enemy(g, i, T_ELECTRON); }

/* ------------------------------------------------------------- terminal */
/* Port of tile_TERMINAL: counts up, and once it reaches zero picks a fresh
 * negative sleep masked by DS:0x165a - which a terminal press shortens. */
static void h_terminal(Game *g, int i)
{
    if (LO(g->f[i]) != T_TERMINAL) return;
    int8_t st = (int8_t)(HI(g->f[i]) + 1);
    if (st < 1) { g->f[i] = MK(T_TERMINAL, (uint8_t)st); return; }
    uint8_t r = (uint8_t)game_rand(g);
    g->f[i] = MK(T_TERMINAL, (uint8_t)(-(int)(r & g->term_mask)));
}

/* --------------------------------------------------------------- Murphy */
static const int DIRD[5] = { 0, -ROW, -1, +ROW, +1 };

static bool is_port_for(uint8_t t, Dir d)
{
    switch (d) {
    case DIR_UP:    return t == T_PORT_U || t == T_PORT_V || t == T_PORT_X;
    case DIR_DOWN:  return t == T_PORT_D || t == T_PORT_V || t == T_PORT_X;
    case DIR_LEFT:  return t == T_PORT_L || t == T_PORT_H || t == T_PORT_X;
    case DIR_RIGHT: return t == T_PORT_R || t == T_PORT_H || t == T_PORT_X;
    default:        return false;
    }
}

/* The original keeps Murphy's action code in the state byte of the cell he
 * occupies, and other objects read it: a zonk landing on Murphy is harmless
 * while he is leaning into a push, and enemies cannot touch him mid-port.
 * Codes follow 46c2:6fd0 (1 up, 2 left, 3 down, 4 right within each group). */
static uint8_t murphy_state_code(Act act, Dir d)
{
    int k = (int)d - 1;                       /* 0 up, 1 left, 2 down, 3 right */
    switch (act) {
    case ACT_MOVE:      return (uint8_t)d;            /* 1..4 (also eating base) */
    case ACT_INFOTRON:  return (uint8_t)(8 + d);      /* 9..12                   */
    case ACT_EXIT:      return 0x0d;
    case ACT_EAT:       return (uint8_t)(0x10 + k);
    case ACT_EAT_INFO:  return (uint8_t)(0x14 + k);
    case ACT_PORT:      return (uint8_t)(0x18 + k);
    case ACT_REDDISK:   return (uint8_t)(0x1c + k);
    case ACT_EAT_RED:   return (uint8_t)(0x20 + k);
    case ACT_DROP_RED:  return 0x2a;
    case ACT_PUSH:      return 0;                     /* set by the caller */
    }
    return 0;
}

/* `now` is the cell Murphy moves into immediately (-1 = he stays put for the
 * whole animation, which is what the original does when pushing an object or
 * stepping through a port); `dst` is where he ends up when it completes. */
/* Every Murphy animation is eight frames except eating an infotron on the
 * spot, whose frame list (DS:0x1236, used by descriptors 0x0f6e..0x0f9e) has
 * only seven entries. */
static int murphy_frames(Act act)
{
    return (act == ACT_EAT_INFO) ? 7 : MOVE_FRAMES;
}

static void murphy_arm(Game *g, Dir d, Act act, int now, int dst,
                       int aux, uint8_t aux_tile)
{
    g->m_from = g->murphy;
    /* The original sets DAT_5024_0dde = 8 when Murphy pushes something and the
     * animation does not advance until it has counted down, so a push costs
     * eight frames of leaning before the eight frames of movement. */
    g->m_delay = (act == ACT_PUSH) ? MOVE_FRAMES : 0;
    g->m_act = act; g->m_dst = dst; g->m_aux = aux; g->m_aux_tile = aux_tile;
    g->m_dir = d; g->m_step = 1;
    uint8_t code = g->m_code ? g->m_code : murphy_state_code(act, d);
    g->m_code = code;
    if (code >= 5 && code <= 8)            sp_sound_play(SFX_EAT);       /* base    */
    else if (code >= 9 && code <= 12)      sp_sound_play(SFX_INFOTRON);
    else if (code >= 0x10 && code <= 0x13) sp_sound_play(SFX_EAT);
    else if (code >= 0x14 && code <= 0x17) sp_sound_play(SFX_INFOTRON);
    else if (act == ACT_EXIT)              sp_sound_play(SFX_EXIT);
    if (now >= 0) {
        /* The original occupies the destination at once and leaves the source
         * marked (tile 0, state 3) so nothing may enter either cell meanwhile. */
        g->f[g->murphy] = MK(T_SPACE, 3);
        g->f[now] = MK(T_MURPHY, code);
        g->murphy = now;
    } else {
        g->f[g->murphy] = MK(T_MURPHY, code);
    }
}

/* The original tests each candidate target in a fixed order, per direction.
 * Note which comparisons are on the whole word (state must be zero, so a
 * falling or rolling object blocks Murphy) and which are on the tile byte
 * alone; the asymmetries below are exactly those in 46c2:71ea (up),
 * 46c2:724c (left), 46c2:72c6 (down) and 46c2:7328 (right). */
static void murphy_begin(Game *g, Dir d, bool space_held)
{
    int i = g->murphy, step = DIRD[d], t = i + step, far = t + step;
    g->blocked_by = 0; g->blocked_dir = DIR_NONE;
    if (t < 0 || t >= LVL_TILES) return;
    uint16_t w = g->f[t];
    uint8_t tt = LO(w);
    const bool horiz = (d == DIR_LEFT || d == DIR_RIGHT);

    if (tt == T_BUG) {                      /* awake bugs are lethal */
        if ((int8_t)HI(w) >= 0) { g->dead = true; g->death = "walked into awake bug"; return; }
        g->f[t] = T_BASE; w = T_BASE; tt = T_BASE;
    }

    if (space_held) {                       /* eat in place, Murphy stays put */
        if (w == T_BASE)     { murphy_arm(g, d, ACT_EAT,      -1, -1, t, 0); return; }
        if (w == T_INFOTRON) { murphy_arm(g, d, ACT_EAT_INFO, -1, -1, t, 0); return; }
        if (w == T_RED_DISK) { murphy_arm(g, d, ACT_EAT_RED,  -1, -1, t, 0); return; }
        return;
    }

    if (d == DIR_LEFT)  g->facing_left = true;
    if (d == DIR_RIGHT) g->facing_left = false;
    if (w == T_SPACE || w == T_BASE) {
        /* codes 1..4 walking into space, 5..8 when a base is eaten on the way */
        g->m_code = (uint8_t)(w == T_BASE ? d + 4 : d);
        murphy_arm(g, d, ACT_MOVE, t, t, 0, 0); return;
    }
    if (w == T_INFOTRON)             { murphy_arm(g, d, ACT_INFOTRON, t, t, 0, 0); return; }
    if (w == T_EXIT) {
        if (g->infotrons < g->infotrons_needed) return;
        murphy_arm(g, d, ACT_EXIT, -1, -1, 0, 0); return;
    }
    if (horiz && w == T_ZONK) {              /* only a zonk at rest can be pushed */
        if (far < 0 || far >= LVL_TILES || g->f[far] != 0) return;
        g->f[far] = MK(T_SPACE, 1);
        g->m_code = (d == DIR_LEFT) ? 0x0e : 0x0f;
        murphy_arm(g, d, ACT_PUSH, -1, t, far, T_ZONK); return;
    }
    if (tt == T_TERMINAL) {                 /* pressing a terminal arms it */
        g->terminal_hit = t;
        g->term_mask = 0x07;                /* DS:0x165a: flicker much faster */
        g->term_armed = true;
        return;
    }
    if (is_port_for(tt, d)) {
        if (far < 0 || far >= LVL_TILES || g->f[far] != 0) return;
        g->f[far] = MK(T_SPACE, 3);
        murphy_arm(g, d, ACT_PORT, -1, far, far, 0); return;
    }
    /* red and yellow disks: whole word when moving left, tile byte otherwise */
    bool red    = (d == DIR_LEFT) ? (w == T_RED_DISK)    : (tt == T_RED_DISK);
    bool yellow = (d == DIR_LEFT) ? (w == T_YELLOW_DISK) : (tt == T_YELLOW_DISK);
    if (red) { murphy_arm(g, d, ACT_REDDISK, t, t, 0, 0); return; }
    if (yellow) {
        if (far < 0 || far >= LVL_TILES || g->f[far] != 0) return;
        g->f[far] = MK(T_SPACE, 1);
        g->m_code = (d == DIR_UP) ? 0x24 : (d == DIR_LEFT) ? 0x25
                  : (d == DIR_RIGHT) ? 0x26 : 0x27;
        murphy_arm(g, d, ACT_PUSH, -1, t, far, T_YELLOW_DISK); return;
    }
    if (horiz && w == T_ORANGE_DISK) {
        if (far < 0 || far >= LVL_TILES || g->f[far] != 0) return;
        g->f[far] = MK(T_SPACE, 1);
        g->m_code = (d == DIR_LEFT) ? 0x28 : 0x29;
        murphy_arm(g, d, ACT_PUSH, -1, t, far, T_ORANGE_DISK); return;
    }
    if (tt == T_SNIKSNAK || tt == T_ELECTRON) {
        g->dead = true; g->death = "walked into enemy"; return;
    }
    /* 46c2:8539: everything else blocks, except an explosion that has already
     * decayed past state 4, which Murphy clears and then walks into. */
    if (tt == T_EXPLOSION && !(HI(w) & 0x80) && HI(w) >= 4) {
        g->f[t] = 0;
        murphy_begin(g, d, space_held);
        return;
    }
    g->blocked_by = w; g->blocked_dir = d;
}

/* FUN_46c2_85c9: a port whose state byte is 1 is a special port; find its
 * record in the level header and apply the gravity / freeze settings. */
static void apply_special_port(Game *g, int cell)
{
    if (cell < 0 || HI(g->f[cell]) != 1) return;
    for (int p = 0; p < g->n_ports; p++) {
        if (g->ports[p].pos != (uint16_t)(cell * 2)) continue;
        g->gravity        = g->ports[p].gravity != 0;
        g->freeze_zonks   = g->ports[p].freeze_zonks;
        g->freeze_enemies = g->ports[p].freeze_enemies != 0;
        return;
    }
}

static void murphy_complete(Game *g)
{
    g->m_code = 0;
    if (LO(g->f[g->murphy]) == T_MURPHY) g->f[g->murphy] = MK(T_MURPHY, 0);
    switch (g->m_act) {
    case ACT_MOVE:                       vacate(g, g->m_from); break;
    case ACT_INFOTRON: g->infotrons++;   vacate(g, g->m_from); break;
    case ACT_REDDISK:  g->red_disks++;   vacate(g, g->m_from); break;
    case ACT_PUSH:                       /* Murphy steps in, object moves on */
        g->f[g->m_dst] = MK(T_MURPHY, 0);
        g->murphy = g->m_dst;
        g->f[g->m_aux] = g->m_aux_tile;
        vacate(g, g->m_from);
        break;
    case ACT_PORT:                       /* Murphy lands beyond the port */
        g->f[g->m_from] = 0;             /* the original does not wake neighbours here */
        g->f[g->m_dst] = MK(T_MURPHY, 0);
        g->murphy = g->m_dst;
        apply_special_port(g, g->port_cell);
        break;
    case ACT_EXIT:      g->finished = true; break;
    case ACT_EAT:       g->f[g->m_aux] = 0; break;
    case ACT_EAT_INFO:  g->f[g->m_aux] = 0; g->infotrons++; break;
    case ACT_EAT_RED:   g->f[g->m_aux] = 0; g->red_disks++; break;
    case ACT_DROP_RED:                   /* left behind when Murphy moves off */
        if (g->red_disks > 0) { g->red_disks--; g->drop_pending = true; }
        break;
    }
    if (g->drop_pending && g->m_from != g->murphy && g->f[g->m_from] == 0) {
        g->f[g->m_from] = T_RED_DISK;
        g->drop_pending = false;
    }
    g->m_from = g->murphy;
}

static void murphy_step(Game *g, Dir want, bool space_held)
{
    if (g->dead || g->finished) return;

    if (g->m_delay > 0) {
        if (--g->m_delay == 0) sp_sound_play(SFX_PUSH);   /* FUN_46c2_6d89 */
        return;
    }
    if (g->m_step > 0) {
        /* The original's animation list is eight frames long and completes on
         * the last one, so the cell Murphy left frees up on frame 8. */
        if (++g->m_step < murphy_frames(g->m_act)) return;
        g->m_step = 0; g->m_dir = DIR_NONE;
        murphy_complete(g);
        return;
    }

    /* gravity: fall if nothing is below and there is no up-port overhead */
    if (g->gravity && g->f[g->murphy + ROW] == 0 &&
        !is_port_for(LO(g->f[g->murphy - ROW]), DIR_UP)) {
        bool holding = (want == DIR_UP    && g->f[g->murphy - ROW] == T_BASE) ||
                       (want == DIR_LEFT  && g->f[g->murphy - 1]   == T_BASE) ||
                       (want == DIR_RIGHT && g->f[g->murphy + 1]   == T_BASE);
        if (!holding) { murphy_begin(g, DIR_DOWN, false); return; }
    }
    if (want != DIR_NONE) { murphy_begin(g, want, space_held); return; }
    if (space_held && g->red_disks > 0 && g->f[g->murphy] == MK(T_MURPHY, 0))
        murphy_arm(g, DIR_NONE, ACT_DROP_RED, -1, -1, g->murphy, 0);
}

/* ------------------------------------------------------------ main loop */
typedef void (*Handler)(Game *, int);
static Handler handler_for(uint8_t t)
{
    switch (t) {
    case T_ZONK:        return h_zonk;
    case T_INFOTRON:    return h_infotron;
    case T_ORANGE_DISK: return h_orange;
    case T_SNIKSNAK:    return h_sniksnak;
    case T_TERMINAL:    return h_terminal;
    case T_ELECTRON:    return h_electron;
    case T_BUG:         return h_bug;
    case T_EXPLOSION:   return h_explosion;
    default:            return NULL;
    }
}

void game_step(Game *g, Dir want, bool space_held)
{
    if (!g->dead && !g->finished) {
        murphy_step(g, want, space_held);

        /* Scan then dispatch, exactly as the original does: the handler for a
         * cell is chosen from the field as it was at scan time. */
        static int  qc[LVL_TILES];
        static uint8_t qt[LVL_TILES];
        int n = 0;
        for (int i = 61; i <= 1378; i++) {
            uint8_t t = LO(g->f[i]);
            if (!(t & 0x0d) || t >= 0x20) continue;
            if (!handler_for(t)) continue;
            qc[n] = i; qt[n] = t; n++;
        }
        for (int k = 0; k < n; k++) handler_for(qt[k])(g, qc[k]);
        tick_explosion_timers(g);

        /* Reservation markers are NOT swept each frame: the original leaves
         * them in place until the object that made them consumes them. */
    }
    g->frame++;
    if (g->frame % FPS_GAME == 0 && !g->finished && !g->dead) g->seconds++;
}

/* ------------------------------------------------------------- rendering */
/* The original draws the level once from the raw level bytes and only repaints
 * cells as objects move through them, so the decorative two-tile chips and the
 * ten hardware variants keep their own graphics even though the simulation has
 * folded them into RAM chips and plain hardware. */
static uint8_t render_tile(const Game *g, int i)
{
    uint8_t f = LO(g->f[i]), l = g->look[i];
    if (f == T_RAM_CHIP && (l == T_CHIP_L || l == T_CHIP_R ||
                            l == T_CHIP_T || l == T_CHIP_B)) return l;
    if (f == T_HARDWARE && l >= T_HW0 && l <= T_HW9) return l;
    if (f >= T_PORT_R && f <= T_PORT_U &&
        l >= T_SPORT_R && l <= T_SPORT_U) return l;
    return f;
}

/* Murphy's animation for the action in progress.  The descriptor table at
 * DS:0x0dfe holds 34 entries; walking up and down each have a pair, chosen by
 * DAT_5024_0dbb (which way he last moved horizontally). */
static const Anim *murphy_anim(const Game *g)
{
    if (g->m_step <= 0 || g->m_delay > 0) return NULL;
    const int L = g->facing_left;
    switch (g->m_code) {
    case 0x01: return anim_murphy[L ? 0 : 1];
    case 0x02: return anim_murphy[2];
    case 0x03: return anim_murphy[L ? 3 : 4];
    case 0x04: return anim_murphy[5];
    case 0x05: return anim_murphy[L ? 7 : 8];
    case 0x06: return anim_murphy[9];
    case 0x07: return anim_murphy[L ? 10 : 11];
    case 0x08: return anim_murphy[12];
    case 0x09: return anim_murphy[L ? 17 : 18];
    case 0x0a: return anim_murphy[19];
    case 0x0b: return anim_murphy[L ? 20 : 21];
    case 0x0c: return anim_murphy[22];
    case 0x0e: return anim_murphy[27];
    case 0x0f: return anim_murphy[28];
    case 0x10: return anim_murphy[13];
    case 0x11: return anim_murphy[14];
    case 0x12: return anim_murphy[15];
    case 0x13: return anim_murphy[16];
    case 0x14: return anim_murphy[23];
    case 0x15: return anim_murphy[24];
    case 0x16: return anim_murphy[25];
    case 0x17: return anim_murphy[26];
    default:   return NULL;          /* ports and disks: static sprite */
    }
}

/* Blit one animation frame from MOVING.DAT at a pixel position. */
static void blit_frame(Image *scr, const GameData *gd, Frame f, int w, int h,
                       int x, int y)
{
    sp_blit(scr, &gd->moving, f.x, f.y, w, h, x, y);
}

/* Which animation a moving zonk / infotron / orange disk is showing, and where
 * it sits relative to its own cell.  States follow the original: 0x10+n
 * falling, 0x20+n rolled left, 0x30+n rolled right, 0x50/0x60 the first two
 * frames of a roll, 0x40/0x41 the pre-fall wobble. */
static bool moving_object_sprite(const Game *g, int i, Frame *out,
                                 int *w, int *h, int *ox, int *oy)
{
    uint16_t word = g->f[i];
    uint8_t t = LO(word), st = HI(word), top = st & 0xf0;
    int n = st & 7;
    const Frame *fall, *rl, *rr;
    if (t == T_ZONK)          { fall = anim_zonk_fall; rl = anim_zonk_rollL; rr = anim_zonk_rollR; }
    else if (t == T_INFOTRON) { fall = anim_info_fall; rl = anim_info_rollL; rr = anim_info_rollR; }
    else if (t == T_ORANGE_DISK) { fall = anim_orange; rl = NULL; rr = NULL; }
    else return false;

    if (top == 0x10) {                       /* falling: 16x18 from the cell above */
        *out = fall[0]; *w = 16; *h = 18; *ox = 0; *oy = -TILE + n * MOVE_PX;
        return true;
    }
    if (t == T_ORANGE_DISK && top == 0x30) { /* orange disks fall in their own group */
        *out = fall[0]; *w = 16; *h = 18; *ox = 0; *oy = -TILE + n * MOVE_PX;
        return true;
    }
    if (!rl) return false;
    if (top == 0x20) { *out = rl[n]; *w = 32; *h = 16; *ox = 0;      *oy = 0; return true; }
    if (top == 0x30) { *out = rr[n]; *w = 32; *h = 16; *ox = -TILE;  *oy = 0; return true; }
    if (top == 0x50) { *out = rl[st & 1]; *w = 32; *h = 16; *ox = -TILE; *oy = 0; return true; }
    if (top == 0x60) { *out = rr[st & 1]; *w = 32; *h = 16; *ox = 0;     *oy = 0; return true; }
    return false;                            /* 0x40/0x41/0x70: draw at rest */
}

void game_draw(const Game *g, const GameData *gd, Image *screen)
{
    int sx, sy;
    game_viewport(g, &sx, &sy);
    int tx0 = sx / TILE, ty0 = sy / TILE, ox = sx % TILE, oy = sy % TILE;

    /* ---- pass 1: the static layer ---- */
    for (int ry = -1; ry <= VIEW_H / TILE + 1; ry++)
        for (int rx = -1; rx <= VIEW_W / TILE + 1; rx++) {
            int lx = tx0 + rx, ly = ty0 + ry;
            if (lx < 0 || lx >= LVL_W || ly < 0 || ly >= LVL_H) continue;
            int dx = rx * TILE - ox, dy = ry * TILE - oy;
            if (lx == 0 || lx == LVL_W - 1 || ly == 0 || ly == LVL_H - 1) {
                bool bx = (lx == 0 || lx == LVL_W - 1);
                bool by = (ly == 0 || ly == LVL_H - 1);
                const Image *src = (bx && by) ? &gd->border_c
                                 : bx ? &gd->border_v : &gd->border_h;
                sp_blit(screen, src, 0, 0, TILE, TILE, dx, dy);
                continue;
            }
            int i = ly * LVL_W + lx;
            uint16_t w = g->f[i];
            uint8_t raw = LO(w), st = HI(w);
            uint8_t t = render_tile(g, i);
            if (t >= T_COUNT) t = T_SPACE;          /* transient markers */

            if (raw == T_EXPLOSION) {
                int fr = st & 7;
                blit_frame(screen, gd, (st & 0x80) ? anim_explosion_info[fr]
                                                   : anim_explosion[fr],
                           TILE, TILE, dx, dy);
                continue;
            }
            if (raw == T_BUG) {
                int fr = (int8_t)st < 0 ? 0 : (st > 15 ? 15 : st);
                blit_frame(screen, gd, anim_bug[fr], TILE, TILE, dx, dy);
                continue;
            }
            if (raw == T_SNIKSNAK || raw == T_ELECTRON) {
                const Frame *tab = (raw == T_SNIKSNAK) ? anim_snik : anim_elec;
                if (st < 0x10) {                    /* turning on the spot */
                    blit_frame(screen, gd, tab[st], TILE, TILE, dx, dy);
                } else {
                    sp_blit(screen, &gd->fixed, T_SPACE * TILE, 0, TILE, TILE, dx, dy);
                }
                continue;
            }
            if (raw == T_MURPHY) {                  /* drawn in pass 3 */
                sp_blit(screen, &gd->fixed, T_SPACE * TILE, 0, TILE, TILE, dx, dy);
                continue;
            }
            Frame fr; int fw, fh, fx, fy;
            if (moving_object_sprite(g, i, &fr, &fw, &fh, &fx, &fy)) {
                sp_blit(screen, &gd->fixed, T_SPACE * TILE, 0, TILE, TILE, dx, dy);
                continue;                           /* sprite comes in pass 2 */
            }
            sp_blit(screen, &gd->fixed, t * TILE, 0, TILE, TILE, dx, dy);
        }

    /* ---- pass 2: objects in motion, drawn over the static layer ---- */
    for (int ry = -1; ry <= VIEW_H / TILE + 1; ry++)
        for (int rx = -1; rx <= VIEW_W / TILE + 1; rx++) {
            int lx = tx0 + rx, ly = ty0 + ry;
            if (lx < 1 || lx >= LVL_W - 1 || ly < 1 || ly >= LVL_H - 1) continue;
            int i = ly * LVL_W + lx;
            int dx = rx * TILE - ox, dy = ry * TILE - oy;
            uint8_t raw = LO(g->f[i]), st = HI(g->f[i]);
            Frame fr; int fw, fh, fx, fy;
            if (moving_object_sprite(g, i, &fr, &fw, &fh, &fx, &fy)) {
                blit_frame(screen, gd, fr, fw, fh, dx + fx, dy + fy);
                continue;
            }
            if ((raw == T_SNIKSNAK || raw == T_ELECTRON) && st >= 0x10) {
                const Frame *tab = (raw == T_SNIKSNAK) ? anim_snik : anim_elec;
                int d = (st - 0x10) >> 3, n = st & 7;
                static const int DXP[4] = { 0, -1, 0, 1 }, DYP[4] = { -1, 0, 1, 0 };
                int step = (n + 1) * MOVE_PX;
                blit_frame(screen, gd, tab[st], TILE, 18,
                           dx - DXP[d] * TILE + DXP[d] * step,
                           dy - DYP[d] * TILE + DYP[d] * step);
            }
        }

    /* ---- pass 3: Murphy ---- */
    {
        int px, py;
        game_murphy_px(g, &px, &py);
        if (!g->dead) {
            const Anim *a = murphy_anim(g);
            if (a && a->n > 0) {
                int k = g->m_step > 0 ? g->m_step - 1 : 0;
                if (k >= a->n) k = a->n - 1;
                int bx = cx(g->murphy) * TILE - sx + a->dx0 + a->ddx * k;
                int by = cy(g->murphy) * TILE - sy + a->dy0 + a->ddy * k;
                blit_frame(screen, gd, a->f[k], a->w, a->h, bx, by);
            } else {
                sp_blit(screen, &gd->fixed, T_MURPHY * TILE, 0, TILE, TILE,
                        px - sx, py - sy);
            }
        }
    }

    /* status panel */
    sp_blit(screen, &gd->panel, 0, 0, SCR_W, PANEL_H, 0, VIEW_H);
    char buf[32];
    const int R1 = VIEW_H + 3, R2 = VIEW_H + 14;
    sp_text(screen, &gd->chars8, 72, R1, g->player, 6);
    int t = g->seconds;
    snprintf(buf, sizeof buf, "%02d", (t / 3600) % 100);
    sp_text(screen, &gd->chars8, 160, R1, buf, 6);
    snprintf(buf, sizeof buf, "%02d", (t / 60) % 60);
    sp_text(screen, &gd->chars8, 184, R1, buf, 6);
    snprintf(buf, sizeof buf, "%02d", t % 60);
    sp_text(screen, &gd->chars8, 208, R1, buf, 6);
    snprintf(buf, sizeof buf, "%03d", g->level_no);
    sp_text(screen, &gd->chars8, 16, R2, buf, 8);
    sp_text(screen, &gd->chars8, 64, R2, g->title, 8);
    if (g->red_disks > 0) {          /* only shown while Murphy carries disks */
        snprintf(buf, sizeof buf, "%d", g->red_disks % 10);
        sp_text(screen, &gd->chars8, 248, R2, buf, 8);
    }
    int left = g->infotrons_needed - g->infotrons;
    if (left < 0) left = 0;
    snprintf(buf, sizeof buf, "%03d", left);
    sp_text(screen, &gd->chars8, 272, R2, buf, 8);
}
