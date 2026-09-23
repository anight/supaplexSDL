#ifndef MUSIC_H
#define MUSIC_H
#include <stdbool.h>
#include <stdint.h>

/* The two tunes in ADLIB.SND.  The game starts song 0 as soon as the sound
 * driver is loaded, and it loops for ever; song 1 is the jingle played when
 * Murphy reaches the exit (46c2:789f). */
enum { MUSIC_THEME = 0, MUSIC_EXIT = 1 };

bool music_load(const char *datadir);   /* reads ADLIB.SND */
void music_free(void);
void music_start(int song);
void music_stop(void);
void music_tick(void);                  /* call at 50 Hz, the game's PIT rate */
bool music_playing(void);

#endif
