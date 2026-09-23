#ifndef DEMO_H
#define DEMO_H
#include "sp.h"
#include "game.h"

#define DEMO_MAX 65536

typedef struct {
    Level   level;
    int     level_no;
    int     nkeys;
    uint8_t keys[DEMO_MAX];
} Demo;

bool demo_load(Demo *d, const char *path);
void demo_input(uint8_t key, Dir *dir, bool *space);

#endif
