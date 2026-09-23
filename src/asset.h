/* Where the game's data comes from.
 *
 * On the desktop an asset is a file read into memory.  On a microcontroller
 * it is a table compiled into flash, and an image may already be decoded to
 * one byte a pixel so it can be blitted where it lies.  The game asks by the
 * original file name and does not care which it got.
 */
#ifndef ASSET_H
#define ASSET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t         len;
    int            w, h;      /* nonzero: `data` is w*h bytes, one index a pixel */
    bool           owned;     /* data is heap memory to give back on release   */
} Asset;

/* `dir` is where the files are on a desktop and ignored elsewhere.
 * Returns false, quietly, if there is no such asset. */
bool sp_asset_open(const char *dir, const char *name, Asset *out);
void sp_asset_release(Asset *a);

#endif
