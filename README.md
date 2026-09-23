# supaplexSDL

A from-scratch C / SDL2 reimplementation of **Supaplex** (Dream Factory /
Digital Integration, 1991), reverse-engineered from the original DOS binary and
its data files. Pure software rendering, no GPU required.

## You must bring your own game files

**This repository contains no game data.** Supaplex's graphics, levels, sound
and music are copyrighted and are not included. To use this you need your own
copy of the original DOS game (the SPFIX 6.2 distribution), and you must copy
its data files into a directory — `orig/` by default:

```
orig/
  FIXED.DAT  MOVING.DAT  PANEL.DAT  PALETTES.DAT  LEVELS.DAT  LEVEL.LST
  TITLE.DAT  TITLE1.DAT  TITLE2.DAT MENU.DAT  BACK.DAT  GFX.DAT  CONTROLS.DAT
  CHARS6.DAT CHARS8.DAT  BLASTER.SND SAMPLE.SND  ADLIB.SND  SUPAPLEX.CFG
  DEMO0.BIN ... DEMO9.BIN            (only for --replay)
```

Without them the program builds but has nothing to load and exits.

## Building

Needs a C11 compiler, a C++11 compiler (for the OPL emulator) and SDL2
development headers (`libsdl2-dev` on Debian/Ubuntu).

```bash
make
./picosupaplex -d orig          # or: ./picosupaplex -d orig -l 3
```

## Controls

It opens with the original's start screens — the `TITLE.DAT` artwork fading
up, then the credits — and then a level-select screen drawn on the original
menu artwork. Any key skips a start screen; `Esc` skips straight to the menu.

| Key | Action |
|-----|--------|
| Arrows / PgUp / PgDn / Home / End | Choose a level |
| Enter | Play the highlighted level |
| Arrow keys | Move Murphy |
| Space + arrow | "Eat" in that direction without moving |
| `R` | Restart the level |
| `F2` / `F3` | Previous / next level |
| `Esc` | Back to the level list, or quit from it |

A game controller works too: stick or D-pad to move and to choose, A to snap
(and to confirm), B or Start to leave a level, Back to restart it.

## Other platforms

The same source builds against [picosdl](https://github.com/anight/picosdl),
a subset of SDL2 for the Raspberry Pi Pico: see
[picosupaplex](https://github.com/anight/picosupaplex).  What differs is kept
behind three seams — the asset layer (`asset.h`, files here and flash tables
there), the screen (`video.c`) and the loop's entry (`app.c`, which `main.c`
calls after the command line) — so there is no separate branch of the game.

## Command line

```
picosupaplex [-d datadir] [-l level] [-s scale] [-p player] [--no-music]
picosupaplex --replay DEMO0.BIN      # headless demo playback (physics test)
picosupaplex -l 3 --drive "3x40,1x20" # headless scripted input trace
```

`-d` points at the directory holding `LEVELS.DAT`, `MOVING.DAT` etc.
(default `orig`). `-s` is the integer window scale (default 3). Passing `-l`
starts that level immediately, the way the original's `/x` option does;
without it you get the start screens and the level-select screen. Music follows
`SUPAPLEX.CFG` unless `--music` or `--no-music` overrides it.

## Licence

`src/opl/dbopl.{cpp,h}` is DOSBox's OPL emulator, GPLv2-or-later, so the built
binary is covered by the GPL. Everything else here is original work.

---

# Reverse-engineering notes

Everything below was recovered from the binary and verified pixel-exact against
the original running under DOSBox. The reverse-engineering workspace — Ghidra
output, the DOSBox comparison harness and the tools under `re/` that these notes
mention — is not part of this repository; only the game's source is.

## The binary

The distribution has no `SUPAPLEX.EXE`: `SUPAPLEX.BAT` runs **`SPFIX62.EXE`**,
Herman Perk's "Speed Fix" build, which *is* the full game with timing fixes.

`SPFIX62.EXE` is **EXEPACK-compressed** (signature `RB` at `cs:000e`, with a
backwards `MOVSB` relocation stub). `re/tools/unexepack.py` unpacks it:

```
packed image 49758 bytes -> 350640 bytes
real cs:ip = 36c2:0000, ss:sp = 555b:0400
```

The unpacked executable runs identically under DOSBox, which verifies the
unpacker. Ghidra only found 41 functions in the packed file and 223 in the
unpacked one once `DS = 0x5024` was pinned and the tile jump table was fed back
in as entry points (`re/tools/`-adjacent Ghidra scripts in `~/sp-tools/scripts`).

Memory layout of the unpacked image (Ghidra addresses, image base `0x1000`):

| Range | Contents |
|-------|----------|
| `1000:0000`–`46c2:0000` | graphics buffers (~224 KB) |
| `46c2:0000`–`5024:0000` | code (38 KB) |
| `5024:0000`–… | data segment (`DS`) |

## Graphics format

All `.DAT` images are **4-plane EGA-style planar**, one row at a time:

```
row = [plane0: w/8 bytes][plane1][plane2][plane3]     (stride = w/2 bytes)
plane p supplies bit p of the 4-bit colour index; MSB = leftmost pixel
```

| File | Size | Contents |
|------|------|----------|
| `FIXED.DAT` | 640×16 | the **40 static tiles**, indexed directly by tile id |
| `MOVING.DAT` | 320×462 | animation frames; level border frame at (304,388)/(304,396) |
| `PANEL.DAT` | 320×24 | status panel background |
| `TITLE.DAT`, `TITLE1`, `TITLE2`, `MENU`, `BACK`, `GFX`, `CONTROLS` | 320×200 | full screens |
| `CHARS6.DAT`, `CHARS8.DAT` | 512 B | 64-glyph 8×8 1bpp fonts, stored as a 512×8 strip; glyph = char − 0x20 |

`PALETTES.DAT` is 4 palettes × 16 colours × 4 bytes `(R,G,B,unused)`, 4 bits per
channel. The VGA DAC gets `value*4`, so the 8-bit colour is `(n<<4)|(n>>2)` —
this reproduces the original's framebuffer exactly. Palette 1 is the in-game
palette; a separate title/menu palette lives in the EXE at file offset `0xa98d`,
and the EXE's own copy of `PALETTES.DAT` is at `0xaa0c`.

## Level format

`LEVELS.DAT` is 111 levels of 1536 bytes: 60×24 tile bytes followed by a
96-byte trailer:

| Offset | Field |
|--------|-------|
| 0..3 | unused |
| 4 | initial gravitation |
| 5 | speed-fix marker (0x20) |
| 6..28 | 23-char level title |
| 29 | initial "freeze zonks" |
| 30 | infotrons needed (0 = all of them) |
| 31 | number of special ports |
| 32..91 | 10 × 6-byte special-port records |

`LEVEL.LST` holds 111 fixed 28-byte display records.

Tile ids 0..39 index `FIXED.DAT` directly:

```
 0 space      1 zonk       2 base        3 murphy     4 infotron
 5 ram chip   6 hardware   7 exit        8 orange disk
 9-12 ports R/D/L/U       13-16 special ports R/D/L/U
17 snik snak 18 yellow disk 19 terminal  20 red disk
21-23 ports vert/horiz/4-way            24 electron   25 bug
26/27 chip left/right      28-30, 32-37 hardware variants
31 EXPLOSION (runtime only) 38/39 chip top/bottom
```

## Start screens

The boot sequence lives in `src/title.c`. Its three palettes are not in
`PALETTES.DAT` — they sit in the executable at `DS:5f15`, four 64-byte
palettes back to back, laid out exactly like `PALETTES.DAT` (four bytes per
entry, R, G, B and an unused index byte, 4 bits per channel).
`re/tools/genpal.py` emits them as `src/titlepal.h`:

| Offset | Contents |
|--------|----------|
| `5f15` | `TITLE.DAT` |
| `5f55` | `TITLE2.DAT`, credits legible |
| `5f95` | the same with entries 2, 8 and 12 forced to grey `(8,8,8)` |
| `5fd5` | a blue ramp, unused by this sequence |

The sequence, measured off a 20 fps DOSBox capture:

1. `TITLE.DAT` fades up over about 0.9 s, holds, and fades back down.
2. `TITLE2.DAT` — which already has the Dream Factory credits drawn into the
   artwork — fades up in the `5f95` palette. The credits are invisible
   because their colour index is the same grey as the panel behind them.
3. That palette morphs into `5f55` over about 1 s, so the credits darken into
   view: index 8 runs from grey `(8,8,8)` to dark blue `(1,3,7)`.
4. The two speed-fix version lines are drawn once the morph finishes.

Every fade is the original's: the VGA DAC receives `(nibble * 4 * k) >> 6` for
`k` running 0 to 64, a plain linear intensity ramp.

Three lines of text are drawn over `TITLE2.DAT`, all in `CHARS6` — whose
glyphs live in 8-pixel cells but are only 5 pixels wide, so the pen advances
**6** pixels and the cells overlap. `sp_text_adv()` takes the advance.

| String (EXE offset) | Position | Colour |
|---------------------|----------|--------|
| `SUPAPLEX SPEED FIX VERSION 6.2` (`49885`) | 72, 11 | 1, white |
| `VERSIONS 1-4 + 6.X BY HERMAN PERK` (`49862`) | 64, 170 | 2, grey |
| `VERSIONS 5.X BY ELMER PRODUCTIONS` (`49af2`) | 64, 180 | 2, grey |

The finished credits screen is a pixel-exact match for the original's; the
title screen matches exactly in palette-index space, and its settled palette
is exactly `nibble * 4` in DAC units. One deliberate difference: the original
holds `TITLE.DAT` only for as long as it takes to load the data files, which
under DOSBox is no time at all, so picosupaplex holds it for a second.

## Rendering

The original renders the **whole 960×384 level** into VGA memory using write
mode 1 (latch copies), 122 bytes per row, and scrolls by moving the display
start address — which is why the SpeedFix author found "no scroll routine".
The cell → buffer offset table is at `DS:0x6155`; the playfield starts at buffer
offset 18788 with 1952-byte rows.

Two details that are easy to get wrong, both verified against DOSBox:

* The viewport is centred on Murphy but clamped to **x ∈ [8, 632], y ∈ [8, 200]**
  — the outermost 8 pixels of the level are never shown.
* The level's outer ring of tiles is **not drawn from the level data**; a fixed
  metal frame from `MOVING.DAT` is drawn instead.

Panel text is drawn opaquely (glyph colour over black) with `CHARS8`:
player name at x=72, time at x=160/184/208 (row y=179); level number at x=16,
title at x=64, red disks at x=248, infotrons remaining at x=272 (row y=190).

## Simulation

The playfield is **one 16-bit word per cell** at `DS:0x1834` - low byte = tile
id, high byte = per-object state. Neighbours are `+/-2` bytes horizontally and
`+/-0x78` (120) vertically. Reserved words `0x8888 / 0x9999 / 0xAAAA / 0xFFFF`
mark cells an object is moving into or out of; they are **not** swept each
frame, they persist until the object that made them consumes them.

### Level load

`FUN_46c2_3519` rewrites the level bytes into physics tiles before play starts,
and this is easy to miss because the screen still shows the originals:

| level tile | becomes |
|---|---|
| 26/27 chip left+right, 38/39 chip top+bottom | 5 (RAM chip) |
| 28..37 hardware variants | 6 (plain hardware) |
| 13..16 special ports | 9..12 with state 1 |
| 17 snik snak, 24 electron | given a starting orientation from the free neighbour |

Without this a zonk will never roll off a chip, which silently breaks most
levels. picosupaplex keeps a separate `look[]` array so the decorations still
render while the simulation sees the folded tiles.

### Frame

1. run the Murphy handler (`FUN_46c2_6fd0`);
2. scan cells 61..1378, looking each tile up in the jump table at `DS:0x160a`
   and queueing `(cell, handler)` pairs;
3. call each queued handler with `SI` = cell offset.

Only **eight** tiles have handlers:

| Tile | Handler | |
|------|---------|---|
| 1 zonk | `46c2:1360` | fall / roll |
| 4 infotron | `46c2:17d8` | same shape, several deliberate differences |
| 8 orange disk | `46c2:362b` | |
| 17 snik snak | `46c2:8610` | six routines via a 48-entry table at `DS:0x154a` |
| 19 terminal | `46c2:3305` | |
| 24 electron | `46c2:8acf` | same six routines, table at `DS:0x15aa` |
| 25 bug | `46c2:3270` | wakes for 14 steps, then sleeps a random 0x20..0x5f |
| 31 explosion | `46c2:36e5` | states 0..7 decay to space; 0x80..0x88 to an infotron |

### Object states

```
0x00        at rest
0x40, 0x41  about to fall
0x10 + n    falling, n = 0..7, two pixels per frame
0x50/0x60   first two frames of rolling left / right
0x20 + n    rolling left,  continued after relocating one cell
0x30 + n    rolling right, continued after relocating one cell
0x70        landed on nothing: carry on falling
```

Three details that each cost a frame if you get them wrong:

* After setting 0x40/0x50/0x60 the original **does not return** - it falls
  straight into its own state machine, so the first animation frame happens in
  the same frame the object starts moving.
* Part-way through its animation an object calls a *vacate* routine on the cell
  it came out of (`FUN_46c2_1cac` for zonks, `FUN_46c2_1d27` for infotrons):
  at fall state 0x16, and at roll states 0x26 / 0x36. This clears the cell and
  lets the neighbours above start moving.
* Murphy's own vacate (`FUN_46c2_1bce`) additionally wakes a zonk or infotron
  **directly** above (state 0x40). The object variants never do - they require
  that cell to be free - and they only look for their own kind diagonally. A
  diagonal roll reserves the cell *above* the vacated one, not the vacated cell.

After a roll finishes, a zonk drops into the cell below immediately while an
infotron parks in state 0x70 and falls on the next frame.

### Murphy

Direction codes match `DS:0x0631`: `1 up, 2 left, 3 down, 4 right`, `+4` with
space held, `9` space alone. Each direction has its own ordered table of
targets (`46c2:71ea` up, `724c` left, `72c6` down, `7328` right). Note which
tests are on the **whole word** - so an object that is falling or rolling
blocks Murphy - and which are on the tile byte alone:

```
up    : AX==0, AX==2, AL==0x19, AX==4, AX==7, AL==0x13,
        AL==0x0c/0x15/0x17, AL==0x14, AL==0x12
left  : ... plus AX==1 (zonk), AL==0x0b/0x16/0x17, AX==0x14, AX==0x12, AX==8
down  : as up, ports AL==0x0a/0x15/0x17
right : as left, ports AL==0x09/0x16/0x17, AL==0x14, AL==0x12
```

Murphy's **action code lives in the state byte of his own cell**, and other
objects read it: a zonk landing on him is harmless while he is leaning into a
push (0x0e, 0x0f, 0x25, 0x26, 0x28, 0x29), and an enemy cannot touch him while
he is inside a port (0x18..0x1b). Codes are 1..4 walking into space, 5..8 when
a base is eaten, 9..12 collecting an infotron, 0x10..0x13 eating on the spot,
0x14..0x17 eating an infotron on the spot, 0x18..0x1b port transit,
0x1c..0x1f taking a red disk, 0x20..0x23 taking one on the spot,
0x24..0x27 pushing a yellow disk, 0x28/0x29 an orange disk, 0x2a dropping a
red disk.

Durations come straight from the animation frame lists:

* every action is **8 frames** (two pixels each) **except eating an infotron on
  the spot, which is 7** (`DS:0x1236`);
* pushing sets `DAT_5024_0dde = 8`, and the animation does not advance until it
  has counted down, so a push costs **16 frames**: eight leaning, eight moving;
* dropping a red disk sets the same counter to **0x40**.

### Enemies

Snik snaks and electrons run the same six routines, selected by state:

```
0x00..0x07  turning on the spot, anticlockwise: 0 up, 2 left, 4 down, 6 right
0x08..0x0f  turning on the spot, clockwise:     8 up, 10 right, 12 down, 14 left
0x10..0x17 moving up    0x18..0x1f moving left
0x20..0x27 moving down  0x28..0x2f moving right
```

They turn one step every fourth frame and decide whether to move on the fourth.
While moving, the cell behind holds a marker tile `0xBB`. They are left-hand
wall followers: after a step they try left, then straight on, then right.

### Randomness

`seed = seed*0x5E5 + 0x31`, returning `seed>>1` (`FUN_46c2_33e1`), seeded from
the BIOS timer - so bug blink and terminal flicker are deliberately not
reproducible, and no demo can depend on them.

## Sound

`SAMPLE.SND` (and its siblings) are loadable 8086 driver blobs with the sample
data appended, which the game calls through `INT 80h` / `INT 81h`. The sample
driver's handler dispatches on `AH` through a table at offset 0x18; `AH=0`
plays effect `AL`, reading

```
start = word[0x8d94 + AL*2]      end = word[0x8d96 + AL*2]
rate  = byte[0x8da4 + AL]
```

writing a 0xFF terminator at `end-1` and patching the immediate of
`add byte [0x15e], imm8` at offset 0x10c with the rate. The timer runs at
1193182/66 = 18078.5 Hz and the sample pointer advances only when that addition
carries, so the real rate is `18078.5 * rate/256` = **8333 Hz** for every
effect. Samples are 6-bit unsigned, written to the PC speaker's PWM counter.

That yields seven effects, and the call sites in the game name them:

| # | length | event | called from |
|---|--------|-------|-------------|
| 0 | 1.33 s | explosion | `FUN_46c2_37c1` |
| 1 | 0.47 s | infotron collected | `FUN_46c2_6d20` |
| 2 | 0.22 s | push completes | `FUN_46c2_6d89` |
| 3 | 0.25 s | object lands | `FUN_46c2_6df2` |
| 4 | 0.23 s | bug blinking near Murphy | `FUN_46c2_6e5b` |
| 5 | 0.01 s | base eaten | `FUN_46c2_6ec4` |
| 6 | 1.80 s | exit reached | `FUN_46c2_6f2d` |

picosupaplex decodes them at load time and plays them through SDL2 audio, one
at a time as the original does. `re/out/sfx/` holds them as WAV files.

### Which set is played, and when

The shipped `SUPAPLEX.CFG` selects the Sound Blaster, and with it the game
plays its effects from `BLASTER.SND`, not `SAMPLE.SND`. That file is Creative's
CT-VOICE driver with seven Creative Voice Files appended; a word table at
`0x8fa8` holds their offsets, and the play routine skips each file's header by
its own header-size field at `+0x14`. Each is one type-1 block of **8-bit
unsigned PCM at 8333 Hz** — the same seven sounds, cleanly: centred on 128,
where the speaker set sits on a large DC offset (its values run 0–60, mean
about 34) that clicks at every start and stop, and is otherwise a PWM duty
approximation made for a paper cone to smooth. Played through a DAC the
speaker set is harsh, so supaplexSDL uses the Blaster set and falls back to
the speaker one only if `BLASTER.SND` is missing.

An effect does not simply cut off whatever is playing. Every trigger
(46c2:6cb7…6ec4) checks a priority byte at `DS:9579` against a gate, and if it
passes sets the priority and a hold time at `DS:957b`, which the 50 Hz timer
interrupt counts down (46c2:0782) before clearing the priority:

| effect | plays if priority below | sets priority | holds (50 Hz ticks) |
|---|---|---|---|
| explosion | 5 | 5 | 15 |
| infotron | 5 | 4 | 15 |
| push, land | 2 | 2 | 7 |
| bug | 3 | 3 | 3 |
| base eaten | 1 | 1 | 3 |
| exit | always | 10 | 250 |

Reaching the exit (46c2:6f2d) also stops the music and plays effect 6 in its
place. The Adlib driver's own jingle, song 1, is what the pure-Adlib setup
plays there instead; supaplexSDL uses it only if it has no digitised effects.

## Music

The music is Adlib (OPL2), not MIDI, and it lives in `ADLIB.SND` — one of the
five loadable driver overlays the game picks between according to
`SUPAPLEX.CFG`. Each overlay is a flat real-mode blob the game loads and calls
through `INT 80h` with the command in `AH` and the OPL base port in `DX`;
`re/ghidra_snd/` holds its decompilation. The commands that matter:

| `AH` | Effect |
|------|--------|
| 0 | start song `AL` — 0 is the theme, 1 the exit jingle |
| 1 | sequencer tick |
| 2 | all notes off |
| 4 | the driver's own Adlib sound effect `AL`, unused here |

`SUPAPLEX.CFG` is four lower-case bytes; the third is `m` for music on and `n`
for off, and the shipped file reads `bkmx` — Sound Blaster, keyboard, music on,
effects on. With `b` the game loads `ADLIB.SND` for the music and
`BLASTER.SND` for the effects, so the music is the same either way.

### Tick rate

The game points `INT 8` at 46c2:072e and reprograms the PIT at 46c2:080a with
divisor `0x5D38`, so the timer fires at 1193182/23864 = **50 Hz** and calls the
driver's tick on every one. The tick adds the song's tempo byte to an 8-bit
accumulator and steps the five channels whenever it overflows, so the theme's
tempo of 43 gives 50 × 43/256 = 8.4 steps per second.

### Data layout in `ADLIB.SND`

| Offset | Contents |
|--------|----------|
| `0x32` | `AH` dispatch table; the entries from index `0x10` double as the stream commands |
| `0x68` | F-number low byte, 96 notes |
| `0xC8` | block and F-number high, 96 notes (8 octaves of 12) |
| `0x128` | word per instrument, each pointing at 11 bytes |
| `0x19E` | the instruments themselves |
| `0xAC5` | pattern byte streams |
| `0xF93` | song table: 11 bytes each — tempo, then 5 pattern-list pointers |

A song has five OPL2 channels (0, 1, 2, 3, 4). Each has a NUL-terminated list
of pattern addresses which loops for ever, and each pattern is a byte stream:

| Byte | Meaning |
|------|---------|
| `00`–`7F` | play this note, wait `len` steps |
| `80` | key off, wait `len` steps |
| `81` | hold the note sounding, wait `len` steps |
| `82` | advance to the next pattern in the list, wrapping |
| `83` | end of song |
| `84 nn` / `85 nn` | channel / global transpose |
| `86 nn` | instrument |
| `87 nn` | volume, 0 to 63 |
| `88 ww` | set the pattern list to `ww` and enter its first pattern |
| `89 nn` | tempo |
| `8A nn` | detune, added to the F-number low byte |
| `E0`–`FF` | set the note length to `byte - 0xDF` steps, then keep reading |

An instrument is 11 bytes: attack/decay, sustain/release, multiplier, total
level and waveform for the carrier, the same five for the modulator, then
feedback/connection. The player is `src/music.c`.

### Verification

`re/tools/musicdump.c` renders a song straight to a WAV. To check it against
the original, DOSBox was run under Xvfb with SDL's disk audio driver
(`SDL_AUDIODRIVER=disk`) capturing the mixer output, with `SUPAPLEX.CFG` set to
`akmx`. Aligning the two recordings gives a waveform correlation of 0.91 to
0.95 on half-second windows, drifting by under 5 ms over 12 seconds — that is
DOSBox's own scheduling jitter, since both sides run the same DBOPL core. The
levels match too: 3901 RMS / 25072 peak against the original's 3962 / 25124,
once DOSBox's Adlib mixer scale of 2.0 is applied.

## Sprites

Animation frame lists are stored as VGA offsets, which look meaningless until
you read the loader. `FUN_46c2_0db1` writes MOVING.DAT row `r` to offset
`0xb72 + r*0x7a`, reduced by `0x4d0c` while `>= 0x4d34`; because 0x4d0c is not
a multiple of the 122-byte stride, that wrap interleaves the 462 rows into
three column bands of the buffer. Inverting it maps any frame pointer back to a
MOVING.DAT pixel coordinate — `re/tools/sprites.py` does this and
`re/tools/genanim.py` emits `src/anim.h` from it.

Murphy has 34 animation descriptors at `DS:0x0dfe`, each giving a start offset,
a per-frame delta, a sprite size and a frame list; walking up and down have two
variants apiece, picked by which way he last moved horizontally.

## Demo format

`DEMO*.BIN` files are self-contained:

```
[1536-byte level][level number, bit 7 = embedded level][RLE key stream][0xFF]
```

Each key byte is `(count << 4) | key`, meaning `count+1` frames of that key.
`--replay` plays one headlessly and reports the outcome.

The original can check them too: `SPFIXnn @ :DEMO5.BIN` plays a demo at warp
speed and prints the verdict, which is how the reference results below were
obtained.

## Verification

Two independent checks, both against the original binary running in DOSBox.

**Demo replay.** Of the ten bundled demos the original reports only DEMO5 and
DEMO7 as successful; the other eight fail in the original too. picosupaplex
agrees on all ten:

```
DEMO5  EASY DEAL     COMPLETED  51/51 infotrons
DEMO7  GO THROUGH!   COMPLETED  44/44 infotrons
DEMO0..4, 6, 8, 9    fail, as they do in the original
```

**Playfield diff.** `re/patch/` builds a copy of the unpacked executable with a
stub injected over the demo-record routine (never called during playback) that
writes the 2880-byte playfield to `FIELD.BIN` at a chosen frame. Comparing that
against `--fieldbin` gives an exact, cell-by-cell check:

```
DEMO5 frames 1000 / 2500 / 4000 / 5000 / 5700 : 0 of 1440 cells differ
DEMO7 frames  500 / 1000 / 3000 / 5000 / 5900 : 0 of 1440 cells differ
```

**Rendering.** First-frame pixel diff against DOSBox, 320x200:

```
level   1: 0.13%    level  20: 0.31%
level  20: 0.31%    level  23: 0.08%
level  45: 0.68%
```

The residual is animated tiles (Murphy's blink, snik snaks, terminals,
electrons, bugs) and the elapsed-time digit in the panel.

## What is and is not implemented

**Faithful**, ported from the decompilation and demo-verified: the field model
and level-load conversion, frame order, zonk and infotron falling and rolling
including all three vacate routines, explosions, bugs, snik snak and electron
state machines, Murphy's full target tables, action codes and durations,
pushing, ports, viewport and border, panel layout, palettes, every file format.

Also implemented since: the digitised sound effects, every sprite animation
(Murphy's walk, eat and push; zonk and infotron falling and rolling; explosions;
bug blink; snik snak and electron turning and moving), orange-disk physics,
terminal flicker and arming, special-port gravity / freeze-zonks /
freeze-enemies, the delayed-explosion timer array that drives chain reactions,
the start screens (title fade and credits), the Adlib music, and a level-select
front end on the original menu artwork.

**Not implemented**: the driver's own Adlib sound effects (`AH=4`) — the
digitised ones from `BLASTER.SND` are used instead — and Supaplex's own menu system — player records (`PLAYER.LST`),
the hall of fame (`HALLFAME.LST`), rankings, statistics, the gfx-tutor and
controls screens — and the level-start pan. picosupaplex substitutes a plain
level-select screen for these. Red-disk detonation timers are still a stub, and
the Adlib / Roland / SoundBlaster drivers are not used (the digitised effects
from `BLASTER.SND` are).

## Repository layout

```
src/             the game (data.c, render.c, game.c, demo.c, sound.c, music.c,
                 menu.c, title.c, main.c)
src/anim.h       sprite tables generated from the EXE
src/titlepal.h   start-screen palettes generated from the EXE
src/opl/         DOSBox's DBOPL OPL2 emulator, vendored (GPLv2+) plus a C shim
orig/            original game data — yours, not in the repository
```
