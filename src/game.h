#ifndef GAME_H
#define GAME_H
#include "sp.h"

/* The playfield mirrors the original's in-memory layout: one 16-bit word per
 * cell, low byte = tile id, high byte = per-object animation/movement state.
 * Reserved "magic" words mark cells that an object is moving into or out of. */
#define ROW        LVL_W
#define LO(w)      ((uint8_t)((w) & 0xff))
#define HI(w)      ((uint8_t)((w) >> 8))
#define MK(t, s)   ((uint16_t)(((uint16_t)(t)) | (((uint16_t)(s)) << 8)))

#define W_ROLLRES  0x8888u      /* cell reserved by an object rolling into it  */
#define W_FALLRES  0x9999u      /* cell reserved by an object falling into it  */
#define W_ROLLFREE 0xAAAAu      /* cell an object has just rolled out of       */
#define W_VACATE   0xFFFFu      /* cell an object has just left                */

/* Murphy movement, 8 frames of 2 pixels per tile. */
#define MOVE_FRAMES 8
#define MOVE_PX     2

/* direction codes, matching the original's DS:0x0631 */
typedef enum { DIR_NONE = 0, DIR_UP = 1, DIR_LEFT = 2, DIR_DOWN = 3, DIR_RIGHT = 4 } Dir;

/* what to do once Murphy's walk animation completes */
typedef enum {
    ACT_MOVE,        /* plain step (also eating a base)          */
    ACT_INFOTRON,    /* step that collects an infotron           */
    ACT_REDDISK,     /* step that collects a red disk            */
    ACT_PUSH,        /* step that pushes an object to m_aux      */
    ACT_PORT,        /* step through a port, landing on m_aux    */
    ACT_EXIT,        /* level complete                           */
    ACT_EAT,         /* space+dir: consume a neighbour, no move  */
    ACT_EAT_INFO,    /* space+dir on an infotron                 */
    ACT_EAT_RED,     /* space+dir on a red disk                  */
    ACT_DROP_RED     /* space alone: drop a red disk             */
} Act;

typedef struct {
    uint16_t f[LVL_TILES];
    uint8_t  look[LVL_TILES];      /* level tiles as drawn (decorations kept) */
    int8_t   timer[LVL_TILES];     /* DS:0x2434 delayed-explosion counters    */

    int  murphy;                   /* cell Murphy occupies (or is entering) */
    int  m_from;                   /* cell he is walking out of             */
    Dir  m_dir;
    int  m_step;                   /* 0 = idle, else 1..MOVE_FRAMES         */
    bool facing_left;              /* DAT_5024_0dbb: last horizontal move   */
    uint8_t m_code;                /* action code kept in Murphy's cell      */
    int  m_delay;                  /* frames of lean-in before a push starts */
    Act  m_act;
    int  m_dst;                    /* cell Murphy ends the step in          */
    int  m_aux;                    /* pushed-object / port destination cell */
    uint8_t m_aux_tile;            /* what to place there on completion     */
    bool drop_pending;
    int  terminal_hit;             /* terminal Murphy bumped into this frame */
    uint16_t blocked_by;           /* field word that refused the last move */
    Dir  blocked_dir;             /* leave a red disk in the cell just left */

    int  infotrons, infotrons_needed;
    int  red_disks;
    bool gravity;
    bool freeze_enemies;
    uint8_t term_mask;             /* DS:0x165a terminal flicker mask         */
    bool term_armed;               /* DS:0x165b set once a terminal is used   */
    int  port_cell;                /* port Murphy is stepping through         */
    struct { uint16_t pos; uint8_t gravity, freeze_zonks, freeze_enemies; } ports[10];
    int  n_ports;
    int  freeze_zonks;             /* 2 = frozen, as in the original        */
    bool dead, finished;
    const char *death;   /* why Murphy died (debugging) */
    int  explosions;               /* live explosion cells                  */

    uint16_t rng;                  /* seed = seed*0x5E5 + 0x31              */
    int  frame;
    int  level_no, seconds;
    char title[24];
    char player[9];
} Game;

void game_start(Game *g, const Level *lv, int level_no);
void game_step(Game *g, Dir want, bool space_held);
void game_viewport(const Game *g, int *sx, int *sy);
void game_murphy_px(const Game *g, int *px, int *py);
void game_draw(const Game *g, const GameData *gd, Image *screen);
uint16_t game_rand(Game *g);

#endif
