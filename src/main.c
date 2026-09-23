/* picosupaplex - SDL2 front end, pure software rendering. */
#include "sp.h"
#include "game.h"
#include "demo.h"
#include "sound.h"
#include "menu.h"
#include "title.h"
#include "music.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPS 35                      /* the original's game rate */

static GameData gd;
static Game     game;

static void present(SDL_Texture *tex, const Image *scr, const Palette *pal)
{
    uint32_t *pix; int pitch;
    if (SDL_LockTexture(tex, NULL, (void **)&pix, &pitch) != 0) return;
    uint32_t lut[16];
    for (int i = 0; i < 16; i++)
        lut[i] = 0xFF000000u | ((uint32_t)pal->r[i] << 16)
               | ((uint32_t)pal->g[i] << 8) | pal->b[i];
    for (int y = 0; y < scr->h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pix + (size_t)y * pitch);
        const uint8_t *src = scr->px + (size_t)y * scr->w;
        for (int x = 0; x < scr->w; x++) row[x] = lut[src[x] & 15];
    }
    SDL_UnlockTexture(tex);
}

int main(int argc, char **argv)
{
    const char *dir = "orig";
    const char *replay = NULL;
    const char *drive = NULL;
    int trace_scroll = 0, trace_info = 0, trace_murphy = 0, verbose = 0;
    const char *dump_file = NULL; int dump_from = 0, dump_to = 0; int field_at = -1; const char *fieldbin = NULL;
    int level = 1, scale = 3, direct = 0, music = -1;   /* -1: ask SUPAPLEX.CFG */
    const char *player = "        ";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc)      dir = argv[++i];
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) { level = atoi(argv[++i]); direct = 1; }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) player = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
        else if (!strcmp(argv[i], "--drive") && i + 1 < argc)  drive = argv[++i];
        else if (!strcmp(argv[i], "--trace-scroll")) trace_scroll = 1;
        else if (!strcmp(argv[i], "--trace-info")) trace_info = 1;
        else if (!strcmp(argv[i], "--trace-murphy")) trace_murphy = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--music")) music = 1;
        else if (!strcmp(argv[i], "--no-music")) music = 0;
        else if (!strcmp(argv[i], "--field") && i + 1 < argc) field_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fieldbin") && i + 1 < argc) fieldbin = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 3 < argc) {
            dump_from = atoi(argv[++i]); dump_to = atoi(argv[++i]); dump_file = argv[++i];
        }
        else { fprintf(stderr, "usage: %s [-d datadir] [-l level] [-s scale] [-p player]\n", argv[0]);
               return 1; }
    }
    if (level < 1) level = 1;
    if (level > NUM_LEVELS) level = NUM_LEVELS;

    /* SUPAPLEX.CFG is four lower-case bytes; the third is "m" for music on
     * and "n" for off (SPFIX62.DOC).  The shipped file says "bkmx". */
    if (music < 0) {
        char p[512]; snprintf(p, sizeof p, "%s/SUPAPLEX.CFG", dir);
        FILE *cf = fopen(p, "rb"); char cfg[4] = { 0, 0, 0, 0 };
        music = 0;
        if (cf) { if (fread(cfg, 1, 4, cf) == 4) music = (cfg[2] == 'm'); fclose(cf); }
    }

    if (replay && fieldbin) {           /* binary field dump for exact diffing */
        Demo d;
        if (!demo_load(&d, replay)) return 1;
        game_start(&game, &d.level, d.level_no);
        for (int i = 0; i < d.nkeys && i < field_at; i++) {
            Dir dd; bool sp;
            demo_input(d.keys[i], &dd, &sp);
            game_step(&game, dd, sp);
        }
        FILE *f = fopen(fieldbin, "wb");
        if (!f) return 1;
        fwrite(game.f, 2, LVL_TILES, f);
        fclose(f);
        printf("wrote field after %d frames to %s\n", field_at, fieldbin);
        return 0;
    }

    if (replay && field_at >= 0) {      /* print the field around Murphy at a frame */
        Demo d;
        if (!demo_load(&d, replay)) return 1;
        game_start(&game, &d.level, d.level_no);
        for (int i = 0; i < d.nkeys && i <= field_at; i++) {
            Dir dd; bool sp;
            demo_input(d.keys[i], &dd, &sp);
            game_step(&game, dd, sp);
        }
        int mx = game.murphy % LVL_W, my = game.murphy / LVL_W;
        printf("frame %d: murphy=(%d,%d) step=%d act=%d infotrons=%d\n",
               field_at, mx, my, game.m_step, (int)game.m_act, game.infotrons);
        for (int y = my - 3; y <= my + 3; y++) {
            if (y < 0 || y >= LVL_H) continue;
            printf("  y=%2d:", y);
            for (int x = mx - 6; x <= mx + 6; x++) {
                if (x < 0 || x >= LVL_W) { printf("      "); continue; }
                printf(" %04x", game.f[y * LVL_W + x]);
            }
            printf("\n");
        }
        printf("  x=   ");
        for (int x = mx - 6; x <= mx + 6; x++) printf("   %2d", x);
        printf("\n");
        return 0;
    }

    if (replay && dump_file) {          /* dump rendered frames for comparison */
        Demo d;
        if (!sp_load_all(&gd, dir)) return 1;
        if (!demo_load(&d, replay)) return 1;
        game_start(&game, &d.level, d.level_no);
        snprintf(game.player, sizeof game.player, "%-8.8s", "DEMO");
        if (!sound_init(dir, music)) fprintf(stderr, "sound unavailable, continuing silently\n");

    Image screen = { SCR_W, SCR_H, calloc(SCR_W * SCR_H, 1) };
        FILE *out = fopen(dump_file, "wb");
        if (!out) { fprintf(stderr, "cannot write %s\n", dump_file); return 1; }
        for (int i = 0; i < d.nkeys; i++) {
            Dir dd; bool sp;
            demo_input(d.keys[i], &dd, &sp);
            game_step(&game, dd, sp);
            if (i >= dump_from && i <= dump_to) {
                game_draw(&game, &gd, &screen);
                fwrite(screen.px, 1, SCR_W * SCR_H, out);
            }
            if (i > dump_to) break;
            if (game.finished || game.dead) break;
        }
        fclose(out);
        printf("dumped frames %d..%d to %s\n", dump_from, dump_to, dump_file);
        return 0;
    }

    if (drive) {   /* --drive "4x40,3x16": key code x frames, comma separated */
        if (!sp_load_all(&gd, dir)) return 1;
        game_start(&game, &gd.levels[level - 1], level);
        printf("drive level %d %s  murphy starts (%d,%d)\n", level,
               gd.levels[level-1].title, game.murphy % LVL_W, game.murphy / LVL_W);
        int frame = 0;
        for (const char *p = drive; *p; ) {
            int key = 0, n = 0;
            if (sscanf(p, "%dx%d", &key, &n) != 2) break;
            for (int k = 0; k < n; k++) {
                Dir dd; bool sp;
                demo_input((uint8_t)key, &dd, &sp);
                game_step(&game, dd, sp);
                frame++;
                if (frame % 4 == 0)
                    printf("  f%-4d key=%d murphy=(%2d,%2d) step=%d info=%d%s%s\n",
                           frame, key, game.murphy % LVL_W, game.murphy / LVL_W,
                           game.m_step, game.infotrons,
                           game.dead ? " DEAD:" : "", game.dead && game.death ? game.death : "");
                if (game.dead || game.finished) break;
            }
            if (game.dead || game.finished) break;
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        printf("  end: %s\n", game.finished ? "COMPLETED" : game.dead ? "DEAD" : "alive");
        return 0;
    }

    if (replay) {                       /* headless demo playback: physics test */
        Demo d;
        if (!demo_load(&d, replay)) return 1;
        game_start(&game, &d.level, d.level_no);
        if (!trace_scroll && !trace_info && !trace_murphy)
            printf("replay %s: level %d %-23s keys=%d\n",
                   replay, d.level_no, d.level.title, d.nkeys);
        int i, reported = 0, prev = game.murphy;
        for (i = 0; i < d.nkeys; i++) {
            Dir dd; bool sp;
            demo_input(d.keys[i], &dd, &sp);
            game_step(&game, dd, sp);
            if (trace_info) {
                static int last = -1;
                if (game.infotrons != last) {
                    printf("%d %.2f %d\n", i, i / 35.0, game.infotrons_needed - game.infotrons);
                    last = game.infotrons;
                }
            }
            if (trace_murphy) {
                int sx, sy; game_viewport(&game, &sx, &sy);
                int px, py; game_murphy_px(&game, &px, &py);
                printf("%d %d %d cell=(%d,%d) step=%d act=%d dst=%d\n", i,
                       px - sx, py - sy, game.murphy % LVL_W, game.murphy / LVL_W,
                       game.m_step, (int)game.m_act, game.m_dst);
            }
            if (trace_scroll) {
                int sx, sy; game_viewport(&game, &sx, &sy);
                printf("%d %d %d\n", i, sx, sy);
            }
            if (verbose && game.blocked_by && reported < 6) {
                printf("  blocked f%-5d dir=%d target tile=%2d state=%02x  murphy=(%d,%d)\n",
                       i, game.blocked_dir, game.blocked_by & 0xff,
                       game.blocked_by >> 8, game.murphy % LVL_W, game.murphy / LVL_W);
                reported++;
                game.blocked_by = 0;
            }
            prev = game.murphy;
            if (game.finished || game.dead) break;
        }
        (void)prev;
        if (!trace_scroll && !trace_info && !trace_murphy) printf("  -> frame %d/%d  %s%s%s  murphy=(%d,%d) infotrons %d/%d\n", i, d.nkeys,
               game.finished ? "COMPLETED" : game.dead ? "DIED" : "ran out of input",
               game.death ? ": " : "", game.death ? game.death : "",
               game.murphy % LVL_W, game.murphy / LVL_W,
               game.infotrons, game.infotrons_needed);
        return game.finished ? 0 : 2;
    }

    if (!sp_load_all(&gd, dir)) { fprintf(stderr, "failed to load game data\n"); return 1; }
    printf("loaded: level %d = %s\n", level, gd.levels[level - 1].title);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_Window *win = SDL_CreateWindow("picosupaplex",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCR_W * scale, SCR_H * scale, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    SDL_RenderSetLogicalSize(ren, SCR_W, SCR_H);

    if (!sound_init(dir, music)) fprintf(stderr, "sound unavailable, continuing silently\n");
    sound_music(MUSIC_THEME);   /* the original starts it as the driver loads */

    Image screen = { SCR_W, SCR_H, calloc(SCR_W * SCR_H, 1) };
    game_start(&game, &gd.levels[level - 1], level);
    snprintf(game.player, sizeof game.player, "%-8.8s", player);

    Menu menu;
    menu_init(&menu, level);
    bool solved[NUM_LEVELS];
    memset(solved, 0, sizeof solved);
    Title title;
    title_init(&title);
    /* -l N starts that level straight away, like the original's /x option;
     * without it the start screens play and the level-select comes up. */
    enum { ST_TITLE, ST_MENU, ST_PLAY } state = direct ? ST_PLAY : ST_TITLE;

    bool running = true;
    int over = 0;
    uint32_t next = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type != SDL_KEYDOWN) continue;
            SDL_Keycode k = e.key.keysym.sym;
            if (state == ST_TITLE) {
                if (k == SDLK_ESCAPE) title.done = true;
                else title_key(&title);
                if (title.done) { state = ST_MENU; menu_init(&menu, level); }
            } else if (state == ST_MENU) {
                menu_key(&menu, k);
                if (menu.quit) running = false;
                if (menu.play) {
                    menu.play = false;
                    level = menu.sel + 1;
                    game_start(&game, &gd.levels[level - 1], level);
                    snprintf(game.player, sizeof game.player, "%-8.8s", player);
                    over = 0; state = ST_PLAY;
                }
            } else {
                if (k == SDLK_ESCAPE) { state = ST_MENU; menu_init(&menu, level); }
                if (k == SDLK_r) {
                    game_start(&game, &gd.levels[level - 1], level);
                    snprintf(game.player, sizeof game.player, "%-8.8s", player);
                    over = 0;
                }
                if (k == SDLK_F2 && level > 1) {
                    level--; game_start(&game, &gd.levels[level - 1], level);
                    snprintf(game.player, sizeof game.player, "%-8.8s", player);
                    over = 0;
                }
                if (k == SDLK_F3 && level < NUM_LEVELS) {
                    level++; game_start(&game, &gd.levels[level - 1], level);
                    snprintf(game.player, sizeof game.player, "%-8.8s", player);
                    over = 0;
                }
            }
        }

        const Palette *pal = &gd.pal[1];
        if (state == ST_TITLE) {
            title_step(&title, &gd, &screen);
            pal = &title.pal;
            if (title.done) { state = ST_MENU; menu_init(&menu, level); }
        } else if (state == ST_MENU) {
            menu_draw(&menu, &gd, &screen, solved);
        } else {
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            Dir want = DIR_NONE;
            if (ks[SDL_SCANCODE_UP])         want = DIR_UP;
            else if (ks[SDL_SCANCODE_DOWN])  want = DIR_DOWN;
            else if (ks[SDL_SCANCODE_LEFT])  want = DIR_LEFT;
            else if (ks[SDL_SCANCODE_RIGHT]) want = DIR_RIGHT;
            bool space = ks[SDL_SCANCODE_SPACE] != 0;

            game_step(&game, want, space);
            game_draw(&game, &gd, &screen);

            if (game.dead || game.finished) {
                const char *msg = game.finished ? "    LEVEL  COMPLETE    "
                                                : "  MURPHY  DID  NOT  SURVIVE  ";
                int x = (SCR_W - (int)strlen(msg) * 8) / 2;
                sp_text_bg(&screen, &gd.chars8, x, VIEW_H / 2 - 4, msg,
                           game.finished ? 2 : 6, 0);
                if (game.finished) solved[level - 1] = true;
                if (over == 0 && game.finished) sound_music(MUSIC_EXIT);
                if (++over > FPS * 2) {
                    sound_music(MUSIC_THEME);
                    over = 0;
                    if (game.finished && level < NUM_LEVELS) level++;
                    menu_init(&menu, level);
                    state = ST_MENU;
                }
            } else over = 0;
        }

        present(tex, &screen, pal);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        next += 1000 / FPS;
        int32_t wait = (int32_t)(next - SDL_GetTicks());
        if (wait > 0) SDL_Delay((uint32_t)wait); else next = SDL_GetTicks();
    }

    free(screen.px);
    sound_quit();
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    sp_free_all(&gd);
    return 0;
}
