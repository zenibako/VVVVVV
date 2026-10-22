/* The SDL3 window and display on openfpgaOS.
 *
 * There is one window, the Pocket's 320x240 screen, which is the game's
 * own resolution.  The framebuffer runs in RGB565; each presented frame is
 * converted from the renderer's ARGB8888 back buffer. */
#include <SDL3/SDL.h>

#include <stdio.h>

#include "of_video.h"
#include "render_internal.h"

#ifdef POCKET_SHOW_FPS
#include "of_timer.h"
#endif

struct SDL_Window
{
    bool created;
};

static SDL_Window window;
static of_video_mode_t video_mode;

static void start_video(void)
{
    static bool started;
    if (started)
    {
        return;
    }
    started = true;
    of_video_init();
    of_video_mode_t wanted = {POCKET_SCREEN_W, POCKET_SCREEN_H, 0, OF_VIDEO_MODE_RGB565, 0};
    if (of_video_set_mode(&wanted) != 0)
    {
        printf("vvvvvv: could not set the 320x240 RGB565 video mode\n");
    }
    of_video_get_mode(&video_mode);
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
}

#ifdef POCKET_SHOW_FPS
/* Frames presented in the last second, drawn in the top right corner of
 * the framebuffer in white 3x5 digits at twice their size. */
static void draw_fps(Uint8 *fb, int stride)
{
    static const Uint16 digits[10] = {
        075557, 026222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717
    };
    static Uint32 second_start;
    static int frames;
    static int shown;
    const Uint32 now = of_time_ms();
    frames++;
    if (now - second_start >= 1000)
    {
        shown = frames;
        frames = 0;
        second_start = now;
    }
    const int x0 = POCKET_SCREEN_W - 2 - 3 * 8;
    for (int y = 0; y < 14; y++)
    {
        Uint16 *row = (Uint16 *) (fb + (size_t) y * stride) + x0;
        for (int x = 0; x < 3 * 8; x++)
        {
            row[x] = 0;
        }
    }
    const int value = shown > 999 ? 999 : shown;
    const int places[3] = {value / 100, value / 10 % 10, value % 10};
    for (int d = 0; d < 3; d++)
    {
        const Uint16 glyph = digits[places[d]];
        for (int gy = 0; gy < 5; gy++)
        {
            for (int gx = 0; gx < 3; gx++)
            {
                if (!(glyph >> ((4 - gy) * 3 + (2 - gx)) & 1))
                {
                    continue;
                }
                for (int py = 0; py < 2; py++)
                {
                    Uint16 *row = (Uint16 *) (fb + (size_t) (2 + gy * 2 + py) * stride);
                    row[x0 + 1 + d * 8 + gx * 2] = 0xFFFF;
                    row[x0 + 2 + d * 8 + gx * 2] = 0xFFFF;
                }
            }
        }
    }
}
#endif

void POCKET_PresentFrame(const Uint32 *argb)
{
    Uint8 *fb = of_video_surface();
    const int stride = video_mode.stride ? video_mode.stride : POCKET_SCREEN_W * 2;
    for (int y = 0; y < POCKET_SCREEN_H; y++)
    {
        /* Two pixels per store: the rows are 4-byte aligned. */
        Uint32 *row = (Uint32 *) (fb + (size_t) y * stride);
        const Uint32 *src = argb + (size_t) y * POCKET_SCREEN_W;
        for (int x = 0; x < POCKET_SCREEN_W; x += 2)
        {
            const Uint32 p0 = src[x];
            const Uint32 p1 = src[x + 1];
            const Uint32 c0 = ((p0 >> 8) & 0xF800) | ((p0 >> 5) & 0x07E0) | ((p0 >> 3) & 0x001F);
            const Uint32 c1 = ((p1 >> 8) & 0xF800) | ((p1 >> 5) & 0x07E0) | ((p1 >> 3) & 0x001F);
            row[x >> 1] = c0 | (c1 << 16);
        }
    }
#ifdef POCKET_SHOW_FPS
    draw_fps(fb, stride);
#endif
    of_video_flip();
}

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{
    (void) title;
    (void) w;
    (void) h;
    (void) flags;
    start_video();
    window.created = true;
    return &window;
}

void SDL_DestroyWindow(SDL_Window *w)
{
    if (w == &window)
    {
        window.created = false;
    }
}

SDL_Window *SDL_GetWindowFromID(SDL_WindowID id)
{
    (void) id;
    if (!window.created)
    {
        SDL_SetError("Invalid window ID");
        return NULL;
    }
    return &window;
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *w)
{
    (void) w;
    return SDL_WINDOW_FULLSCREEN | SDL_WINDOW_INPUT_FOCUS;
}

bool SDL_GetWindowSize(SDL_Window *w, int *width, int *height)
{
    (void) w;
    if (width != NULL)
    {
        *width = POCKET_SCREEN_W;
    }
    if (height != NULL)
    {
        *height = POCKET_SCREEN_H;
    }
    return true;
}

bool SDL_GetWindowSizeInPixels(SDL_Window *w, int *width, int *height)
{
    return SDL_GetWindowSize(w, width, height);
}

/* The window always fills the screen. */
bool SDL_SetWindowFullscreen(SDL_Window *w, bool fullscreen)
{
    (void) w;
    (void) fullscreen;
    return true;
}

bool SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *icon)
{
    (void) w;
    (void) icon;
    return true;
}

bool SDL_SetWindowMinimumSize(SDL_Window *w, int min_w, int min_h)
{
    (void) w;
    (void) min_w;
    (void) min_h;
    return true;
}

bool SDL_SetWindowPosition(SDL_Window *w, int x, int y)
{
    (void) w;
    (void) x;
    (void) y;
    return true;
}

bool SDL_SetWindowSize(SDL_Window *w, int width, int height)
{
    (void) w;
    (void) width;
    (void) height;
    return true;
}

bool SDL_SetWindowTitle(SDL_Window *w, const char *title)
{
    (void) w;
    (void) title;
    return true;
}

bool SDL_ShowWindow(SDL_Window *w)
{
    (void) w;
    return true;
}

bool SDL_MinimizeWindow(SDL_Window *w)
{
    (void) w;
    return true;
}

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *w)
{
    (void) w;
    return 1;
}

const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID displayID)
{
    static const SDL_DisplayMode mode = {
        1, SDL_PIXELFORMAT_RGB565, POCKET_SCREEN_W, POCKET_SCREEN_H, 1.0f, 60.0f, 60, 1, NULL
    };
    if (displayID != 1)
    {
        SDL_SetError("Invalid display");
        return NULL;
    }
    return &mode;
}

const char *SDL_GetCurrentVideoDriver(void)
{
    return "openfpgaos";
}
