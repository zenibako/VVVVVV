/* Shared between the port's surface, renderer and video code. */
#ifndef POCKET_RENDER_INTERNAL_H
#define POCKET_RENDER_INTERNAL_H

#include <SDL3/SDL.h>

/* floor(x / 255) for x up to 255 * 255, without a divide. */
static inline Uint32 POCKET_Div255(Uint32 x)
{
    return (x + 1 + (x >> 8)) >> 8;
}

/* s over d with coverage a, one channel. */
static inline Uint8 POCKET_Blend(Uint32 s, Uint32 d, Uint32 a)
{
    return (Uint8) POCKET_Div255(s * a + d * (255 - a));
}

static inline bool POCKET_IntersectRect(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result)
{
    const int x0 = SDL_max(a->x, b->x);
    const int y0 = SDL_max(a->y, b->y);
    const int x1 = SDL_min(a->x + a->w, b->x + b->w);
    const int y1 = SDL_min(a->y + a->h, b->y + b->h);
    result->x = x0;
    result->y = y0;
    result->w = x1 > x0 ? x1 - x0 : 0;
    result->h = y1 > y0 ? y1 - y0 : 0;
    return result->w > 0 && result->h > 0;
}

/* Nearest-neighbour source position of destination step i when n source
 * pixels span d destination pixels, sampled at pixel centres in 16.16
 * fixed point exactly as SDL_stretch.c does. */
static inline int POCKET_NearestStep(int i, int n, int d)
{
    const Uint64 inc = ((Uint64) n << 16) / (Uint64) d;
    return (int) ((inc / 2 + (Uint64) i * inc) >> 16);
}

Uint32 POCKET_MapRGBA(const SDL_PixelFormatDetails *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void POCKET_GetRGBA(Uint32 pixel, const SDL_PixelFormatDetails *f, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a);

/* Converts a whole surface to packed ARGB8888 at out. */
bool POCKET_SurfaceToARGB(SDL_Surface *surface, Uint32 *out);

/* Shows a finished ARGB8888 frame of POCKET_SCREEN_W by POCKET_SCREEN_H;
 * video.c on the device, the test harness on a computer. */
#define POCKET_SCREEN_W 320
#define POCKET_SCREEN_H 240
void POCKET_PresentFrame(const Uint32 *argb);

#endif
