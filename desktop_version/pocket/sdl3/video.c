/* The SDL3 window and display on openfpgaOS.
 *
 * There is one window, the Pocket's 320x240 screen, which is the game's
 * own resolution.  The framebuffer runs in RGB565, and render.c draws the
 * window straight into it. */
#include <SDL3/SDL.h>

#include <stdio.h>

#include "of_video.h"
#include "render_internal.h"
#include "pocket_audio.h"

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
extern Uint32 POCKET_render_us;
extern Uint32 POCKET_render_part_us[6];
extern Uint32 POCKET_render_calls;
extern int POCKET_saves_written;
extern int POCKET_saves_failed;
static Uint32 present_us;

static void draw_number(Uint8 *fb, int stride, int line, int value)
{
    static const Uint16 digits[10] = {
        075557, 026222, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717
    };
    const int x0 = POCKET_SCREEN_W - 2 - 3 * 8;
    const int y0 = line * 14;
    for (int y = 0; y < 14; y++)
    {
        Uint16 *row = (Uint16 *) (fb + (size_t) (y0 + y) * stride) + x0;
        for (int x = 0; x < 3 * 8; x++)
        {
            row[x] = 0;
        }
    }
    value = value < 0 ? 0 : value > 999 ? 999 : value;
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
                    Uint16 *row = (Uint16 *) (fb + (size_t) (y0 + 2 + gy * 2 + py) * stride);
                    row[x0 + 1 + d * 8 + gx * 2] = 0xFFFF;
                    row[x0 + 2 + d * 8 + gx * 2] = 0xFFFF;
                }
            }
        }
    }
}

/* Once, tenths of a millisecond for a full screen of ARGB8888: memcpy,
 * a word fill loop, and a word copy loop. */
static int bench[3];

static void run_bench(void)
{
    static bool done;
    if (done)
    {
        return;
    }
    done = true;
    const size_t count = (size_t) POCKET_SCREEN_W * POCKET_SCREEN_H;
    Uint32 *a = (Uint32 *) SDL_malloc(count * 4);
    Uint32 *b = (Uint32 *) SDL_malloc(count * 4);
    if (a == NULL || b == NULL)
    {
        SDL_free(a);
        SDL_free(b);
        return;
    }
    Uint32 start = of_time_us();
    SDL_memcpy(b, a, count * 4);
    bench[0] = (int) ((of_time_us() - start) / 100);
    start = of_time_us();
    for (size_t i = 0; i < count; i++)
    {
        ((volatile Uint32 *) a)[i] = 0xFF000000u;
    }
    bench[1] = (int) ((of_time_us() - start) / 100);
    start = of_time_us();
    for (size_t i = 0; i < count; i++)
    {
        ((volatile Uint32 *) b)[i] = ((volatile Uint32 *) a)[i];
    }
    bench[2] = (int) ((of_time_us() - start) / 100);
    SDL_free(a);
    SDL_free(b);
}

/* Over the last second, down the top right corner: frames presented, then
 * the milliseconds per frame spent drawing in the renderer, presenting,
 * and everywhere else (the game's own code, input and sound); then saves
 * written to the save file and saves that failed; then tens of copies and
 * fills per frame. */
static void draw_profile(Uint8 *fb, int stride)
{
    static Uint32 second_start, render_start, present_start;
    static int frames;
    static int shown[4];
    static int parts[6];
    static Uint32 parts_start[6];
    static int calls;
    static Uint32 calls_start;
    run_bench();
    const Uint32 now = of_time_ms();
    frames++;
    if (now - second_start >= 1000)
    {
        const Uint32 elapsed_us = (now - second_start) * 1000;
        const Uint32 render = POCKET_render_us - render_start;
        const Uint32 present = present_us - present_start;
        shown[0] = frames;
        shown[1] = (int) (render / frames / 1000);
        shown[2] = (int) (present / frames / 1000);
        shown[3] = (int) ((elapsed_us - SDL_min(elapsed_us, render + present)) / frames / 1000);
        for (int i = 0; i < 6; i++)
        {
            /* Tenths of a millisecond per frame. */
            parts[i] = (int) ((POCKET_render_part_us[i] - parts_start[i]) / frames / 100);
            parts_start[i] = POCKET_render_part_us[i];
        }
        calls = (int) ((POCKET_render_calls - calls_start) / frames / 10);
        calls_start = POCKET_render_calls;
        frames = 0;
        second_start = now;
        render_start = POCKET_render_us;
        present_start = present_us;
    }
    for (int i = 0; i < 4; i++)
    {
        draw_number(fb, stride, i, shown[i]);
    }
    draw_number(fb, stride, 4, POCKET_saves_written);
    draw_number(fb, stride, 5, POCKET_saves_failed);
    draw_number(fb, stride, 6, calls);
    for (int i = 0; i < 6; i++)
    {
        draw_number(fb, stride, 7 + i, parts[i]);
    }
    draw_number(fb, stride, 14, bench[0]);
    draw_number(fb, stride, 15, bench[1]);
    draw_number(fb, stride, 16, bench[2]);
}
#endif

/* Until the first frame is shown the game draws into memory, so the boot
 * console stays on screen while it loads. */
static Uint16 first_frame[POCKET_SCREEN_W * POCKET_SCREEN_H];
static bool video_started;

Uint16 *POCKET_FrameBuffer(int *pitch)
{
    if (!video_started)
    {
        *pitch = POCKET_SCREEN_W;
        return first_frame;
    }
    *pitch = (video_mode.stride ? video_mode.stride : POCKET_SCREEN_W * 2) / 2;
    return (Uint16 *) of_video_surface();
}

void POCKET_PresentFrame(void)
{
    if (!video_started)
    {
#ifdef POCKET_SHOW_FPS
        printf("%5u first frame\n", (unsigned) of_time_ms());
#endif
        video_started = true;
        start_video();
        Uint8 *fb = of_video_surface();
        const int stride = video_mode.stride ? video_mode.stride : POCKET_SCREEN_W * 2;
        for (int y = 0; y < POCKET_SCREEN_H; y++)
        {
            SDL_memcpy(fb + (size_t) y * stride, first_frame + (size_t) y * POCKET_SCREEN_W, POCKET_SCREEN_W * 2);
        }
    }
#ifdef POCKET_SHOW_FPS
    const Uint32 present_start = of_time_us();
    draw_profile(of_video_surface(), video_mode.stride ? video_mode.stride : POCKET_SCREEN_W * 2);
#endif
    of_video_flip();
#ifdef POCKET_SHOW_FPS
    present_us += of_time_us() - present_start;
#endif
    pocket_audio_update();
}

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{
    (void) title;
    (void) w;
    (void) h;
    (void) flags;
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
