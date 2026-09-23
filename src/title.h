#ifndef TITLE_H
#define TITLE_H
#include "sp.h"

/* The boot sequence the original plays before the game proper:
 *
 *   TITLE.DAT fades up, holds, fades back down; the screen is replaced by
 *   TITLE2.DAT which fades up in a palette where the credits are invisible
 *   (they are drawn into the artwork, but their colour is the same grey as
 *   the panel behind them); that colour then morphs to dark blue so the
 *   credits appear; finally the two speed-fix version lines are drawn.
 *
 * Every fade is the original's: the VGA DAC gets (nibble * 4 * k) >> 6 with
 * k running 0..64, which is a plain linear intensity ramp.
 */
typedef struct {
    int  phase;
    int  t;            /* ticks elapsed in this phase */
    bool done;         /* sequence over, hand control to the menu */
    Palette pal;       /* palette to present this frame */
} Title;

void title_init(Title *ti);
void title_key(Title *ti);                 /* any key: skip to the next phase */
void title_step(Title *ti, const GameData *gd, Image *screen);

#endif
