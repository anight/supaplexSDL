/* Audio: the digitised effects from SAMPLE.SND and the Adlib music from
 * ADLIB.SND, mixed into one SDL stream.
 *
 * SAMPLE.SND is a loadable 8086 driver blob with the sample data appended.
 * Its INT handler dispatches on AH through a table at offset 0x18; AH=0 plays
 * effect AL.  That routine reads
 *     start = word[0x8d94 + AL*2],  end = word[0x8d96 + AL*2]
 *     rate  = byte[0x8da4 + AL]
 * writes a 0xFF terminator at end-1, and patches `add [0x15e], imm8` at 0x10c
 * with the rate.  The timer runs at 1193182/66 = 18078.5 Hz and the sample
 * pointer advances whenever that addition carries, so the real sample rate is
 * 18078.5 * rate/256 = 8333 Hz for every effect.  Samples are 6-bit unsigned
 * (0..63) written to the PC speaker's PWM counter.
 *
 * The music is a port of ADLIB.SND driving an emulated OPL2; see music.c.
 * The game's PIT runs at 1193182/23864 = 50 Hz (46c2:080a) and its timer
 * interrupt calls the driver's tick (AH=1) on every one of those, so the
 * mixer steps the sequencer every out_rate/50 samples.
 */
#include "sound.h"
#include "music.h"
#include "opl/opl.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_OFFSETS 0x8d94
#define TABLE_RATES   0x8da4
#define TIMER_HZ      (1193182.0 / 66.0)
#define MUSIC_HZ      50            /* PIT divisor 0x5d38 */
/* DOSBox gives its Adlib mixer channel a scale of 2.0; matching it makes the
 * music come out at the level the original is heard at.  The effects are
 * pulled down a little so the two together leave some headroom. */
#define MUSIC_GAIN    2
#define OUT_RATE      44100
#define CHUNK         512

typedef struct { int16_t *pcm; int len; } Sample;

static Sample  samples[SFX_COUNT];
static int     sfx_hz = 8333;
static int     out_rate = OUT_RATE;
static SDL_AudioDeviceID dev;
static volatile int cur = -1;       /* the original plays one effect at a time */
static uint32_t sfx_pos, sfx_step;  /* 16.16 cursor into the current effect */
static int      tick_left;
static bool     have_music;
static int32_t  oplbuf[CHUNK];

static void mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int n = len / 2;

    while (n > 0) {
        if (have_music && tick_left <= 0) {
            music_tick();
            tick_left = out_rate / MUSIC_HZ;
        }
        int k = n;
        if (k > CHUNK) k = CHUNK;
        if (have_music && k > tick_left) k = tick_left;

        if (have_music) {
            memset(oplbuf, 0, (size_t)k * sizeof *oplbuf);
            opl_render(oplbuf, k);
        }
        for (int i = 0; i < k; i++) {
            int32_t v = have_music ? oplbuf[i] * MUSIC_GAIN : 0;
            int s = cur;
            if (s >= 0) {
                uint32_t idx = sfx_pos >> 16;
                if ((int)idx < samples[s].len) {
                    v += samples[s].pcm[idx];
                    sfx_pos += sfx_step;
                } else cur = -1;
            }
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            out[i] = (int16_t)v;
        }
        out += k; n -= k; tick_left -= k;
    }
}

static bool load_samples(const char *datadir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/SAMPLE.SND", datadir);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return false; }
    fclose(f);
    if (n < TABLE_RATES + SFX_COUNT) { free(d); return false; }

    for (int i = 0; i < SFX_COUNT; i++) {
        int a = d[TABLE_OFFSETS + i*2] | (d[TABLE_OFFSETS + i*2 + 1] << 8);
        int b = d[TABLE_OFFSETS + i*2 + 2] | (d[TABLE_OFFSETS + i*2 + 3] << 8);
        if (a <= 0 || b <= a || b > n) continue;
        int len = b - a - 1;                     /* last byte holds the terminator */
        samples[i].pcm = malloc((size_t)len * sizeof(int16_t));
        samples[i].len = len;
        for (int k = 0; k < len; k++) {
            int v = d[a + k] & 0x3f;             /* 6-bit unsigned */
            samples[i].pcm[k] = (int16_t)((v - 32) * 500);
        }
        if (i == 0) sfx_hz = (int)(TIMER_HZ * d[TABLE_RATES] / 256.0 + 0.5);
    }
    free(d);
    return true;
}

bool sound_init(const char *datadir, bool music)
{
    bool have_sfx = load_samples(datadir);
    have_music = music && music_load(datadir);
    if (!have_sfx && !have_music) return false;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = OUT_RATE; want.format = AUDIO_S16SYS;
    want.channels = 1;    want.samples = 1024; want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return false; }
    out_rate = have.freq;
    sfx_step = (uint32_t)(((double)sfx_hz / out_rate) * 65536.0 + 0.5);
    tick_left = out_rate / MUSIC_HZ;
    if (have_music) opl_init(out_rate);
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void sound_music(int song)
{
    if (!dev || !have_music) return;
    SDL_LockAudioDevice(dev);
    if (song < 0) music_stop(); else music_start(song);
    SDL_UnlockAudioDevice(dev);
}

void sp_sound_play(int fx)
{
    if (!dev || fx < 0 || fx >= SFX_COUNT || !samples[fx].pcm) return;
    SDL_LockAudioDevice(dev);
    cur = fx; sfx_pos = 0;
    SDL_UnlockAudioDevice(dev);
}

void sound_quit(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    for (int i = 0; i < SFX_COUNT; i++) { free(samples[i].pcm); samples[i].pcm = NULL; }
    music_free();
}
