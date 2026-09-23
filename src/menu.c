#include "menu.h"
#include <stdio.h>
#include <string.h>

#define ROWS 11
#define LIST_X 24
#define LIST_Y 40

void menu_init(Menu *m, int level)
{
    memset(m, 0, sizeof *m);
    m->sel = level - 1;
    if (m->sel < 0) m->sel = 0;
    if (m->sel >= NUM_LEVELS) m->sel = NUM_LEVELS - 1;
    m->top = m->sel - ROWS / 2;
    if (m->top > NUM_LEVELS - ROWS) m->top = NUM_LEVELS - ROWS;
    if (m->top < 0) m->top = 0;
}

void menu_key(Menu *m, SDL_Scancode k)
{
    switch (k) {
    case SDL_SCANCODE_UP:     m->sel--; break;
    case SDL_SCANCODE_DOWN:   m->sel++; break;
    case SDL_SCANCODE_PAGEUP: m->sel -= ROWS; break;
    case SDL_SCANCODE_PAGEDOWN: m->sel += ROWS; break;
    case SDL_SCANCODE_HOME:   m->sel = 0; break;
    case SDL_SCANCODE_END:    m->sel = NUM_LEVELS - 1; break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: m->play = true; break;
    case SDL_SCANCODE_ESCAPE: m->quit = true; break;
    default: break;
    }
    if (m->sel < 0) m->sel = 0;
    if (m->sel >= NUM_LEVELS) m->sel = NUM_LEVELS - 1;
    if (m->sel < m->top) m->top = m->sel;
    if (m->sel >= m->top + ROWS) m->top = m->sel - ROWS + 1;
    if (m->top > NUM_LEVELS - ROWS) m->top = NUM_LEVELS - ROWS;
    if (m->top < 0) m->top = 0;
}

static void bar(Image *scr, int x, int y, int w, int h, uint8_t c)
{
    for (int j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= scr->h) continue;
        for (int i = 0; i < w; i++) {
            if (x + i < 0 || x + i >= scr->w) continue;
            scr->px[(size_t)(y + j) * scr->w + x + i] = c;
        }
    }
}

void menu_draw(const Menu *m, const GameData *gd, Image *screen, const bool *solved)
{
    sp_blit(screen, &gd->menu, 0, 0, SCR_W, SCR_H, 0, 0);

    /* a panel behind the list so the artwork does not fight the text */
    bar(screen, LIST_X - 8, LIST_Y - 12, 272, ROWS * 10 + 22, 0);

    sp_text(screen, &gd->chars8, LIST_X, LIST_Y - 11, "SELECT A LEVEL", 2);

    for (int r = 0; r < ROWS; r++) {
        int n = m->top + r;
        if (n < 0 || n >= NUM_LEVELS) continue;
        int y = LIST_Y + r * 10;
        bool cur = (n == m->sel);
        if (cur) bar(screen, LIST_X - 4, y - 1, 264, 10, 9);
        char buf[40];
        snprintf(buf, sizeof buf, "%-27.27s", gd->level_names[n]);
        sp_text_bg(screen, &gd->chars8, LIST_X, y, buf,
                   cur ? 1 : (solved && solved[n] ? 4 : 8), SP_TRANSPARENT);
        if (solved && solved[n])
            sp_text_bg(screen, &gd->chars8, LIST_X + 244, y, "*", 2, SP_TRANSPARENT);
    }

    sp_text(screen, &gd->chars8, LIST_X, LIST_Y + ROWS * 10 + 2,
            "ARROWS SELECT   ENTER PLAYS", 6);
}
