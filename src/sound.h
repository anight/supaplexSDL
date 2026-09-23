#ifndef SOUND_H
#define SOUND_H
#include <stdbool.h>
#include <stdint.h>

/* Effect numbers are the values the game passes to the sound driver
 * (INT 81h, AH=0, AL=effect); they index BLASTER.SND's table of VOC files. */
enum {
    SFX_EXPLODE = 0,   /* FUN_46c2_37c1  */
    SFX_INFOTRON = 1,  /* FUN_46c2_6d20  */
    SFX_PUSH = 2,      /* FUN_46c2_6d89  */
    SFX_LAND = 3,      /* FUN_46c2_6df2  */
    SFX_BUG = 4,       /* FUN_46c2_6e5b  */
    SFX_EAT = 5,       /* FUN_46c2_6ec4  */
    SFX_EXIT = 6,      /* FUN_46c2_6fd0, exit branch */
    SFX_COUNT = 7
};

/* What the mixer notices about itself: the slowest block, blocks slower than
 * real time, samples clamped to 16 bits, and blocks with an effect in them. */
typedef struct { uint32_t max_us, late, blocks, clipped, sfx_blocks; } SoundStats;

bool sound_init(const char *datadir, bool music);
void sound_stats(SoundStats *out, bool reset);
void sound_quit(void);
void sp_sound_play(int fx);        /* no-op when audio is unavailable */
bool sound_has_effects(void);      /* BLASTER.SND was found */
void sound_music(int song);        /* MUSIC_THEME / MUSIC_EXIT, or -1 to stop */

#endif
