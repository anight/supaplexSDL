/* The start-screen sequence, as SPFIX62.EXE plays it.
 *
 * Timings were measured from a 20 fps capture of the original under DOSBox
 * and are expressed here in 35 Hz game ticks.  The screen contents, the three
 * palettes and the three text lines all come from the executable; see
 * re/tools/genpal.py and the strings at DS:9105 for their provenance. */
#include "title.h"
#include "titlepal.h"
#include <string.h>

enum { PH_TITLE_IN, PH_TITLE_HOLD, PH_TITLE_OUT,
       PH_CRED_IN, PH_CRED_TEXT, PH_CRED_HOLD, PH_DONE };

/* ticks per phase; PH_CRED_HOLD waits for a keypress instead */
static const int phase_len[] = { 32, 35, 8, 11, 35, 0, 0 };

/* The speed-fix banner and the two porting credits, drawn in CHARS6 with a
 * 6 pixel advance.  x/y are where the original puts them. */
#define BANNER    "SUPAPLEX SPEED FIX VERSION 6.2"
#define VERSION1  "VERSIONS 1-4 + 6.X BY HERMAN PERK"
#define VERSION2  "VERSIONS 5.X BY ELMER PRODUCTIONS"

/* nibble -> 8-bit, dimmed to k/64 of full intensity the way the DAC sees it */
static void pal_fade(Palette *dst, const uint8_t src[16][3], int k)
{
    for (int c = 0; c < 16; c++) {
        uint8_t *o[3] = { &dst->r[c], &dst->g[c], &dst->b[c] };
        for (int i = 0; i < 3; i++) {
            int dac = (src[c][i] * 4 * k) >> 6;      /* 6-bit DAC value */
            *o[i] = (uint8_t)((dac << 2) | (dac >> 4));
        }
    }
}

/* linear interpolation between two nibble palettes, k of 64 */
static void pal_mix(Palette *dst, const uint8_t a[16][3],
                    const uint8_t b[16][3], int k)
{
    uint8_t mix[16][3];
    for (int c = 0; c < 16; c++)
        for (int i = 0; i < 3; i++)
            mix[c][i] = (uint8_t)((a[c][i] * (64 - k) + b[c][i] * k) / 64);
    pal_fade(dst, mix, 64);
}

void title_init(Title *ti)
{
    memset(ti, 0, sizeof *ti);
    ti->phase = PH_TITLE_IN;
}

void title_key(Title *ti)
{
    /* a key skips the rest of the current phase; on the final screen it ends
     * the sequence, which is how the original gets you into the menu */
    if (ti->phase >= PH_CRED_HOLD) { ti->done = true; return; }
    ti->phase++;
    ti->t = 0;
}

static void draw_credits(const GameData *gd, Image *screen, bool versions)
{
    sp_blit(screen, &gd->title2, 0, 0, SCR_W, SCR_H, 0, 0);
    sp_text_adv(screen, &gd->chars6, 72, 11, BANNER, 1, SP_TRANSPARENT, 6);
    if (versions) {
        sp_text_adv(screen, &gd->chars6, 64, 170, VERSION1, 2, SP_TRANSPARENT, 6);
        sp_text_adv(screen, &gd->chars6, 64, 180, VERSION2, 2, SP_TRANSPARENT, 6);
    }
}

void title_step(Title *ti, const GameData *gd, Image *screen)
{
    int len = phase_len[ti->phase];
    if (len && ti->t >= len) { ti->phase++; ti->t = 0; }

    switch (ti->phase) {
    case PH_TITLE_IN:
        sp_blit(screen, &gd->title, 0, 0, SCR_W, SCR_H, 0, 0);
        pal_fade(&ti->pal, title_pal[TP_TITLE], ti->t * 64 / phase_len[PH_TITLE_IN]);
        break;
    case PH_TITLE_HOLD:
        sp_blit(screen, &gd->title, 0, 0, SCR_W, SCR_H, 0, 0);
        pal_fade(&ti->pal, title_pal[TP_TITLE], 64);
        break;
    case PH_TITLE_OUT:
        sp_blit(screen, &gd->title, 0, 0, SCR_W, SCR_H, 0, 0);
        pal_fade(&ti->pal, title_pal[TP_TITLE],
                 64 - ti->t * 64 / phase_len[PH_TITLE_OUT]);
        break;
    case PH_CRED_IN:
        draw_credits(gd, screen, false);
        pal_fade(&ti->pal, title_pal[TP_CREDITS_BLANK],
                 ti->t * 64 / phase_len[PH_CRED_IN]);
        break;
    case PH_CRED_TEXT:
        draw_credits(gd, screen, false);
        pal_mix(&ti->pal, title_pal[TP_CREDITS_BLANK], title_pal[TP_CREDITS],
                ti->t * 64 / phase_len[PH_CRED_TEXT]);
        break;
    default:
        draw_credits(gd, screen, true);
        pal_fade(&ti->pal, title_pal[TP_CREDITS], 64);
        if (ti->phase > PH_CRED_HOLD) ti->done = true;
        break;
    }
    ti->t++;
}
