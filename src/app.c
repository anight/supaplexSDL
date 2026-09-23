/* The interactive game loop, common to the desktop and to picosdl.
 *
 * Input is the keyboard, and a game controller if one is attached: stick or
 * D-pad to move, A to snap (Supaplex's space+direction) and to confirm in
 * menus, B or Start to leave a level, Back to restart it.  Under picosdl the
 * board's own analog stick is read as a joystick when no pad is attached,
 * its click standing in for A.  On the desktop plain joysticks are left
 * alone, because SDL enumerates some keyboards' media-key collections as
 * joysticks with an axis parked at one end. */
#include "app.h"
#include "sp.h"
#include "game.h"
#include "sound.h"
#include "music.h"
#include "menu.h"
#include "title.h"
#include "video.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define FPS       35                /* the original's game rate */
#define DEADZONE  12000
#define REPEAT_1  12                /* frames before a held direction repeats */
#define REPEAT_N  4                 /* and between repeats after that        */

void (*sp_app_frame_hook)(void);

static GameData gd;
static Game     game;
static Level    level_buf;

bool sp_cfg_music(const char *dir)
{
    Asset a;
    bool on = false;
    if (sp_asset_open(dir, "SUPAPLEX.CFG", &a)) {
        on = a.len >= 3 && a.data[2] == 'm';
        sp_asset_release(&a);
    }
    return on;
}

/* ------------------------------------------------------------------ input */

typedef struct {
    SDL_GameController *pad;
    SDL_Joystick       *stick;      /* picosdl: the board's stick, no pad */
    Dir  dir;                       /* direction held this frame          */
    bool a, b, start, back;         /* buttons held this frame            */
    bool a_prev, b_prev, start_prev, back_prev;
    Dir  dir_prev;
    int  held;                      /* frames the direction has been held */
} Pad;

static void pad_open(Pad *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (p->pad = SDL_GameControllerOpen(i)) != NULL)
            return;
#ifdef PICOSDL_SDL_H
    if (SDL_NumJoysticks() > 0) p->stick = SDL_JoystickOpen(0);
#endif
}

static Dir axis_dir(int x, int y)
{
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax < DEADZONE && ay < DEADZONE) return DIR_NONE;
    if (ay >= ax) return y < 0 ? DIR_UP : DIR_DOWN;
    return x < 0 ? DIR_LEFT : DIR_RIGHT;
}

static void pad_poll(Pad *p)
{
    p->a_prev = p->a; p->b_prev = p->b; p->start_prev = p->start; p->back_prev = p->back;
    p->dir_prev = p->dir;
    p->dir = DIR_NONE; p->a = p->b = p->start = p->back = false;

    if (p->pad) {
        SDL_GameController *c = p->pad;
        if      (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    p->dir = DIR_UP;
        else if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  p->dir = DIR_DOWN;
        else if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  p->dir = DIR_LEFT;
        else if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p->dir = DIR_RIGHT;
        else p->dir = axis_dir(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX),
                               SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
        p->a     = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A)
                || SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSTICK);
        p->b     = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B);
        p->start = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START);
        p->back  = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK);
    } else if (p->stick) {
        p->dir = axis_dir(SDL_JoystickGetAxis(p->stick, 0), SDL_JoystickGetAxis(p->stick, 1));
        p->a   = SDL_JoystickGetButton(p->stick, 0) != 0;
    }

    if (p->dir != DIR_NONE && p->dir == p->dir_prev) p->held++;
    else p->held = 0;
}

/* a direction as a menu keypress: once, then repeating while held */
static SDL_Scancode pad_menu_key(const Pad *p)
{
    static const SDL_Scancode K[5] = { SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UP, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT };
    if (p->dir == DIR_NONE) return SDL_SCANCODE_UNKNOWN;
    if (p->held == 0) return K[p->dir];
    if (p->held >= REPEAT_1 && (p->held - REPEAT_1) % REPEAT_N == 0) return K[p->dir];
    return SDL_SCANCODE_UNKNOWN;
}

#define PRESSED(p, f) ((p)->f && !(p)->f##_prev)

/* ------------------------------------------------------------------- play */

static void start_level(int level, const char *player)
{
    if (!sp_level(&gd, level - 1, &level_buf)) return;
    game_start(&game, &level_buf, level);
    snprintf(game.player, sizeof game.player, "%-8.8s", player);
}

int sp_app_run(const AppConfig *cfg)
{
    int level = cfg->level;
    const char *player = cfg->player ? cfg->player : "        ";
    int music = cfg->music;

    if (!sp_load_all(&gd, cfg->dir)) { fprintf(stderr, "failed to load game data\n"); return 1; }
    if (level < 1) level = 1;
    if (level > gd.n_levels) level = gd.n_levels;
    if (music < 0) music = sp_cfg_music(cfg->dir);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    Image screen;
    if (!video_open(cfg->scale > 0 ? cfg->scale : 3, &screen)) return 1;

    if (!sound_init(cfg->dir, music)) fprintf(stderr, "sound unavailable, continuing silently\n");
    sound_music(MUSIC_THEME);   /* the original starts it as the driver loads */

    Pad pad;
    pad_open(&pad);

    start_level(level, player);
    Menu menu;
    menu_init(&menu, level);
    static bool solved[NUM_LEVELS];
    Title title;
    title_init(&title);
    /* -l N starts that level straight away, like the original's /x option;
     * without it the start screens play and the level-select comes up. */
    enum { ST_TITLE, ST_MENU, ST_PLAY } state = cfg->direct ? ST_PLAY : ST_TITLE;

    bool running = true;
    int over = 0;
    uint32_t next = SDL_GetTicks();
    while (running) {
        /* Keys and pad presses both arrive here as scancodes, so the three
         * screens handle one kind of input. */
        SDL_Scancode keys[16]; int nk = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && nk < 16) keys[nk++] = e.key.keysym.scancode;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad.pad) pad_open(&pad);
        }
        pad_poll(&pad);
        if (state != ST_PLAY) {
            SDL_Scancode k = pad_menu_key(&pad);
            if (k && nk < 16) keys[nk++] = k;
            if ((PRESSED(&pad, a) || PRESSED(&pad, start)) && nk < 16) keys[nk++] = SDL_SCANCODE_RETURN;
        } else {
            if ((PRESSED(&pad, b) || PRESSED(&pad, start)) && nk < 16) keys[nk++] = SDL_SCANCODE_ESCAPE;
            if (PRESSED(&pad, back) && nk < 16) keys[nk++] = SDL_SCANCODE_R;
        }

        for (int i = 0; i < nk; i++) {
            SDL_Scancode k = keys[i];
            if (state == ST_TITLE) {
                if (k == SDL_SCANCODE_ESCAPE) title.done = true;
                else title_key(&title);
                if (title.done) { state = ST_MENU; menu_init(&menu, level); }
            } else if (state == ST_MENU) {
                menu_key(&menu, k);
#ifdef PICOSDL_SDL_H
                menu.quit = false;              /* there is nowhere to quit to */
#endif
                if (menu.quit) running = false;
                if (menu.play) {
                    menu.play = false;
                    level = menu.sel + 1;
                    start_level(level, player);
                    over = 0; state = ST_PLAY;
                }
            } else {
                if (k == SDL_SCANCODE_ESCAPE) { state = ST_MENU; menu_init(&menu, level); }
                if (k == SDL_SCANCODE_R) { start_level(level, player); over = 0; }
                if (k == SDL_SCANCODE_F2 && level > 1) { level--; start_level(level, player); over = 0; }
                if (k == SDL_SCANCODE_F3 && level < gd.n_levels) { level++; start_level(level, player); over = 0; }
            }
        }

        video_begin_frame();
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
            else want = pad.dir;
            bool space = ks[SDL_SCANCODE_SPACE] != 0 || pad.a;

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
                    if (game.finished && level < gd.n_levels) level++;
                    menu_init(&menu, level);
                    state = ST_MENU;
                }
            } else over = 0;
        }

        video_present(&screen, pal);
        if (sp_app_frame_hook) sp_app_frame_hook();

        next += 1000 / FPS;
        int32_t wait = (int32_t)(next - SDL_GetTicks());
        if (wait > 0) SDL_Delay((uint32_t)wait); else next = SDL_GetTicks();
    }

    sound_quit();
    video_close();
    SDL_Quit();
    sp_free_all(&gd);
    return 0;
}
