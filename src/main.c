/*
 * main.c - entry point of the portable INVATAXI.
 *
 * Usage: invataxi-sdl [--sound-menu] [path-to-original-game-directory]
 *
 * The original game files (INVATAXI.EXE, HJUVA_#1.EXE, HJUVA_#2.EXE,
 * HARMONIA.NVA, HISCORE.NVA and 1.SKP ... 24.SKP) are needed at runtime.
 * Without an argument the game looks in the current directory, then next to
 * the executable, then in a "data" folder next to the executable.
 * In the web version the files are packed into the page at /game.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "files.h"
#include "game.h"
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

/* Uses d as the game directory if it contains INVATAXI.EXE and HARMONIA.NVA;
   returns 1 on success. */
static int try_dir(const char *d)
{
    if (game_file_exists(d, "INVATAXI.EXE") && game_file_exists(d, "HARMONIA.NVA")) {
        files_set_dir(d);
        return 1;
    }
    return 0;
}

/* Parses the command line, locates the original game files, loads the pictures
   and runs the game. */
int main(int argc, char **argv)
{
    int found = 0, i;
    const char *dir = NULL;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sound-menu")) game_sound_menu = 1;
        else dir = argv[i];
    }
    if (dir) found = try_dir(dir);
#ifdef __EMSCRIPTEN__
    if (!found && try_dir("/game")) {      /* packed into the web page */
        found = 1;
        game_file_restore("hiscore.nva");  /* high scores from an earlier visit */
    }
#endif
    if (!found) found = try_dir("");
    if (!found) {
        char *base = SDL_GetBasePath();
        if (base) {
            char buf[1200];
            found = try_dir(base);
            if (!found) { snprintf(buf, sizeof buf, "%sdata", base); found = try_dir(buf); }
            if (!found) { snprintf(buf, sizeof buf, "%s../Resources", base); found = try_dir(buf); }
            SDL_free(base);
        }
    }
    if (!found) {
        const char *msg =
            "Original INVATAXI game files not found.\n\n"
            "Copy INVATAXI.EXE, HJUVA_#1.EXE, HJUVA_#2.EXE, HARMONIA.NVA,\n"
            "HISCORE.NVA and the *.SKP files into the \"data\" folder next to\n"
            "this program, or pass the game directory as the first argument.";
        fprintf(stderr, "%s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "INVATAXI", msg, NULL);
        return 1;
    }
    if (!game_load_assets()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "INVATAXI",
                                 "Could not read the pictures from the original EXE files.", NULL);
        return 1;
    }
    if (!plat_init("INVATAXI")) return 1;
#ifdef __EMSCRIPTEN__
    /* Show the title picture behind the page's Start button and wait for the
       click: browsers allow sound only after the user has interacted. */
    game_show_title();
    EM_ASM({ if (Module.onTitleShown) Module.onTitleShown(); });
    while (!EM_ASM_INT({ return Module.startClicked ? 1 : 0; }))
        vga_wait_retrace();
    while (crt_keypressed()) crt_readkey();   /* drop keys pressed before Start */
#endif
    game_main();
    plat_shutdown();
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onGameExit) Module.onGameExit(); });   /* web page: "play again" */
#endif
    return 0;
}
