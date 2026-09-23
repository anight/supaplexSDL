/* Demo files: <1536-byte level><level number, bit7 = embedded level><RLE keys>
 * Key stream bytes are (count << 4) | key, giving count+1 frames of that key;
 * the stream ends with 0xFF.  Key codes match the original's DS:0x0631:
 *   0 none, 1 up, 2 left, 3 down, 4 right, 5..8 space+dir, 9 space alone. */
#include "demo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool demo_load(Demo *d, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < LEVEL_BYTES + 2) { fclose(f); return false; }
    uint8_t *b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return false; }
    fclose(f);

    memcpy(d->level.tiles, b, LVL_TILES);
    const uint8_t *tr = b + LVL_TILES;
    d->level.gravity          = tr[4];
    memcpy(d->level.title, tr + 6, 23); d->level.title[23] = 0;
    d->level.freeze_zonks     = tr[29];
    d->level.infotrons_needed = tr[30];
    d->level.n_special_ports  = tr[31] > 10 ? 10 : tr[31];

    d->level_no = b[LEVEL_BYTES] & 0x7f;
    d->nkeys = 0;
    for (long i = LEVEL_BYTES + 1; i < n && b[i] != 0xFF; i++) {
        int count = (b[i] >> 4) + 1, key = b[i] & 0x0f;
        for (int k = 0; k < count && d->nkeys < DEMO_MAX; k++) d->keys[d->nkeys++] = (uint8_t)key;
    }
    free(b);
    return true;
}

void demo_input(uint8_t key, Dir *dir, bool *space)
{
    if (key == 9)      { *dir = DIR_NONE; *space = true;  return; }
    if (key >= 5 && key <= 8) { *dir = (Dir)(key - 4); *space = true; return; }
    *dir = (key >= 1 && key <= 4) ? (Dir)key : DIR_NONE;
    *space = false;
}
