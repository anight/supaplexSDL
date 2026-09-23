/* picosupaplex - a from-scratch C/SDL2 reimplementation of Supaplex (1991),
 * reverse engineered from SPFIX62.EXE and the original .DAT data files.
 *
 * All formats below were recovered by analysis and verified pixel-exact
 * against the original running under DOSBox.
 */
#ifndef SP_H
#define SP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "asset.h"

/* ---- screen geometry (VGA mode 320x200, 16 colours) ---- */
#define SCR_W        320
#define SCR_H        200
#define PANEL_H       24
#define VIEW_W       SCR_W
#define VIEW_H       (SCR_H - PANEL_H)     /* 176 = 11 tiles */
#define TILE          16

/* ---- level geometry ---- */
#define LVL_W         60
#define LVL_H         24
#define LVL_TILES    (LVL_W * LVL_H)       /* 1440 */
#define LEVEL_BYTES 1536                   /* 1440 tiles + 96 byte trailer */
#define NUM_LEVELS   111

/* ---- tile ids (0..39), index straight into FIXED.DAT ---- */
enum {
    T_SPACE = 0, T_ZONK, T_BASE, T_MURPHY, T_INFOTRON, T_RAM_CHIP, T_HARDWARE,
    T_EXIT, T_ORANGE_DISK, T_PORT_R, T_PORT_D, T_PORT_L, T_PORT_U,
    T_SPORT_R, T_SPORT_D, T_SPORT_L, T_SPORT_U, T_SNIKSNAK, T_YELLOW_DISK,
    T_TERMINAL, T_RED_DISK, T_PORT_V, T_PORT_H, T_PORT_X, T_ELECTRON, T_BUG,
    T_CHIP_L, T_CHIP_R, T_HW0, T_HW1, T_HW2, T_EXPLOSION, T_HW4, T_HW5, T_HW6,
    T_HW7, T_HW8, T_HW9, T_CHIP_T, T_CHIP_B, T_COUNT
};

/* Runtime-only marker left in the cell a snik snak or electron is moving out
 * of; the high byte carries the direction (1 up, 2 left, 3 down, 4 right). */
#define T_ENEMY_TAIL 0xBB

/* ---- a 1bpp bitmap font: 64 glyphs of 8x8, stored as a 512x8 strip.
 * Glyph index = character - 0x20 (so it covers ' ' .. '_'). ---- */
typedef struct { uint8_t bits[8][512]; } Font;

/* ---- an indexed (palette) image.  `owned` says px is heap memory; when it
 * is not, px may point at read-only data in flash and is never written. ---- */
typedef struct { int w, h; uint8_t *px; bool owned; } Image;

/* ---- one palette: 16 RGB entries, already expanded to 8 bit ---- */
typedef struct { uint8_t r[16], g[16], b[16]; } Palette;

/* ---- a parsed level ---- */
typedef struct {
    uint8_t tiles[LVL_TILES];
    char    title[24];
    uint8_t gravity;           /* initial gravitation on/off              */
    uint8_t freeze_zonks;      /* initial "zonks frozen" flag             */
    uint8_t infotrons_needed;  /* 0 means "all of them"                   */
    uint8_t n_special_ports;
    struct { uint16_t pos; uint8_t gravity, freeze_zonks, freeze_enemies; }
            ports[10];
} Level;

/* ---- loaded game data ---- */
typedef struct {
    Palette pal[4];
    Image   fixed;             /* 640x16  : the 40 static tiles           */
    Image   border_v, border_h, border_c;  /* level edge frame, 16x16 each  */
    Image   moving;            /* 320x462 : animation frames              */
    Image   panel;             /* 320x24  : status panel background       */
    Image   title, title1, title2, menu, back, gfx, controls;   /* 320x200 */
    Font    chars6, chars8;    /* bitmap fonts                            */
    Asset   levels_dat;        /* LEVELS.DAT as it is; see sp_level()     */
    int     n_levels;
    char    level_names[NUM_LEVELS][28];
    uint8_t border_px[3][256];
} GameData;

/* data.c */
bool  sp_load_all(GameData *gd, const char *dir);
void  sp_free_all(GameData *gd);
bool  sp_level(const GameData *gd, int index, Level *out);   /* 0-based */
void  sp_parse_level(const uint8_t *raw, Level *out);        /* 1536 bytes */
bool  sp_decode_planar(const uint8_t *data, size_t len, int w, int h, Image *out);

/* render.c */
#define SP_TRANSPARENT (-1)
void  sp_text(Image *dst, const Font *f, int x, int y, const char *s, uint8_t colour);
void  sp_text_bg(Image *dst, const Font *f, int x, int y, const char *s,
                 uint8_t colour, int bg);
void  sp_text_adv(Image *dst, const Font *f, int x, int y, const char *s,
                  uint8_t colour, int bg, int adv);

void  sp_blit(Image *dst, const Image *src, int sx, int sy, int w, int h, int dx, int dy);
void  sp_clear(Image *dst, uint8_t c);

void  sp_sound_play(int fx);      /* implemented in sound.c; no-op without audio */

#endif
