/* The desktop's asset layer: files in a directory, read whole. */
#include "asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool sp_asset_open(const char *dir, const char *name, Asset *out)
{
    memset(out, 0, sizeof *out);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir ? dir : ".", name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (!b || (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n)) {
        free(b); fclose(f); return false;
    }
    fclose(f);
    out->data = b; out->len = (size_t)n; out->owned = true;
    return true;
}

void sp_asset_release(Asset *a)
{
    if (a->owned) free((void *)a->data);
    memset(a, 0, sizeof *a);
}
