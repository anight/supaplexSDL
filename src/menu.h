#ifndef MENU_H
#define MENU_H
#include "sp.h"
#include <SDL2/SDL.h>

/* A level-select front end drawn on the original MENU.DAT artwork.  This is
 * not a reimplementation of Supaplex's own menu (which manages players, the
 * hall of fame and rankings); it covers choosing a level and seeing progress. */
typedef struct {
    int  sel;                 /* highlighted level, 0-based */
    int  top;                 /* first visible row          */
    bool quit, play;
} Menu;

void menu_init(Menu *m, int level);
void menu_key(Menu *m, SDL_Scancode k);
void menu_draw(const Menu *m, const GameData *gd, Image *screen,
               const bool *solved);

#endif
