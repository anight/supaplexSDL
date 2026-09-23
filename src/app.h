#ifndef APP_H
#define APP_H
#include <stdbool.h>

typedef struct {
    const char *dir;        /* data directory (desktop only)                   */
    int   level;            /* 1-based                                         */
    bool  direct;           /* start that level at once, skipping the screens  */
    int   scale;            /* desktop window scale                            */
    const char *player;
    int   music;            /* 1 on, 0 off, -1 as SUPAPLEX.CFG says            */
} AppConfig;

/* The interactive game: start screens, level select, play.  Returns when the
 * player quits (never, on a board). */
int  sp_app_run(const AppConfig *cfg);

/* Called once a frame after the present, if set: a board uses it to report
 * on itself without the game knowing anything about the board. */
extern void (*sp_app_frame_hook)(void);

/* SUPAPLEX.CFG is four lower-case bytes; the third is "m" for music on and
 * "n" for off (SPFIX62.DOC).  The shipped file says "bkmx". */
bool sp_cfg_music(const char *dir);

#endif
