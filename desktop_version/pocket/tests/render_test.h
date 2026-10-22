/* What the renderer test records from each renderer. */
#ifndef RENDER_TEST_H
#define RENDER_TEST_H

#include <SDL3/SDL.h>

#define CAPTURE_MAX 64

typedef struct
{
    const char *name;
    int w, h, bytes_per_pixel; /* 0 by 0 for a number */
    Uint8 *bytes;
    int number;
} CaptureItem;

typedef struct
{
    CaptureItem items[CAPTURE_MAX];
    int count;
} Capture;

/* Defined in render_test.c, against desktop SDL3. */
void capture_surface(Capture *capture, const char *name, SDL_Surface *surface);
void capture_number(Capture *capture, const char *name, int number);

void render_ops_sdl(SDL_Renderer *renderer, Capture *capture);
void render_ops_pocket(SDL_Renderer *renderer, Capture *capture);

#endif
