#ifndef SOUND_H
#define SOUND_H
#include <stdbool.h>

/* Effect numbers are the values the game passes to the sound driver
 * (INT 81h, AH=0, AL=effect); they index SAMPLE.SND's table directly. */
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

bool sound_init(const char *datadir, bool music);
void sound_quit(void);
void sp_sound_play(int fx);        /* no-op when audio is unavailable */
void sound_music(int song);        /* MUSIC_THEME / MUSIC_EXIT, or -1 to stop */

#endif
