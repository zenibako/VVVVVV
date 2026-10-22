/* Shows the port's renderer in a desktop window, for `make host-render`.
 *
 * That build draws the game with sdl3/render.c and sdl3/surface.c (their
 * SDL calls given a PR_ prefix) and leaves windows, input and audio to
 * desktop SDL3.  Each finished frame is the one the Pocket would show,
 * scaled up to the window. */
#include <SDL3/SDL.h>

#include "render_internal.h"

void POCKET_PresentFrame(const Uint32 *argb)
{
    static SDL_Renderer *renderer;
    static SDL_Texture *frame;
    if (renderer == NULL)
    {
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (windows == NULL || count == 0)
        {
            SDL_free(windows);
            return;
        }
        renderer = SDL_CreateRenderer(windows[0], NULL);
        SDL_free(windows);
        if (renderer == NULL)
        {
            SDL_Log("host-render: %s", SDL_GetError());
            return;
        }
        SDL_SetRenderLogicalPresentation(renderer, POCKET_SCREEN_W, POCKET_SCREEN_H,
                                         SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  POCKET_SCREEN_W, POCKET_SCREEN_H);
        SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST);
    }
    SDL_UpdateTexture(frame, NULL, argb, POCKET_SCREEN_W * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, frame, NULL, NULL);
    SDL_RenderPresent(renderer);
}
