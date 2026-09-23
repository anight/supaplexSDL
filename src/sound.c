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
 * (0..63) written to the PC speaker's PWM counter.  They are played straight
 * out of the file's bytes, which on a microcontroller are in flash.
 *
 * The music is a port of ADLIB.SND driving an emulated OPL2; see music.c.
 * The game's PIT runs at 1193182/23864 = 50 Hz (46c2:080a) and its timer
 * interrupt calls the driver's tick (AH=1) on every one of those, so the
 * mixer steps the sequencer every out_rate/50 samples.
 *
 * The output is 16-bit stereo, both channels the same: it is all the audio
 * layer of a small SDL subset offers, and plain SDL is happy with it too.
 */
#include "sound.h"
#include "music.h"
#include "asset.h"
#include "opl/opl.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define TABLE_OFFSETS 0x8d94
#define TABLE_RATES   0x8da4
#define TIMER_HZ      (1193182.0 / 66.0)
#define MUSIC_HZ      50            /* PIT divisor 0x5d38 */
/* DOSBox gives its Adlib mixer channel a scale of 2.0; matching it makes the
 * music come out at the level the original is heard at.  The effects are
 * pulled down a little so the two together leave some headroom. */
#define MUSIC_GAIN    2
#define SFX_GAIN      500
#ifndef SP_AUDIO_RATE
#define SP_AUDIO_RATE 44100
#endif
#define CHUNK         256

typedef struct { const uint8_t *raw; int len; } Sample;

static Asset    sample_snd;
static Sample   samples[SFX_COUNT];
static int      sfx_hz = 8333;
static int      out_rate = SP_AUDIO_RATE;
static bool     opened;
static volatile int cur = -1;       /* the original plays one effect at a time */
static uint32_t sfx_pos, sfx_step;  /* 16.16 cursor into the current effect */
static int      tick_left;
static bool     have_music;
static int32_t  oplbuf[CHUNK];

static void mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int n = len / 4;                               /* stereo frames */

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
                    v += ((samples[s].raw[idx] & 0x3f) - 32) * SFX_GAIN;
                    sfx_pos += sfx_step;
                } else cur = -1;
            }
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            out[2 * i] = out[2 * i + 1] = (int16_t)v;
        }
        out += 2 * k; n -= k; tick_left -= k;
    }
}

static bool load_samples(const char *datadir)
{
    if (!sp_asset_open(datadir, "SAMPLE.SND", &sample_snd)) return false;
    const uint8_t *d = sample_snd.data;
    long n = (long)sample_snd.len;
    if (n < TABLE_RATES + SFX_COUNT) { sp_asset_release(&sample_snd); return false; }

    for (int i = 0; i < SFX_COUNT; i++) {
        int a = d[TABLE_OFFSETS + i*2] | (d[TABLE_OFFSETS + i*2 + 1] << 8);
        int b = d[TABLE_OFFSETS + i*2 + 2] | (d[TABLE_OFFSETS + i*2 + 3] << 8);
        if (a <= 0 || b <= a || b > n) continue;
        samples[i].raw = d + a;
        samples[i].len = b - a - 1;              /* last byte holds the terminator */
        if (i == 0) sfx_hz = (int)(TIMER_HZ * d[TABLE_RATES] / 256.0 + 0.5);
    }
    return true;
}

bool sound_init(const char *datadir, bool music)
{
    bool have_sfx = load_samples(datadir);
    have_music = music && music_load(datadir);
    if (!have_sfx && !have_music) return false;

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = SP_AUDIO_RATE; want.format = AUDIO_S16SYS;
    want.channels = 2;         want.samples = 1024; want.callback = mix;
    if (SDL_OpenAudio(&want, &have) != 0) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return false;
    }
    opened = true;
    out_rate = have.freq;
    sfx_step = (uint32_t)(((double)sfx_hz / out_rate) * 65536.0 + 0.5);
    tick_left = out_rate / MUSIC_HZ;
    if (have_music) opl_init(out_rate);
    SDL_PauseAudio(0);
    return true;
}

void sound_music(int song)
{
    if (!opened || !have_music) return;
    SDL_LockAudio();
    if (song < 0) music_stop(); else music_start(song);
    SDL_UnlockAudio();
}

void sp_sound_play(int fx)
{
    if (!opened || fx < 0 || fx >= SFX_COUNT || !samples[fx].raw) return;
    SDL_LockAudio();
    cur = fx; sfx_pos = 0;
    SDL_UnlockAudio();
}

void sound_quit(void)
{
    if (opened) SDL_CloseAudio();
    opened = false;
    memset(samples, 0, sizeof samples);
    sp_asset_release(&sample_snd);
    music_free();
}
