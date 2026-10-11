#ifndef GAME_H
#define GAME_H
extern int game_sound_menu;   /* show the original sound card menu */
int  game_load_assets(void);
void game_main(void);
#ifdef __EMSCRIPTEN__
void game_show_title(void);   /* web version: title picture behind the Start button */
#endif
#endif
