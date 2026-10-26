/* The game's writable directories (saves/, levels/, screenshots/), kept in
 * one openfpgaOS save file. io.c sends paths under them here. */
#ifndef POCKET_SAVEFS_H
#define POCKET_SAVEFS_H

#include <SDL3/SDL.h>

/* True if path is in one of the writable directories. */
bool SAVEFS_owns(const char *path);

SDL_IOStream *SAVEFS_open(const char *path, const char *mode);
bool SAVEFS_stat(const char *path, SDL_PathInfo *info);
bool SAVEFS_enumerate(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata);
bool SAVEFS_mkdir(const char *path);
bool SAVEFS_remove(const char *path);

#endif
