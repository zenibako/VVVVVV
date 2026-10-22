/* Compares the port's renderer with desktop SDL3's software renderer.
 *
 * render_ops.c is built twice: against SDL3, and against sdl3/render.c and
 * sdl3/surface.c with every SDL call renamed with a PR_ prefix (render_rename.h).
 * Both runs record each step's pixels; a channel may differ by
 * TOLERANCE, for rounding in SDL's blend arithmetic.  Set
 * RENDER_TEST_OUT to a folder to also write each step as a PPM, the SDL
 * picture on the left and the port's on the right. */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_internal.h"
#include "render_test.h"

#define TOLERANCE 2

SDL_Renderer *PR_SDL_CreateRenderer(SDL_Window *window, const char *name);

/* The window's framebuffer, video.c's on the device. */
static Uint16 frame_buffer[POCKET_SCREEN_W * POCKET_SCREEN_H];

Uint16 *POCKET_FrameBuffer(int *pitch)
{
    *pitch = POCKET_SCREEN_W;
    return frame_buffer;
}

void POCKET_PresentFrame(void)
{
}

static CaptureItem *next_item(Capture *capture, const char *name)
{
    if (capture->count == CAPTURE_MAX)
    {
        fprintf(stderr, "too many captures\n");
        exit(2);
    }
    CaptureItem *item = &capture->items[capture->count++];
    memset(item, 0, sizeof(*item));
    item->name = name;
    item->tolerance = TOLERANCE;
    return item;
}

void capture_surface(Capture *capture, const char *name, SDL_Surface *surface)
{
    CaptureItem *item = next_item(capture, name);
    if (surface == NULL)
    {
        item->number = -1;
        return;
    }
    item->w = surface->w;
    item->h = surface->h;
    item->bytes_per_pixel = SDL_BYTESPERPIXEL(surface->format);
    const size_t row = (size_t) item->w * item->bytes_per_pixel;
    item->bytes = (Uint8 *) malloc(row * item->h);
    for (int y = 0; y < item->h; y++)
    {
        memcpy(item->bytes + row * y, (const Uint8 *) surface->pixels + (size_t) surface->pitch * y, row);
    }
}

void capture_window(Capture *capture, const char *name, SDL_Surface *surface)
{
    capture_surface(capture, name, surface);
    CaptureItem *item = &capture->items[capture->count - 1];
    if (item->bytes == NULL)
    {
        return;
    }
    const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(surface->format);
    Uint32 *out = (Uint32 *) malloc((size_t) item->w * item->h * 4);
    for (int y = 0; y < item->h; y++)
    {
        for (int x = 0; x < item->w; x++)
        {
            Uint32 pixel = 0;
            memcpy(&pixel, item->bytes + ((size_t) y * item->w + x) * item->bytes_per_pixel, item->bytes_per_pixel);
            Uint8 r, g, b;
            SDL_GetRGB(pixel, details, NULL, &r, &g, &b);
            out[(size_t) y * item->w + x] = POCKET_FromRGB565(POCKET_ToRGB565(((Uint32) r << 16) | ((Uint32) g << 8) | b));
        }
    }
    free(item->bytes);
    item->bytes = (Uint8 *) out;
    item->bytes_per_pixel = 4;
    item->tolerance = 9; /* neighbouring RGB565 levels widen 8 or 9 apart */
}

void capture_number(Capture *capture, const char *name, int number)
{
    next_item(capture, name)->number = number;
}

static void write_ppm(const char *dir, int index, const CaptureItem *a, const CaptureItem *b)
{
    if (a->bytes_per_pixel < 3)
    {
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%02d.ppm", dir, index);
    FILE *f = fopen(path, "wb");
    if (f == NULL)
    {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", a->w * 2 + 4, a->h);
    for (int y = 0; y < a->h; y++)
    {
        for (int side = 0; side < 2; side++)
        {
            const CaptureItem *item = side ? b : a;
            for (int x = 0; x < item->w; x++)
            {
                const Uint8 *p = item->bytes + ((size_t) y * item->w + x) * item->bytes_per_pixel;
                /* Little-endian 32-bit formats here are B, G, R first. */
                Uint8 rgb[3] = {p[2], p[1], p[0]};
                if (item->bytes_per_pixel == 3)
                {
                    rgb[0] = p[0];
                    rgb[2] = p[2];
                }
                fwrite(rgb, 1, 3, f);
            }
            if (side == 0)
            {
                static const Uint8 gap[12] = {255, 0, 255, 255, 0, 255, 255, 0, 255, 255, 0, 255};
                fwrite(gap, 1, sizeof(gap), f);
            }
        }
    }
    fclose(f);
}

int main(void)
{
    static Capture sdl, pocket;

    SDL_Surface *window = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *sdl_renderer = SDL_CreateSoftwareRenderer(window);
    if (sdl_renderer == NULL)
    {
        fprintf(stderr, "SDL software renderer: %s\n", SDL_GetError());
        return 2;
    }
    render_ops_sdl(sdl_renderer, &sdl);

    SDL_Renderer *pocket_renderer = PR_SDL_CreateRenderer((SDL_Window *) &pocket, NULL);
    if (pocket_renderer == NULL)
    {
        fprintf(stderr, "port renderer: %s\n", SDL_GetError());
        return 2;
    }
    render_ops_pocket(pocket_renderer, &pocket);

    const char *out = getenv("RENDER_TEST_OUT");
    int failures = 0;
    if (sdl.count != pocket.count)
    {
        printf("FAIL: %d captures from SDL, %d from the port\n", sdl.count, pocket.count);
        return 1;
    }
    for (int i = 0; i < sdl.count; i++)
    {
        const CaptureItem *a = &sdl.items[i];
        const CaptureItem *b = &pocket.items[i];
        if (a->w != b->w || a->h != b->h || a->bytes_per_pixel != b->bytes_per_pixel)
        {
            printf("FAIL %-20s size %dx%dx%d, port %dx%dx%d\n", a->name,
                   a->w, a->h, a->bytes_per_pixel, b->w, b->h, b->bytes_per_pixel);
            failures++;
            continue;
        }
        if (a->bytes == NULL)
        {
            const bool same = a->number == b->number;
            printf("%s %-20s %d%s%d\n", same ? "ok  " : "FAIL", a->name, a->number,
                   same ? " = " : ", port ", b->number);
            failures += !same;
            continue;
        }
        int worst = 0, over = 0, first_x = -1, first_y = -1;
        const size_t n = (size_t) a->w * a->h * a->bytes_per_pixel;
        for (size_t j = 0; j < n; j++)
        {
            const int diff = abs((int) a->bytes[j] - (int) b->bytes[j]);
            if (diff > worst)
            {
                worst = diff;
            }
            if (diff > a->tolerance)
            {
                if (over++ == 0)
                {
                    first_x = (int) (j / a->bytes_per_pixel % a->w);
                    first_y = (int) (j / a->bytes_per_pixel / a->w);
                }
            }
        }
        if (over > 0)
        {
            const size_t at = ((size_t) first_y * a->w + first_x) * a->bytes_per_pixel;
            printf("FAIL %-20s %dx%d: %d channels off by more than %d (worst %d), first at %d,%d:",
                   a->name, a->w, a->h, over, a->tolerance, worst, first_x, first_y);
            for (int k = 0; k < a->bytes_per_pixel; k++)
            {
                printf(" %02x/%02x", a->bytes[at + k], b->bytes[at + k]);
            }
            printf("\n");
            failures++;
        }
        else
        {
            printf("ok   %-20s %dx%d, worst channel difference %d\n", a->name, a->w, a->h, worst);
        }
        if (out != NULL)
        {
            write_ppm(out, i, a, b);
        }
    }
    printf("%s: %d of %d captures differ\n", failures ? "FAIL" : "PASS", failures, sdl.count);
    return failures ? 1 : 0;
}
