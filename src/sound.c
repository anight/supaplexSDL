/* Audio: the digitised effects from BLASTER.SND and the Adlib music from
 * ADLIB.SND, mixed into one SDL stream.
 *
 * The shipped SUPAPLEX.CFG selects the Sound Blaster, and the game then plays
 * its effects from BLASTER.SND: Creative's CT-VOICE driver with seven
 * Creative Voice Files appended, each one block of 8-bit unsigned PCM at
 * 8333 Hz.  They are played straight out of the file's bytes, which on a
 * microcontroller are in flash, under the original's priority rules.
 *
 * The music is a port of ADLIB.SND driving an emulated OPL2; see music.c.
 * The game's PIT runs at 1193182/23864 = 50 Hz (46c2:080a) and its timer
 * interrupt calls the driver's tick (AH=1) on every one of those, so the
 * mixer steps the sequencer every out_rate/50 samples.  The same interrupt
 * counts down how long an effect holds its priority.
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

#define VOC_TABLE     0x8fa8        /* BLASTER.SND: offsets of the 7 VOC files */
#define TIMER_HZ      50            /* PIT divisor 0x5d38 */
/* DOSBox gives its Adlib mixer channel a scale of 2.0 and the Sound
 * Blaster's 8-bit DAC a shift of 8, which leaves no room for the two at
 * once.  These keep their balance - effects on top of the music - at about
 * three quarters of that, so they only clip when both peak together. */
#define MUSIC_GAIN_Q2 6             /* x1.5 */
#define SFX_GAIN      128           /* per step of an 8-bit sample */
#ifndef SP_AUDIO_RATE
#define SP_AUDIO_RATE 44100
#endif
#define CHUNK         256

typedef struct {
    const uint8_t *pcm;             /* 8-bit unsigned, centred on 128 */
    int            len;
    int            rate;
    uint32_t       step;            /* 16.16 source samples per output frame */
} Sample;

/* 46c2:6cb7..6ec4 and 6f2d: an effect starts only if the one playing has a
 * lower priority than its gate; it then holds the priority for `hold` ticks
 * of the 50 Hz timer, which the interrupt at 46c2:0782 counts down. */
static const struct { uint8_t gate, prio, hold; } RULE[SFX_COUNT] = {
    [SFX_EXPLODE]  = { 5,   5,  15 },
    [SFX_INFOTRON] = { 5,   4,  15 },
    [SFX_PUSH]     = { 2,   2,   7 },
    [SFX_LAND]     = { 2,   2,   7 },
    [SFX_BUG]      = { 3,   3,   3 },
    [SFX_EAT]      = { 1,   1,   3 },
    [SFX_EXIT]     = { 255, 10, 250 },
};

static Asset    blaster;
static Sample   samples[SFX_COUNT];
static int      out_rate = SP_AUDIO_RATE;
static bool     opened, have_music;
static int      tick_left;          /* output frames until the next timer tick */

/* the effect playing: the original plays one at a time */
static volatile int cur = -1;
static uint32_t sfx_pos;            /* 16.16 cursor into it                   */
static uint8_t  sfx_prio, sfx_hold; /* DS:9579 and DS:957b                    */

static int32_t  oplbuf[CHUNK];
static volatile SoundStats stats;

/* The game's 50 Hz timer: the music's tick and the effect priority's. */
static void timer_tick(void)
{
    if (have_music) music_tick();
    if (sfx_hold && --sfx_hold == 0) sfx_prio = 0;
}

static void mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;                          /* stereo frames */
    Uint64 t0 = SDL_GetPerformanceCounter();

    for (int n = frames; n > 0; ) {
        if (tick_left <= 0) {
            timer_tick();
            tick_left = out_rate / TIMER_HZ;
        }
        int k = n < CHUNK ? n : CHUNK;
        if (k > tick_left) k = tick_left;

        if (have_music) {
            memset(oplbuf, 0, (size_t)k * sizeof *oplbuf);
            opl_render(oplbuf, k);
        }
        for (int i = 0; i < k; i++) {
            int32_t v = have_music ? (oplbuf[i] * MUSIC_GAIN_Q2) >> 2 : 0;
            int s = cur;
            if (s >= 0) {
                const Sample *sm = &samples[s];
                uint32_t idx = sfx_pos >> 16;
                if ((int)idx < sm->len) {
                    v += ((int)sm->pcm[idx] - 128) * SFX_GAIN;
                    sfx_pos += sm->step;
                } else cur = -1;
            }
            if (v > 32767 || v < -32768) {
                stats.clipped++;
                v = v > 32767 ? 32767 : -32768;
            }
            out[2 * i] = out[2 * i + 1] = (int16_t)v;
        }
        out += 2 * k; n -= k; tick_left -= k;
    }

    /* how long the block took against how long it plays for */
    Uint64 us = (SDL_GetPerformanceCounter() - t0) * 1000000u / SDL_GetPerformanceFrequency();
    Uint64 budget = (Uint64)frames * 1000000u / (Uint64)out_rate;
    if (us > stats.max_us) stats.max_us = (uint32_t)us;
    if (us > budget) stats.late++;
    stats.blocks++;
    if (cur >= 0) stats.sfx_blocks++;
}

/* The word table at VOC_TABLE gives each Creative Voice File's offset; the
 * play routine skips the file's header by its own size field at +0x14 and
 * hands the first block to the driver.  That block is type 1: a 3-byte
 * length, a time constant (rate = 1000000 / (256 - tc)), a codec byte (0 for
 * 8-bit unsigned PCM), then the samples. */
static bool load_effects(const char *datadir)
{
    if (!sp_asset_open(datadir, "BLASTER.SND", &blaster)) return false;
    const uint8_t *d = blaster.data;
    size_t n = blaster.len;
    int found = 0;
    for (int i = 0; n > VOC_TABLE + 2 * SFX_COUNT && i < SFX_COUNT; i++) {
        size_t o = d[VOC_TABLE + 2*i] | (d[VOC_TABLE + 2*i + 1] << 8);
        if (o + 0x1a > n || memcmp(d + o, "Creative Voice File", 19) != 0) continue;
        size_t p = o + (d[o + 0x14] | (d[o + 0x15] << 8));
        if (p + 6 > n || d[p] != 1 || d[p + 5] != 0) continue;
        size_t blen = d[p+1] | (d[p+2] << 8) | ((size_t)d[p+3] << 16);
        if (blen < 2 || p + 4 + blen > n) continue;
        samples[i].pcm  = d + p + 6;
        samples[i].len  = (int)(blen - 2);
        samples[i].rate = 1000000 / (256 - d[p + 4]);
        found++;
    }
    if (!found) sp_asset_release(&blaster);
    return found > 0;
}

bool sound_init(const char *datadir, bool music)
{
    bool have_sfx = load_effects(datadir);
    have_music = music && music_load(datadir);
    if (!have_sfx && !have_music) return false;

    /* The chip is built before the device opens: once it is open the mixer
     * may run at any moment, on another core, and must not find it half set up. */
    if (have_music) opl_init(SP_AUDIO_RATE);

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
    if (have_music && out_rate != SP_AUDIO_RATE) opl_init(out_rate);  /* not yet running */
    for (int i = 0; i < SFX_COUNT; i++)
        samples[i].step = (uint32_t)(((uint64_t)samples[i].rate << 16) / (uint64_t)out_rate);
    tick_left = out_rate / TIMER_HZ;
    SDL_PauseAudio(0);
    return true;
}

void sound_quit(void)
{
    if (opened) SDL_CloseAudio();
    opened = false;
    memset(samples, 0, sizeof samples);
    sp_asset_release(&blaster);
    music_free();
}

void sound_music(int song)
{
    if (!opened || !have_music) return;
    SDL_LockAudio();
    if (song < 0) music_stop(); else music_start(song);
    SDL_UnlockAudio();
}

bool sound_has_effects(void) { return opened && samples[SFX_EXIT].pcm != NULL; }

void sp_sound_play(int fx)
{
    if (!opened || fx < 0 || fx >= SFX_COUNT || !samples[fx].pcm) return;
    SDL_LockAudio();
    if (sfx_prio < RULE[fx].gate) {
        sfx_prio = RULE[fx].prio;
        sfx_hold = RULE[fx].hold;
        cur = fx; sfx_pos = 0;
        /* 46c2:6f2d: reaching the exit silences the music (6c8e, AH=2) and
         * plays the digitised fanfare in its place */
        if (fx == SFX_EXIT && have_music) music_stop();
    }
    SDL_UnlockAudio();
}

void sound_stats(SoundStats *out, bool reset)
{
    SDL_LockAudio();
    *out = *(const SoundStats *)&stats;
    if (reset) memset((void *)&stats, 0, sizeof stats);
    SDL_UnlockAudio();
}
