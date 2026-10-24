/* Drawing that the renderer test runs twice: once on desktop SDL3's own
 * software renderer and once, with every SDL call given a PR_ prefix, on the
 * port's renderer (sdl3/render.c and sdl3/surface.c).  Each step appends
 * what it produced to the capture, and render_test.c compares the two. */
#include <SDL3/SDL.h>

#include "render_test.h"

#ifdef SDL_RenderClear
bool POCKET_ScrollTextureRows(SDL_Texture *texture, int dy);
bool POCKET_ShiftTextureRows(SDL_Texture *texture, int dy);
#endif

static void capture_target(SDL_Renderer *renderer, Capture *capture, const char *name)
{
    SDL_Surface *pixels = SDL_RenderReadPixels(renderer, NULL);
    if (SDL_GetRenderTarget(renderer) == NULL)
    {
        capture_window(capture, name, pixels);
    }
    else
    {
        capture_layer(capture, name, pixels);
    }
    SDL_DestroySurface(pixels);
}

/* A 16x16 sprite with opaque, clear and part-transparent pixels, loaded
 * the way GraphicsResources.cpp loads a PNG. */
static SDL_Texture *make_sprite(SDL_Renderer *renderer)
{
    static Uint8 rgba[16 * 16 * 4];
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            Uint8 *p = rgba + (y * 16 + x) * 4;
            p[0] = (Uint8) (x * 16);
            p[1] = (Uint8) (y * 16);
            p[2] = (Uint8) (255 - x * 8 - y * 4);
            p[3] = (x + y) % 5 == 0 ? 0 : (x < 8 ? 255 : (Uint8) (y * 16 + 7));
        }
    }
    SDL_Surface *loaded = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_ABGR8888, rgba, 16 * 4);
    SDL_Surface *converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888);
    SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_BLEND);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, converted);
    SDL_DestroySurface(converted);
    SDL_DestroySurface(loaded);
    return texture;
}

static void draw_sprite(SDL_Renderer *renderer, SDL_Texture *sprite, float x, float y, float w, float h, SDL_FlipMode flip)
{
    const SDL_FRect dst = {x, y, w, h};
    SDL_RenderTextureRotated(renderer, sprite, NULL, &dst, 0, NULL, flip);
}

/* The alpha primitives and sprites clear to: just short of opaque on a
 * target texture, so the port keeps it in ARGB8888 and must match SDL
 * exactly. */
static Uint8 clear_alpha = 255;

static void primitives(SDL_Renderer *renderer, Capture *capture)
{
    SDL_SetRenderDrawColor(renderer, 10, 20, 30, clear_alpha);
    SDL_RenderClear(renderer);

    const SDL_FRect opaque = {5, 5, 40, 30};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 200, 100, 50, 255);
    SDL_RenderFillRect(renderer, &opaque);

    const SDL_FRect translucent = {25, 20, 60, 40};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 255, 128, 128);
    SDL_RenderFillRect(renderer, &translucent);

    const SDL_FRect replaced = {100, 10, 20, 20};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 90, 80, 70, 60);
    SDL_RenderFillRect(renderer, &replaced);

    const SDL_FRect outline = {130, 10, 50, 25};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &outline);

    const SDL_FRect off_screen = {300, 220, 50, 50};
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderFillRect(renderer, &off_screen);

    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    SDL_RenderLine(renderer, 10, 100, 110, 100);
    SDL_RenderLine(renderer, 10, 110, 10, 200);
    SDL_RenderLine(renderer, 20, 110, 60, 130);
    SDL_RenderLine(renderer, 20, 140, 40, 200);
    SDL_RenderLine(renderer, 100, 200, 60, 150);

    SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
    SDL_RenderPoint(renderer, 200, 100);
    const SDL_FPoint points[3] = {{201, 101}, {202, 102}, {319, 239}};
    SDL_RenderPoints(renderer, points, 3);

    capture_target(renderer, capture, "primitives");
}

static void sprites(SDL_Renderer *renderer, SDL_Texture *sprite, Capture *capture)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 40, 60, 80, clear_alpha);
    SDL_RenderClear(renderer);

    draw_sprite(renderer, sprite, 0, 0, 16, 16, SDL_FLIP_NONE);
    draw_sprite(renderer, sprite, 20, 0, 16, 16, SDL_FLIP_HORIZONTAL);
    draw_sprite(renderer, sprite, 40, 0, 16, 16, SDL_FLIP_VERTICAL);
    draw_sprite(renderer, sprite, 60, 0, 16, 16, (SDL_FlipMode) (SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
    draw_sprite(renderer, sprite, 80, 0, 32, 32, SDL_FLIP_NONE);
    draw_sprite(renderer, sprite, 120, 0, 48, 16, SDL_FLIP_HORIZONTAL);

    /* Hanging off each edge. */
    draw_sprite(renderer, sprite, -5, 40, 16, 16, SDL_FLIP_NONE);
    draw_sprite(renderer, sprite, 310, 40, 16, 16, SDL_FLIP_HORIZONTAL);
    draw_sprite(renderer, sprite, 100, -7, 16, 16, SDL_FLIP_VERTICAL);
    draw_sprite(renderer, sprite, 100, 233, 32, 32, SDL_FLIP_NONE);

    /* Part of the sprite, as the tile and font drawing does. */
    const SDL_FRect part = {4, 2, 8, 6};
    const SDL_FRect part_dst = {200, 40, 8, 6};
    SDL_RenderTexture(renderer, sprite, &part, &part_dst);
    const SDL_FRect part_big = {220, 40, 16, 12};
    SDL_RenderTexture(renderer, sprite, &part, &part_big);
    const SDL_FRect overhang = {12, 12, 8, 8};
    const SDL_FRect overhang_dst = {240, 40, 16, 16};
    SDL_RenderTexture(renderer, sprite, &overhang, &overhang_dst);

    /* Colour and alpha mods, as text and entity colours are drawn. */
    SDL_SetTextureColorMod(sprite, 255, 128, 0);
    draw_sprite(renderer, sprite, 0, 60, 16, 16, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(sprite, 100);
    draw_sprite(renderer, sprite, 20, 60, 16, 16, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(sprite, 255, 255, 255);
    draw_sprite(renderer, sprite, 40, 60, 16, 16, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(sprite, 255);

    /* Every blend mode, with and without a mod. */
    static const SDL_BlendMode modes[] = {
        SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_ADD,
        SDL_BLENDMODE_MOD, SDL_BLENDMODE_MUL
    };
    for (int i = 0; i < 5; i++)
    {
        SDL_SetTextureBlendMode(sprite, modes[i]);
        draw_sprite(renderer, sprite, (float) (i * 20), 90, 16, 16, SDL_FLIP_NONE);
        SDL_SetTextureColorMod(sprite, 200, 150, 100);
        SDL_SetTextureAlphaMod(sprite, 180);
        draw_sprite(renderer, sprite, (float) (i * 20), 110, 16, 16, SDL_FLIP_NONE);
        SDL_SetTextureColorMod(sprite, 255, 255, 255);
        SDL_SetTextureAlphaMod(sprite, 255);
    }
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);

    capture_target(renderer, capture, "sprites");
}

/* The game draws each layer into an ARGB8888 target texture and stacks
 * them; the targets keep the default blend mode SDL gives them. */
static void targets(SDL_Renderer *renderer, SDL_Texture *sprite, Capture *capture)
{
    SDL_Texture *layer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);

    SDL_SetRenderTarget(renderer, layer);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    draw_sprite(renderer, sprite, 8, 8, 64, 64, SDL_FLIP_NONE);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 90);
    const SDL_FRect veil = {30, 30, 80, 80};
    SDL_RenderFillRect(renderer, &veil);
    capture_target(renderer, capture, "layer");

    SDL_SetRenderTarget(renderer, screen);
    SDL_SetRenderDrawColor(renderer, 30, 30, 90, 255);
    SDL_RenderClear(renderer);
    const SDL_FRect scroll_src = {8, 8, 320, 240};
    SDL_RenderTexture(renderer, layer, &scroll_src, NULL);
    capture_target(renderer, capture, "screen");

    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    const SDL_FRect whole = {0, 0, 320, 240};
    SDL_RenderTextureRotated(renderer, screen, NULL, &whole, 0, NULL, SDL_FLIP_VERTICAL);
    capture_target(renderer, capture, "flipped present");

    SDL_SetTextureBlendMode(layer, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(renderer, layer, NULL, NULL);
    capture_target(renderer, capture, "layer opaque");

    /* The bad signal filter's round trip through surfaces, which reads
     * the game's own layer. */
    SDL_Texture *game = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_SetRenderTarget(renderer, game);
    SDL_RenderTexture(renderer, layer, NULL, NULL);
    SDL_Surface *read = SDL_RenderReadPixels(renderer, NULL);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(game);
    SDL_Surface *work = SDL_CreateSurface(320, 240, SDL_GetPixelFormatForMasks(32, 0, 0, 0, 0));
    SDL_BlitSurface(read, NULL, work, NULL);
    capture_surface(capture, "read into XRGB8888", work);
    SDL_UpdateTexture(screen, NULL, work->pixels, work->pitch);
    SDL_DestroySurface(read);
    SDL_DestroySurface(work);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, screen, NULL, NULL);
    capture_target(renderer, capture, "updated texture");

    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    capture_number(capture, "output width", w);
    capture_number(capture, "output height", h);
    SDL_PropertiesID props = SDL_GetTextureProperties(layer);
    capture_number(capture, "texture width", (int) SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0));
    capture_number(capture, "texture height", (int) SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0));
    capture_number(capture, "texture access", (int) SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_ACCESS_NUMBER, 0));
    capture_number(capture, "target after set", SDL_GetRenderTarget(renderer) == NULL);

    SDL_DestroyTexture(screen);
    SDL_DestroyTexture(layer);
}

/* Graphics::scroll_texture moving an opaque layer up or down: SDL clears
 * a second texture, copies the layer onto it at an offset and copies it
 * back; the port only changes which row it shows first. */
static void scroll_rows(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Texture *temp, int dy)
{
#ifdef SDL_RenderClear
    (void) renderer;
    (void) temp;
    POCKET_ScrollTextureRows(texture, dy);
#else
    SDL_Texture *target = SDL_GetRenderTarget(renderer);
    const SDL_FRect src = {0, 0, 96, 64};
    const SDL_FRect dst = {0, (float) dy, 96, 64};
    SDL_SetRenderTarget(renderer, temp);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, &src, &dst);
    SDL_SetRenderTarget(renderer, target);
    SDL_RenderTexture(renderer, temp, &src, &src);
#endif
}

/* The tower foreground cache: rows shifted with what they expose made
 * transparent; SDL does the same through a cleared texture, unblended. */
static void shift_rows(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Texture *temp, int dy)
{
#ifdef SDL_RenderClear
    (void) renderer;
    (void) temp;
    POCKET_ShiftTextureRows(texture, dy);
#else
    SDL_Texture *target = SDL_GetRenderTarget(renderer);
    const SDL_FRect src = {0, 0, 96, 64};
    const SDL_FRect dst = {0, (float) dy, 96, 64};
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(temp, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, temp);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, &src, &dst);
    SDL_SetRenderTarget(renderer, texture);
    SDL_RenderTexture(renderer, temp, &src, &src);
    SDL_SetRenderTarget(renderer, target);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(temp, SDL_BLENDMODE_BLEND);
#endif
}

static void tower_cache(SDL_Renderer *renderer, SDL_Texture *sprite, Capture *capture)
{
    SDL_Texture *cache = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 64);
    SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 64);
    SDL_Texture *screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 56);
    SDL_SetTextureBlendMode(cache, SDL_BLENDMODE_BLEND);

    SDL_SetRenderTarget(renderer, cache);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_NONE);
    for (int i = 0; i < 6; i++)
    {
        draw_sprite(renderer, sprite, (float) (i * 16), (float) (i * 8), 16, 16, SDL_FLIP_NONE);
    }
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
    for (int step = 0; step < 3; step++)
    {
        shift_rows(renderer, cache, temp, step == 1 ? 16 : -8);
        SDL_SetRenderTarget(renderer, screen);
        SDL_SetRenderDrawColor(renderer, 0, 90, 40, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect shifted = {0, 8, 96, 56};
        SDL_RenderTexture(renderer, cache, &shifted, NULL);
        capture_target(renderer, capture, "tower cache shifted");
        SDL_SetRenderTarget(renderer, cache);
        SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_NONE);
        draw_sprite(renderer, sprite, (float) (step * 24), step == 1 ? 0.0f : 48.0f, 16, 16, SDL_FLIP_NONE);
        SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
        for (int copy = 0; copy < 2; copy++)
        {
            SDL_SetRenderTarget(renderer, screen);
            SDL_SetRenderDrawColor(renderer, 60, 20, 90, 255);
            SDL_RenderClear(renderer);
            const SDL_FRect part = {0, (float) (3 + step), 96, 56};
            SDL_RenderTexture(renderer, cache, &part, NULL);
            capture_target(renderer, capture, step == 0 ? "tower cache up" : step == 1 ? "tower cache down" : "tower cache up again");
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(screen);
    SDL_DestroyTexture(temp);
    SDL_DestroyTexture(cache);
}

static void scrolling(SDL_Renderer *renderer, SDL_Texture *sprite, Capture *capture)
{
    SDL_Texture *tower = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 64);
    SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 64);
    SDL_Texture *shared = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 64);

    SDL_SetRenderTarget(renderer, tower);
    SDL_SetRenderDrawColor(renderer, 40, 90, 140, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    for (int i = 0; i < 8; i++)
    {
        SDL_SetRenderDrawColor(renderer, (Uint8) (i * 30), 200, (Uint8) (255 - i * 20), 255);
        const SDL_FRect band = {(float) (i * 5), (float) (i * 8), 50, 4};
        SDL_RenderFillRect(renderer, &band);
    }
    scroll_rows(renderer, tower, temp, 5);
    draw_sprite(renderer, sprite, 4, 0, 16, 16, SDL_FLIP_NONE);
    capture_target(renderer, capture, "scrolled down");

    scroll_rows(renderer, tower, temp, -13);
    SDL_SetRenderDrawColor(renderer, 250, 250, 0, 255);
    const SDL_FRect edge = {60, 50, 30, 14};
    SDL_RenderFillRect(renderer, &edge);
    capture_target(renderer, capture, "scrolled up");

    /* Shared whole, then scrolled: the copy keeps the rows it was given. */
    SDL_SetRenderTarget(renderer, shared);
    SDL_RenderTexture(renderer, tower, NULL, NULL);
    SDL_SetRenderTarget(renderer, tower);
    for (int i = 0; i < 9; i++)
    {
        scroll_rows(renderer, tower, temp, -3);
    }
    capture_target(renderer, capture, "scrolled past the wrap");
    SDL_SetRenderTarget(renderer, shared);
    capture_target(renderer, capture, "shared before scrolling");

    /* Copied out in parts, as drawtowerbackground does. */
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    const SDL_FRect part = {10, 7, 70, 50};
    const SDL_FRect placed = {100, 20, 70, 50};
    SDL_RenderTexture(renderer, tower, &part, &placed);
    SDL_RenderTexture(renderer, shared, NULL, NULL);
    capture_target(renderer, capture, "scrolled copied out");

    SDL_DestroyTexture(shared);
    SDL_DestroyTexture(temp);
    SDL_DestroyTexture(tower);
}

/* One game frame as Render.cpp and Graphics.cpp draw it: a cached
 * foreground layer copied over a filled background, the gameplay layer
 * copied onto the game layer and the game layer onto the cleared window.
 * Run twice, so the second frame uses whatever the first one cached. */
static void frame(SDL_Renderer *renderer, SDL_Texture *sprite, SDL_Texture *foreground,
                  SDL_Texture *gameplay, SDL_Texture *game, int number, Capture *capture)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderTarget(renderer, gameplay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 40, 40, 255);
    SDL_RenderFillRect(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 85, 85, 85, 255);
    const SDL_FRect star = {(float) (40 + number * 7), 50, 2, 2};
    SDL_RenderFillRect(renderer, &star);
    SDL_RenderTexture(renderer, foreground, NULL, NULL);
    SDL_SetTextureAlphaMod(foreground, 128);
    const SDL_FRect shadow = {3, 2, 320, 240};
    SDL_RenderTexture(renderer, foreground, NULL, &shadow);
    SDL_SetTextureAlphaMod(foreground, 255);
    SDL_SetTextureColorMod(sprite, 255, 120, 40);
    draw_sprite(renderer, sprite, (float) (100 + number * 3), 120, 16, 16, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(sprite, 255, 255, 255);

    SDL_SetRenderTarget(renderer, game);
    SDL_RenderTexture(renderer, gameplay, NULL, NULL);
    const SDL_FRect bar = {0, 0, 320, 16};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &bar);

    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTextureRotated(renderer, game, NULL, NULL, 0, NULL, number ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
    capture_target(renderer, capture, number ? "second frame" : "first frame");
}

static void frames(SDL_Renderer *renderer, SDL_Texture *sprite, Capture *capture)
{
    SDL_Texture *foreground = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_Texture *gameplay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_Texture *game = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);

    SDL_SetRenderTarget(renderer, foreground);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    for (int i = 0; i < 12; i++)
    {
        draw_sprite(renderer, sprite, (float) (i * 26), (float) (i * 19), 16, 16, SDL_FLIP_NONE);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 200, 30, 30, 255);
    const SDL_FRect wall = {0, 224, 320, 16};
    SDL_RenderFillRect(renderer, &wall);

    frame(renderer, sprite, foreground, gameplay, game, 0, capture);
    frame(renderer, sprite, foreground, gameplay, game, 1, capture);

    /* The game layer copied whole from the gameplay layer, which is then
     * redrawn: the game layer must keep the old picture. */
    SDL_SetRenderTarget(renderer, game);
    SDL_RenderTexture(renderer, gameplay, NULL, NULL);
    SDL_SetRenderTarget(renderer, gameplay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 60, 0, 90, 255);
    SDL_RenderFillRect(renderer, NULL);
    draw_sprite(renderer, sprite, 30, 30, 16, 16, SDL_FLIP_NONE);
    capture_target(renderer, capture, "redrawn gameplay");
    SDL_SetRenderTarget(renderer, game);
    capture_target(renderer, capture, "kept game layer");
    SDL_RenderTexture(renderer, gameplay, NULL, NULL);
    SDL_SetRenderTarget(renderer, gameplay);
    const SDL_FRect corner = {0, 0, 40, 40};
    SDL_RenderFillRect(renderer, &corner);
    SDL_SetRenderTarget(renderer, game);
    capture_target(renderer, capture, "game after partial");

    /* A held back clear, then part of it drawn over. */
    SDL_SetRenderTarget(renderer, foreground);
    SDL_SetRenderDrawColor(renderer, 9, 8, 7, 100);
    SDL_RenderClear(renderer);
    draw_sprite(renderer, sprite, 50, 50, 32, 32, SDL_FLIP_HORIZONTAL);
    capture_target(renderer, capture, "drawn after clear");
    SDL_SetRenderTarget(renderer, game);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 20, 30, 40, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, foreground, NULL, NULL);
    SDL_RenderTexture(renderer, game, NULL, NULL);
    capture_target(renderer, capture, "translucent layers");

    SDL_DestroyTexture(game);
    SDL_DestroyTexture(gameplay);
    SDL_DestroyTexture(foreground);
}

static void surfaces(Capture *capture)
{
    SDL_Surface *source = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888);
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            const SDL_Color c = {(Uint8) (x * 32), (Uint8) (y * 32), 99, (Uint8) ((x + y) * 18)};
            ((Uint32 *) source->pixels)[y * 8 + x] = SDL_MapSurfaceRGBA(source, c.r, c.g, c.b, c.a);
        }
    }
    static const SDL_PixelFormat formats[] = {
        SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_RGB24
    };
    static const char *names[] = {"blit to ARGB8888", "blit to XRGB8888", "blit to ABGR8888", "blit to RGB24"};
    for (int i = 0; i < 4; i++)
    {
        SDL_Surface *dst = SDL_CreateSurface(12, 10, formats[i]);
        const SDL_Rect area = {1, 1, 6, 6};
        const SDL_Rect at = {3, 2, 0, 0};
        SDL_BlitSurface(source, NULL, dst, NULL);
        SDL_BlitSurface(source, &area, dst, &at);
        capture_surface(capture, names[i], dst);
        SDL_Surface *big = SDL_CreateSurface(16, 16, formats[i]);
        SDL_BlitSurfaceScaled(dst, NULL, big, NULL, SDL_SCALEMODE_NEAREST);
        capture_surface(capture, "scaled 2x", big);
        SDL_DestroySurface(big);
        capture_number(capture, "map RGB", (int) SDL_MapSurfaceRGB(dst, 1, 2, 3));
        capture_number(capture, "map RGBA", (int) SDL_MapSurfaceRGBA(dst, 1, 2, 3, 4));
        SDL_BlendMode blend;
        SDL_GetSurfaceBlendMode(dst, &blend);
        capture_number(capture, "default blend", (int) blend);
        SDL_DestroySurface(dst);
    }
    SDL_Surface *opaque = SDL_ConvertSurface(source, SDL_PIXELFORMAT_ABGR8888);
    capture_surface(capture, "converted", opaque);
    SDL_DestroySurface(opaque);
    SDL_DestroySurface(source);
}

void RENDER_OPS(SDL_Renderer *renderer, Capture *capture)
{
    /* As Screen.cpp does; SDL's default is linear. */
    SDL_SetDefaultTextureScaleMode(renderer, SDL_SCALEMODE_NEAREST);
    SDL_Texture *sprite = make_sprite(renderer);
    /* On a target texture, exactly as SDL; then on the window. */
    SDL_Texture *canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_SetRenderTarget(renderer, canvas);
    clear_alpha = 254;
    primitives(renderer, capture);
    sprites(renderer, sprite, capture);
    clear_alpha = 255;
    /* An opaque target given translucent pixels: the port widens what it
     * kept in RGB565. */
    SDL_SetRenderDrawColor(renderer, 10, 20, 30, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 90, 80, 70, 60);
    const SDL_FRect hole = {100, 10, 20, 20};
    SDL_RenderFillRect(renderer, &hole);
    draw_sprite(renderer, sprite, 30, 30, 16, 16, SDL_FLIP_NONE);
    SDL_Surface *widened = SDL_RenderReadPixels(renderer, NULL);
    capture_window(capture, "opaque made translucent", widened);
    SDL_DestroySurface(widened);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(canvas);
    primitives(renderer, capture);
    sprites(renderer, sprite, capture);
    targets(renderer, sprite, capture);
    scrolling(renderer, sprite, capture);
    tower_cache(renderer, sprite, capture);
    frames(renderer, sprite, capture);
    surfaces(capture);
    SDL_DestroyTexture(sprite);
}
