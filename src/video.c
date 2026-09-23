#include "video.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PICOSDL_SDL_H
/* ---------------------------------------------------------------- picosdl
 *
 * One buffer, owned here, handed to the panel with PSDL_PresentBuffer().  A
 * present only starts the transfer, so the next frame waits for it to finish
 * before drawing over the pixels it is reading.  The palette is the display's
 * colour table, so a fade is 16 register writes and nothing else. */
static uint8_t canvas[SCR_W * SCR_H];
static SDL_Color shown[16];
static bool shown_valid;

bool video_open(int scale, Image *screen)
{
    (void)scale;
    screen->w = SCR_W; screen->h = SCR_H; screen->px = canvas; screen->owned = false;
    memset(canvas, 0, sizeof canvas);
    return true;
}

void video_begin_frame(void) { PSDL_PresentSync(); }

void video_present(const Image *screen, const Palette *pal)
{
    SDL_Color c[16];
    for (int i = 0; i < 16; i++)
        c[i] = (SDL_Color){ pal->r[i], pal->g[i], pal->b[i], SDL_ALPHA_OPAQUE };
    if (!shown_valid || memcmp(c, shown, sizeof c) != 0) {
        SDL_SetPaletteColors(PSDL_GlobalPalette(), c, 0, 16);
        memcpy(shown, c, sizeof c);
        shown_valid = true;
    }
    PSDL_PresentBuffer(screen->px, SCR_W, SCR_H, SCR_W);
}

void video_close(void) { PSDL_PresentSync(); }

#else
/* ---------------------------------------------------------------- SDL2 */
static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint8_t      *pixels;

bool video_open(int scale, Image *screen)
{
    win = SDL_CreateWindow("supaplexSDL",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCR_W * scale, SCR_H * scale, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    if (!ren || !tex) { fprintf(stderr, "SDL renderer: %s\n", SDL_GetError()); return false; }
    SDL_RenderSetLogicalSize(ren, SCR_W, SCR_H);
    pixels = calloc(SCR_W * SCR_H, 1);
    screen->w = SCR_W; screen->h = SCR_H; screen->px = pixels; screen->owned = true;
    return pixels != NULL;
}

void video_begin_frame(void) { }

void video_present(const Image *scr, const Palette *pal)
{
    uint32_t *pix; int pitch;
    if (SDL_LockTexture(tex, NULL, (void **)&pix, &pitch) == 0) {
        uint32_t lut[16];
        for (int i = 0; i < 16; i++)
            lut[i] = 0xFF000000u | ((uint32_t)pal->r[i] << 16)
                   | ((uint32_t)pal->g[i] << 8) | pal->b[i];
        for (int y = 0; y < scr->h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pix + (size_t)y * pitch);
            const uint8_t *src = scr->px + (size_t)y * scr->w;
            for (int x = 0; x < scr->w; x++) row[x] = lut[src[x] & 15];
        }
        SDL_UnlockTexture(tex);
    }
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

void video_close(void)
{
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    free(pixels);
    tex = NULL; ren = NULL; win = NULL; pixels = NULL;
}
#endif
