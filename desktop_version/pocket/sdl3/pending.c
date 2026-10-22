/* Placeholders for the SDL3 video, surface, event and gamepad calls.
 *
 * Phase 1 of the port only needs the game to link for the Pocket; the host
 * build uses desktop SDL3.  The platform layer (zenibako/VVVVVV#3) and the
 * software renderer (zenibako/VVVVVV#4) replace these.  Each one fails
 * cleanly so a call is visible in the log rather than undefined. */
#include <SDL3/SDL.h>

int SDL_AddGamepadMappingsFromFile(const char *file)
{
    (void) file;
    return 0;
}

bool SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, const SDL_Rect *dstrect)
{
    (void) src;
    (void) srcrect;
    (void) dst;
    (void) dstrect;
    return SDL_SetError("SDL_BlitSurface is not available yet");
}

bool SDL_BlitSurfaceScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, const SDL_Rect *dstrect, SDL_ScaleMode scaleMode)
{
    (void) src;
    (void) srcrect;
    (void) dst;
    (void) dstrect;
    (void) scaleMode;
    return SDL_SetError("SDL_BlitSurfaceScaled is not available yet");
}

void SDL_CloseGamepad(SDL_Gamepad *gamepad)
{
    (void) gamepad;
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *surface, SDL_PixelFormat format)
{
    (void) surface;
    (void) format;
    SDL_SetError("SDL_ConvertSurface is not available yet");
    return NULL;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name)
{
    (void) window;
    (void) name;
    SDL_SetError("SDL_CreateRenderer is not available yet");
    return NULL;
}

SDL_Surface *SDL_CreateSurface(int width, int height, SDL_PixelFormat format)
{
    (void) width;
    (void) height;
    (void) format;
    SDL_SetError("SDL_CreateSurface is not available yet");
    return NULL;
}

SDL_Surface *SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void *pixels, int pitch)
{
    (void) width;
    (void) height;
    (void) format;
    (void) pixels;
    (void) pitch;
    SDL_SetError("SDL_CreateSurfaceFrom is not available yet");
    return NULL;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, SDL_PixelFormat format, SDL_TextureAccess access, int w, int h)
{
    (void) renderer;
    (void) format;
    (void) access;
    (void) w;
    (void) h;
    SDL_SetError("SDL_CreateTexture is not available yet");
    return NULL;
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
    (void) renderer;
    (void) surface;
    SDL_SetError("SDL_CreateTextureFromSurface is not available yet");
    return NULL;
}

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{
    (void) title;
    (void) w;
    (void) h;
    (void) flags;
    SDL_SetError("SDL_CreateWindow is not available yet");
    return NULL;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    (void) renderer;
}

void SDL_DestroySurface(SDL_Surface *surface)
{
    (void) surface;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    (void) texture;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    (void) window;
}

bool SDL_GetCurrentRenderOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    (void) renderer;
    (void) w;
    (void) h;
    return SDL_SetError("SDL_GetCurrentRenderOutputSize is not available yet");
}

const char *SDL_GetCurrentVideoDriver(void)
{
    SDL_SetError("SDL_GetCurrentVideoDriver is not available yet");
    return NULL;
}

const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID displayID)
{
    (void) displayID;
    SDL_SetError("SDL_GetDesktopDisplayMode is not available yet");
    return NULL;
}

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *window)
{
    (void) window;
    return 0;
}

SDL_Joystick *SDL_GetGamepadJoystick(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    SDL_SetError("SDL_GetGamepadJoystick is not available yet");
    return NULL;
}

const char *SDL_GetGamepadName(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    SDL_SetError("SDL_GetGamepadName is not available yet");
    return NULL;
}

Uint16 SDL_GetGamepadProduct(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return 0;
}

SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return 0;
}

Uint16 SDL_GetGamepadVendor(SDL_Gamepad *gamepad)
{
    (void) gamepad;
    return 0;
}

SDL_JoystickID SDL_GetJoystickID(SDL_Joystick *joystick)
{
    (void) joystick;
    return 0;
}

const SDL_PixelFormatDetails *SDL_GetPixelFormatDetails(SDL_PixelFormat format)
{
    (void) format;
    SDL_SetError("SDL_GetPixelFormatDetails is not available yet");
    return NULL;
}

SDL_PixelFormat SDL_GetPixelFormatForMasks(int bpp, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    (void) bpp;
    (void) Rmask;
    (void) Gmask;
    (void) Bmask;
    (void) Amask;
    return 0;
}

void SDL_GetRGB(Uint32 pixelvalue, const SDL_PixelFormatDetails *format, const SDL_Palette *palette, Uint8 *r, Uint8 *g, Uint8 *b)
{
    (void) pixelvalue;
    (void) format;
    (void) palette;
    (void) r;
    (void) g;
    (void) b;
}

void SDL_GetRGBA(Uint32 pixelvalue, const SDL_PixelFormatDetails *format, const SDL_Palette *palette, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    (void) pixelvalue;
    (void) format;
    (void) palette;
    (void) r;
    (void) g;
    (void) b;
    (void) a;
}

SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *renderer)
{
    (void) renderer;
    SDL_SetError("SDL_GetRenderTarget is not available yet");
    return NULL;
}

bool SDL_GetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode *blendMode)
{
    (void) surface;
    (void) blendMode;
    return SDL_SetError("SDL_GetSurfaceBlendMode is not available yet");
}

SDL_Palette *SDL_GetSurfacePalette(SDL_Surface *surface)
{
    (void) surface;
    SDL_SetError("SDL_GetSurfacePalette is not available yet");
    return NULL;
}

bool SDL_GetTextureAlphaMod(SDL_Texture *texture, Uint8 *alpha)
{
    (void) texture;
    (void) alpha;
    return SDL_SetError("SDL_GetTextureAlphaMod is not available yet");
}

bool SDL_GetTextureColorMod(SDL_Texture *texture, Uint8 *r, Uint8 *g, Uint8 *b)
{
    (void) texture;
    (void) r;
    (void) g;
    (void) b;
    return SDL_SetError("SDL_GetTextureColorMod is not available yet");
}

SDL_PropertiesID SDL_GetTextureProperties(SDL_Texture *texture)
{
    (void) texture;
    return 0;
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window)
{
    (void) window;
    return 0;
}

SDL_Window *SDL_GetWindowFromID(SDL_WindowID id)
{
    (void) id;
    SDL_SetError("SDL_GetWindowFromID is not available yet");
    return NULL;
}

bool SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    (void) window;
    (void) w;
    (void) h;
    return SDL_SetError("SDL_GetWindowSize is not available yet");
}

bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h)
{
    (void) window;
    (void) w;
    (void) h;
    return SDL_SetError("SDL_GetWindowSizeInPixels is not available yet");
}

bool SDL_LockSurface(SDL_Surface *surface)
{
    (void) surface;
    return SDL_SetError("SDL_LockSurface is not available yet");
}

Uint32 SDL_MapSurfaceRGB(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b)
{
    (void) surface;
    (void) r;
    (void) g;
    (void) b;
    return 0;
}

Uint32 SDL_MapSurfaceRGBA(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void) surface;
    (void) r;
    (void) g;
    (void) b;
    (void) a;
    return 0;
}

bool SDL_MinimizeWindow(SDL_Window *window)
{
    (void) window;
    return SDL_SetError("SDL_MinimizeWindow is not available yet");
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID instance_id)
{
    (void) instance_id;
    SDL_SetError("SDL_OpenGamepad is not available yet");
    return NULL;
}

bool SDL_PollEvent(SDL_Event *event)
{
    (void) event;
    return SDL_SetError("SDL_PollEvent is not available yet");
}

bool SDL_RenderClear(SDL_Renderer *renderer)
{
    (void) renderer;
    return SDL_SetError("SDL_RenderClear is not available yet");
}

bool SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rect)
{
    (void) renderer;
    (void) rect;
    return SDL_SetError("SDL_RenderFillRect is not available yet");
}

bool SDL_RenderLine(SDL_Renderer *renderer, float x1, float y1, float x2, float y2)
{
    (void) renderer;
    (void) x1;
    (void) y1;
    (void) x2;
    (void) y2;
    return SDL_SetError("SDL_RenderLine is not available yet");
}

bool SDL_RenderPoint(SDL_Renderer *renderer, float x, float y)
{
    (void) renderer;
    (void) x;
    (void) y;
    return SDL_SetError("SDL_RenderPoint is not available yet");
}

bool SDL_RenderPoints(SDL_Renderer *renderer, const SDL_FPoint *points, int count)
{
    (void) renderer;
    (void) points;
    (void) count;
    return SDL_SetError("SDL_RenderPoints is not available yet");
}

bool SDL_RenderPresent(SDL_Renderer *renderer)
{
    (void) renderer;
    return SDL_SetError("SDL_RenderPresent is not available yet");
}

SDL_Surface *SDL_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    (void) renderer;
    (void) rect;
    SDL_SetError("SDL_RenderReadPixels is not available yet");
    return NULL;
}

bool SDL_RenderRect(SDL_Renderer *renderer, const SDL_FRect *rect)
{
    (void) renderer;
    (void) rect;
    return SDL_SetError("SDL_RenderRect is not available yet");
}

bool SDL_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect *srcrect, const SDL_FRect *dstrect)
{
    (void) renderer;
    (void) texture;
    (void) srcrect;
    (void) dstrect;
    return SDL_SetError("SDL_RenderTexture is not available yet");
}

bool SDL_RenderTextureRotated(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect *srcrect, const SDL_FRect *dstrect, double angle, const SDL_FPoint *center, SDL_FlipMode flip)
{
    (void) renderer;
    (void) texture;
    (void) srcrect;
    (void) dstrect;
    (void) angle;
    (void) center;
    (void) flip;
    return SDL_SetError("SDL_RenderTextureRotated is not available yet");
}

bool SDL_SetDefaultTextureScaleMode(SDL_Renderer *renderer, SDL_ScaleMode scale_mode)
{
    (void) renderer;
    (void) scale_mode;
    return SDL_SetError("SDL_SetDefaultTextureScaleMode is not available yet");
}

bool SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    (void) renderer;
    (void) blendMode;
    return SDL_SetError("SDL_SetRenderDrawBlendMode is not available yet");
}

bool SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void) renderer;
    (void) r;
    (void) g;
    (void) b;
    (void) a;
    return SDL_SetError("SDL_SetRenderDrawColor is not available yet");
}

bool SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void) renderer;
    (void) texture;
    return SDL_SetError("SDL_SetRenderTarget is not available yet");
}

bool SDL_SetRenderVSync(SDL_Renderer *renderer, int vsync)
{
    (void) renderer;
    (void) vsync;
    return SDL_SetError("SDL_SetRenderVSync is not available yet");
}

bool SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode)
{
    (void) surface;
    (void) blendMode;
    return SDL_SetError("SDL_SetSurfaceBlendMode is not available yet");
}

bool SDL_SetTextureAlphaMod(SDL_Texture *texture, Uint8 alpha)
{
    (void) texture;
    (void) alpha;
    return SDL_SetError("SDL_SetTextureAlphaMod is not available yet");
}

bool SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode)
{
    (void) texture;
    (void) blendMode;
    return SDL_SetError("SDL_SetTextureBlendMode is not available yet");
}

bool SDL_SetTextureColorMod(SDL_Texture *texture, Uint8 r, Uint8 g, Uint8 b)
{
    (void) texture;
    (void) r;
    (void) g;
    (void) b;
    return SDL_SetError("SDL_SetTextureColorMod is not available yet");
}

bool SDL_SetTextureScaleMode(SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    (void) texture;
    (void) scaleMode;
    return SDL_SetError("SDL_SetTextureScaleMode is not available yet");
}

bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
    (void) window;
    (void) fullscreen;
    return SDL_SetError("SDL_SetWindowFullscreen is not available yet");
}

bool SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon)
{
    (void) window;
    (void) icon;
    return SDL_SetError("SDL_SetWindowIcon is not available yet");
}

bool SDL_SetWindowMinimumSize(SDL_Window *window, int min_w, int min_h)
{
    (void) window;
    (void) min_w;
    (void) min_h;
    return SDL_SetError("SDL_SetWindowMinimumSize is not available yet");
}

bool SDL_SetWindowPosition(SDL_Window *window, int x, int y)
{
    (void) window;
    (void) x;
    (void) y;
    return SDL_SetError("SDL_SetWindowPosition is not available yet");
}

bool SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
    (void) window;
    (void) w;
    (void) h;
    return SDL_SetError("SDL_SetWindowSize is not available yet");
}

bool SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    (void) window;
    (void) title;
    return SDL_SetError("SDL_SetWindowTitle is not available yet");
}

bool SDL_ShowWindow(SDL_Window *window)
{
    (void) window;
    return SDL_SetError("SDL_ShowWindow is not available yet");
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
    (void) surface;
}

bool SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    (void) texture;
    (void) rect;
    (void) pixels;
    (void) pitch;
    return SDL_SetError("SDL_UpdateTexture is not available yet");
}
