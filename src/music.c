/* The Adlib music player, ported from ADLIB.SND.
 *
 * ADLIB.SND is a flat real-mode overlay the game loads and calls through
 * INT 80h with the command in AH and the OPL base port in DX; the
 * decompilation is in re/ghidra_snd/.  Only the music side is ported here -
 * the driver's own sound effects (AH=4) are unused because picosupaplex
 * plays the digitised ones from BLASTER.SND instead.
 *
 * Five melodic OPL2 channels, each running a byte stream:
 *
 *   00..7F  play this note, then wait `len` steps
 *   80      key off,  then wait `len` steps
 *   81      hold,     then wait `len` steps
 *   82      advance to the next pattern in this channel's list, looping
 *   83      end of song
 *   84 nn   channel transpose      85 nn  global transpose
 *   86 nn   instrument             87 nn  volume (0..63)
 *   88 ww   set the pattern list to ww and enter its first pattern
 *   89 nn   tempo                  8A nn  detune, added to the F-number
 *   E0..FF  set the note length to (byte - 0xDF) steps, then keep reading
 *
 * A song is an 11 byte record at 0xF93: one tempo byte then five words, each
 * the address of a NUL-terminated list of pattern addresses.  On every 50 Hz
 * tick the tempo is added to an 8 bit accumulator and the five channels step
 * once whenever that overflows, so song 0's tempo of 43 gives 50*43/256 =
 * 8.4 steps per second.
 */
#include "music.h"
#include "opl/opl.h"
#include "asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SONGS       0x0f93      /* song table: 11 bytes per song           */
#define FNUM_LO     0x0068      /* 96 F-number low bytes, one per note     */
#define BLOCK_HI    0x00c8      /* 96 block/F-number-high bytes            */
#define INSTR_PTRS  0x0128      /* word per instrument, 11 bytes each      */
#define CHANNELS    5

/* OPL2 operator offsets for the five channels the music uses */
static const uint8_t MOD[CHANNELS] = { 0, 1, 2, 8, 9 };
static const uint8_t CAR[CHANNELS] = { 3, 4, 5, 0xb, 0xc };

typedef struct {
    uint16_t list_start, list_cur;   /* pattern list                       */
    uint16_t sp;                     /* stream pointer                     */
    uint8_t  delay, len;             /* steps left, length of a note       */
    uint8_t  instr;
    uint8_t  bnote;                  /* last value written to B0+ch        */
    int8_t   detune, transpose;
} Chan;

static Asset    snd_asset;
static const uint8_t *snd;
static size_t   snd_len;
static Chan     chan[CHANNELS];
static uint8_t  tempo, tempo_acc;
static int8_t   gtranspose;
static bool     playing;

static uint16_t rd16(uint16_t off)
{
    return (size_t)off + 1 < snd_len
         ? (uint16_t)(snd[off] | (snd[off + 1] << 8)) : 0;
}
static uint8_t rd8(uint16_t off)
{
    return (size_t)off < snd_len ? snd[off] : 0x83;   /* off the end: stop */
}

bool music_load(const char *datadir)
{
    if (!sp_asset_open(datadir, "ADLIB.SND", &snd_asset)) return false;
    if (snd_asset.len < SONGS + 22) { sp_asset_release(&snd_asset); return false; }
    snd = snd_asset.data;
    snd_len = snd_asset.len;
    return true;
}

void music_free(void)
{
    sp_asset_release(&snd_asset);
    snd = NULL; snd_len = 0; playing = false;
}

bool music_playing(void) { return playing; }

/* 46c2:0734 - silence every channel by rewriting B0+ch without the key bit */
static void all_notes_off(void)
{
    for (int c = 0; c < CHANNELS; c++) opl_write((uint8_t)(0xb0 + c), chan[c].bnote);
    opl_write(0xb7, 0);
    opl_write(0xb8, 0);
}

/* 0000:0603 - load an 11 byte instrument: five registers for the carrier,
 * five for the modulator, then feedback/connection for the channel. */
static void set_instrument(int c, uint8_t n)
{
    uint16_t p = rd16((uint16_t)(INSTR_PTRS + n * 2));
    static const uint8_t reg[5] = { 0x60, 0x80, 0x20, 0x40, 0xe0 };
    for (int i = 0; i < 5; i++)
        opl_write((uint8_t)(reg[i] + CAR[c]), rd8((uint16_t)(p + i)));
    for (int i = 0; i < 5; i++)
        opl_write((uint8_t)(reg[i] + MOD[c]), rd8((uint16_t)(p + 5 + i)));
    opl_write((uint8_t)(0xc0 + c), rd8((uint16_t)(p + 10)));
}

/* 0000:0696 - volume runs 0 (silent) to 63 (loudest) and only rewrites the
 * carrier's total level, keeping the key scale bits the instrument set. */
static void set_volume(int c, uint8_t vol)
{
    uint16_t p = rd16((uint16_t)(INSTR_PTRS + chan[c].instr * 2));
    uint8_t ksl = rd8((uint16_t)(p + 3)) & 0xc0;
    opl_write((uint8_t)(0x40 + CAR[c]), (uint8_t)(ksl | (0x3f - (vol & 0x3f))));
}

void music_stop(void)
{
    playing = false;
    all_notes_off();
}

void music_start(int song)
{
    if (!snd) return;

    /* 0000:041c, done once by the original but harmless to repeat */
    opl_write(0x01, 0); opl_write(0x04, 0); opl_write(0x08, 0);
    opl_write(0xbd, 0x40);
    all_notes_off();

    uint16_t base = (uint16_t)(SONGS + song * 11);
    memset(chan, 0, sizeof chan);
    playing = false;
    gtranspose = 0;
    tempo = rd8(base);
    tempo_acc = 0xff;
    for (int c = 0; c < CHANNELS; c++) {
        uint16_t list = rd16((uint16_t)(base + 1 + c * 2));
        chan[c].list_start = chan[c].list_cur = list;
        chan[c].sp    = rd16(list);
        chan[c].delay = 1;
    }
    playing = true;
}

/* 0000:06bd - advance one channel by a single sequencer step */
static void step(int c)
{
    Chan *ch = &chan[c];
    if (--ch->delay != 0) return;

    for (;;) {
        uint8_t b = rd8(ch->sp++);

        if (b >= 0xe0) {                       /* note length */
            ch->len = (uint8_t)(b + 0x21);     /* 0xE0 -> 1 ... 0xFF -> 32 */
            continue;
        }
        if (b < 0x80) {                        /* play a note */
            uint8_t note = (uint8_t)(b + gtranspose + ch->transpose);
            opl_write((uint8_t)(0xb0 + c), ch->bnote);      /* key off first */
            opl_write((uint8_t)(0xa0 + c),
                      (uint8_t)(rd8((uint16_t)(FNUM_LO + note)) + ch->detune));
            ch->bnote = rd8((uint16_t)(BLOCK_HI + note));
            opl_write((uint8_t)(0xb0 + c), (uint8_t)(ch->bnote | 0x20));
            ch->delay = ch->len;
            return;
        }
        switch (b) {
        case 0x80:                             /* key off */
            opl_write((uint8_t)(0xb0 + c), ch->bnote);
            ch->delay = ch->len;
            return;
        case 0x81:                             /* hold the note sounding */
            ch->delay = ch->len;
            return;
        case 0x82: {                           /* next pattern, looping */
            uint16_t s = (uint16_t)(ch->list_cur + 2);
            if (rd16(s) == 0) s = ch->list_start;
            ch->list_cur = s;
            ch->sp = rd16(s);
            continue;
        }
        case 0x83:                             /* end of song */
            music_stop();
            return;
        case 0x84: ch->transpose = (int8_t)rd8(ch->sp++); continue;
        case 0x85: gtranspose    = (int8_t)rd8(ch->sp++); continue;
        case 0x86: ch->instr = rd8(ch->sp++); set_instrument(c, ch->instr); continue;
        case 0x87: set_volume(c, (uint8_t)(rd8(ch->sp++) & 0x3f)); continue;
        case 0x88: {                           /* set the pattern list */
            uint16_t s = rd16(ch->sp);
            ch->list_start = ch->list_cur = s;
            ch->sp = rd16(s);
            continue;
        }
        case 0x89: tempo = rd8(ch->sp++); continue;
        case 0x8a: ch->detune = (int8_t)rd8(ch->sp++); continue;
        default:                               /* unreachable in both songs */
            music_stop();
            return;
        }
    }
}

void music_tick(void)
{
    if (!playing || !snd) return;
    unsigned sum = (unsigned)tempo_acc + tempo;
    tempo_acc = (uint8_t)sum;
    if (sum < 0x100) return;                       /* no 8 bit carry, no step */
    for (int c = 0; c < CHANNELS && playing; c++) step(c);
}
