#ifndef FILES_H
#define FILES_H
#include <stdio.h>
#include <stddef.h>

/* Directory that holds the original game files (with trailing separator). */
void  files_set_dir(const char *dir);
const char *files_get_dir(void);
/* Open a game file case-insensitively (DOS names) inside the game directory. */
FILE *game_fopen(const char *name, const char *mode);
/* Read a whole game file. Returns malloc'd buffer or NULL. */
unsigned char *game_load(const char *name, size_t *len);
int   game_file_exists(const char *dir, const char *name);
/* Web version: keep a file the game has written (HISCORE.NVA) in the browser
   so it survives reloading the page, and bring it back at startup.
   Both do nothing on the desktop, where the file is simply on disk. */
void  game_file_commit(const char *name);
void  game_file_restore(const char *name);

#endif
