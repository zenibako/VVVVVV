/* Drawing that the renderer test runs twice: once on desktop SDL3's own
 * software renderer and once, with every SDL call given a PR_ prefix, on the
 * port's renderer (sdl3/render.c and sdl3/surface.c).  Each step appends
 * what it produced to the capture, and render_test.c compares the two. */
#include <SDL3/SDL.h>
#include <limits.h>
#include <assert.h>

#include "render_test.h"

#ifdef SDL_RenderClear
bool POCKET_ScrollTexture(SDL_Texture *texture, int dx, int dy);
bool POCKET_RepeatTexture(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*);
bool POCKET_CopyOpaqueGrid(SDL_Renderer*, SDL_Texture*, const Uint16*, int, int, int, int);
bool POCKET_CopyKeyedGrid(SDL_Renderer*, SDL_Texture*, const int*, int, int);
void POCKET_SetTextureRuns(SDL_Texture*, bool);
bool POCKET_ShiftTextureRows(SDL_Texture *texture, int dy);
typedef struct POCKET_TileLayer POCKET_TileLayer;
POCKET_TileLayer *POCKET_CreateTileLayer(int w, int h);
void POCKET_DestroyTileLayer(POCKET_TileLayer *layer);
bool POCKET_TileSheetIsKeyed(SDL_Texture *sheet);
void POCKET_ShiftTileLayer(POCKET_TileLayer *layer, int dy);
void POCKET_ClearTileLayer(POCKET_TileLayer *layer);
void POCKET_PutTile(POCKET_TileLayer *layer, SDL_Texture *sheet, int sx, int sy, int x, int y, int w, int h);
bool POCKET_PutTileGrid(POCKET_TileLayer*, SDL_Texture*, const int*, int, int, int);
bool POCKET_DrawTileLayer(SDL_Renderer *renderer, POCKET_TileLayer *layer, int src_y);
bool POCKET_CopyOpaqueTile(SDL_Renderer *renderer, SDL_Texture *sheet, int sx, int sy, int x, int y, int w, int h);
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
    POCKET_ScrollTexture(texture, 0, dy);
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

/* The tower's tile layer: tiles put in, the layer shifted up by a tile row
 * and another tile put in, then blended onto an opaque target from row 3; SDL blends the same tiles straight onto the target where they end up.
 * The tile at the top leaves, and its rows come back as the clear new row.
 * The tiles come from a sheet whose pixels are wholly opaque or clear. */
static void tile_layer(SDL_Renderer *renderer, Capture *capture)
{
    static Uint32 argb[16 * 16];
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            argb[y * 16 + x] = (x * 3 + y) % 4 == 0 ? 0x00123456 : 0xFF000000 | (Uint32) (x * 0x0F0300 + y * 0x00000F + 0x200010);
        }
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_ARGB8888, argb, 16 * 4);
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_Texture *sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_Texture *screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 56);
    SDL_SetRenderTarget(renderer, screen);
    SDL_SetRenderDrawColor(renderer, 20, 70, 120, 255);
    SDL_RenderClear(renderer);

    /* {sheet x, sheet y, layer x, layer y before the shift} */
    static const int tiles[][4] = {
        {0, 0, 0, 8}, {8, 0, 24, 16}, {0, 8, 40, 40}, {8, 8, 88, 56}, {8, 0, 64, 24}, {8, 8, 72, 0}
    };
    const int count = (int) SDL_arraysize(tiles);
#ifdef SDL_RenderClear
    capture_number(capture, "tile sheet keyed", POCKET_TileSheetIsKeyed(sheet));
    POCKET_TileLayer *layer = POCKET_CreateTileLayer(96, 64);
    POCKET_ClearTileLayer(layer);
    for (int i = 0; i < count; i++)
    {
        POCKET_PutTile(layer, sheet, tiles[i][0], tiles[i][1], tiles[i][2], tiles[i][3], 8, 8);
    }
    /* Drawn once, so the shifted rows' runs are kept and the new row's
     * built. */
    POCKET_DrawTileLayer(renderer, layer, 0);
    SDL_RenderClear(renderer);
    POCKET_ShiftTileLayer(layer, -8);
    POCKET_PutTile(layer, sheet, 0, 0, 16, 48, 8, 8);
    POCKET_DrawTileLayer(renderer, layer, 3);
    POCKET_DestroyTileLayer(layer);
#else
    capture_number(capture, "tile sheet keyed", 1);
    for (int i = 0; i < count; i++)
    {
        const SDL_FRect src = {(float) tiles[i][0], (float) tiles[i][1], 8, 8};
        const SDL_FRect dst = {(float) tiles[i][2], (float) (tiles[i][3] - 8 - 3), 8, 8};
        SDL_RenderTexture(renderer, sheet, &src, &dst);
    }
    const SDL_FRect src = {0, 0, 8, 8};
    const SDL_FRect dst = {16, 48 - 3, 8, 8};
    SDL_RenderTexture(renderer, sheet, &src, &dst);
#endif
    capture_target(renderer, capture, "tile layer");
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(screen);
    SDL_DestroyTexture(sheet);
}

/* The tower background's colour change: tiles from a sheet blended onto
 * black, put onto a cleared target, some of them hanging over its edges.
 * Then the keyed sheet changes, and a tile layer must put the new tile. */
static void opaque_tiles(SDL_Renderer *renderer, SDL_Texture *sprite, Capture *capture)
{
    SDL_Texture *sheet = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 16, 16);
    SDL_SetRenderTarget(renderer, sheet);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, sprite, NULL, NULL);

    SDL_Texture *screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 96, 56);
    SDL_SetRenderTarget(renderer, screen);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 200, 30, 90, 255);
    const SDL_FRect band = {0, 40, 96, 16};
    SDL_RenderFillRect(renderer, &band);
    /* {sheet x, sheet y, x, y} */
    static const int tiles[][4] = {
        {0, 0, 0, -5}, {8, 0, 8, 3}, {0, 8, 92, 20}, {8, 8, 40, 52}, {8, 0, -3, 30}, {0, 8, 48, 24}
    };
    for (int i = 0; i < (int) SDL_arraysize(tiles); i++)
    {
#ifdef SDL_RenderClear
        if (!POCKET_CopyOpaqueTile(renderer, sheet, tiles[i][0], tiles[i][1], tiles[i][2], tiles[i][3], 8, 8))
        {
            capture_number(capture, "opaque tile refused", i);
        }
#else
        const SDL_FRect src = {(float) tiles[i][0], (float) tiles[i][1], 8, 8};
        const SDL_FRect dst = {(float) tiles[i][2], (float) tiles[i][3], 8, 8};
        SDL_RenderTexture(renderer, sheet, &src, &dst);
#endif
    }
#ifdef SDL_RenderClear
    capture_number(capture, "keyed sprite tile refused", POCKET_CopyOpaqueTile(renderer, sprite, 0, 0, 0, 0, 8, 8));
#else
    capture_number(capture, "keyed sprite tile refused", 0);
#endif
    capture_target(renderer, capture, "opaque tiles");

    static Uint32 argb[16 * 16];
    for (int i = 0; i < 16 * 16; i++)
    {
        argb[i] = i % 3 == 0 ? 0 : 0xFF000000 | (Uint32) (i * 0x010203);
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_ARGB8888, argb, 16 * 4);
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_Texture *keyed = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    for (int round = 0; round < 2; round++)
    {
        SDL_SetRenderDrawColor(renderer, 10, 60, 30, 255);
        SDL_RenderClear(renderer);
#ifdef SDL_RenderClear
        POCKET_TileLayer *layer = POCKET_CreateTileLayer(96, 56);
        POCKET_ClearTileLayer(layer);
        POCKET_PutTile(layer, keyed, 8, 0, 4, 4, 8, 8);
        POCKET_DrawTileLayer(renderer, layer, 0);
        POCKET_DestroyTileLayer(layer);
#else
        const SDL_FRect src = {8, 0, 8, 8};
        const SDL_FRect dst = {4, 4, 8, 8};
        SDL_RenderTexture(renderer, keyed, &src, &dst);
#endif
        capture_target(renderer, capture, round == 0 ? "keyed tile" : "keyed tile changed");
        for (int i = 0; i < 16 * 16; i++)
        {
            argb[i] = argb[i] == 0 ? 0 : argb[i] ^ 0x00FFFF00;
        }
        SDL_UpdateTexture(keyed, NULL, argb, 16 * 4);
    }
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(keyed);
    SDL_DestroyTexture(screen);
    SDL_DestroyTexture(sheet);
}

/* Full scrolling textures are 336 square, including pixels offscreen. */
#ifndef SDL_RenderClear
static void scroll_reference(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Texture *temp, int dx, int dy)
{
    SDL_SetRenderTarget(renderer, temp);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (dx > -336 && dx < 336 && dy > -336 && dy < 336)
    {
        const SDL_FRect to = {(float) dx, (float) dy, 336, 336};
        SDL_RenderTexture(renderer, texture, NULL, &to);
    }
    SDL_SetRenderTarget(renderer, texture);
    SDL_RenderTexture(renderer, temp, NULL, NULL);
}
#endif

static bool try_fast_scroll(SDL_Texture *texture, int dx, int dy)
{
#ifdef SDL_RenderClear
    return POCKET_ScrollTexture(texture, dx, dy);
#else
    (void) texture; (void) dx; (void) dy;
    return false;
#endif
}

static void scroll_offset(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Texture *temp, int dx, int dy)
{
#ifdef SDL_RenderClear
    (void) renderer;
    (void) temp;
    assert(POCKET_ScrollTexture(texture, dx, dy));
#else
    scroll_reference(renderer, texture, temp, dx, dy);
#endif
}

static void scroll_pattern(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 11; x++)
        {
            const SDL_FRect mark = {(float) (x * 31), (float) (y * 47), 5, 9};
            SDL_SetRenderDrawColor(renderer, (x + y) % 3 ? 255 : 0, x % 2 ? 255 : 0, y % 2 ? 255 : 0, 255);
            SDL_RenderFillRect(renderer, &mark);
        }
}

static void exact_target(SDL_Renderer *renderer, Capture *capture, const char *name)
{
    capture_target(renderer, capture, name);
    capture->items[capture->count - 1].tolerance = 0;
}

static void scroll_axes(SDL_Renderer *renderer, Capture *capture)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *shared = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(temp, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(shared, SDL_BLENDMODE_NONE);
    static const struct { int x, y; const char *name; } cases[] = {
        {0, 0, "scroll unchanged"}, {-3, 0, "scroll left"}, {7, 0, "scroll right"},
        {-3, -5, "scroll diagonal up"}, {9, 5, "scroll diagonal down"},
        {-336, 0, "scroll full width"}, {400, 0, "scroll beyond width"},
        {INT_MIN, 0, "scroll minimum x"}, {INT_MAX, 0, "scroll maximum x"},
        {0, INT_MIN, "scroll minimum y"}, {0, INT_MAX, "scroll maximum y"}
    };
    for (size_t i = 0; i < SDL_arraysize(cases); i++)
    {
        scroll_pattern(renderer, texture);
        scroll_offset(renderer, texture, temp, cases[i].x, cases[i].y);
        exact_target(renderer, capture, cases[i].name);
    }
    scroll_pattern(renderer, texture);
    for (int i = 0; i < 120; i++) scroll_offset(renderer, texture, temp, -3, 0);
    exact_target(renderer, capture, "scroll repeated beyond width");
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
    scroll_offset(renderer, texture, temp, -3, 5);
    exact_target(renderer, capture, "scroll pending clear");

    scroll_pattern(renderer, texture);
    SDL_SetRenderTarget(renderer, shared);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_SetRenderTarget(renderer, texture);
    scroll_offset(renderer, texture, temp, -3, 0);
    exact_target(renderer, capture, "scroll source after sharing");
    SDL_SetRenderTarget(renderer, shared);
    exact_target(renderer, capture, "scroll shared snapshot kept");
    scroll_pattern(renderer, texture);
    SDL_SetRenderTarget(renderer, shared);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    scroll_offset(renderer, shared, temp, 4, -7);
    exact_target(renderer, capture, "scroll shared after row shift");
    SDL_SetRenderTarget(renderer, texture);
    exact_target(renderer, capture, "scroll shared leaves original");

    scroll_pattern(renderer, texture);
    SDL_SetTextureColorMod(texture, 128, 255, 255);
    capture_number(capture, "scroll rejects tint", try_fast_scroll(texture, -3, 0));
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 128);
    capture_number(capture, "scroll rejects alpha mod", try_fast_scroll(texture, 0, -3));
    SDL_SetTextureAlphaMod(texture, 255);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
    capture_number(capture, "scroll rejects add blend", try_fast_scroll(texture, -3, 2));
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    exact_target(renderer, capture, "scroll rejected without mutation");
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 128);
    SDL_RenderClear(renderer);
    capture_number(capture, "scroll rejects transparent", try_fast_scroll(texture, -3, 0));
    exact_target(renderer, capture, "scroll transparent unchanged");

    /* Static textures retain ARGB storage even when all pixels are opaque. */
    static Uint32 pixels[336 * 336];
    for (int i = 0; i < 336 * 336; i++)
        pixels[i] = 0xFF000000u | ((Uint32) (i % 251) << 16) | ((Uint32) (i % 193) << 8) | (Uint32) (i % 173);
    SDL_Surface *surface = SDL_CreateSurfaceFrom(336, 336, SDL_PIXELFORMAT_ARGB8888, pixels, 336 * 4);
    SDL_Texture *image = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_SetTextureBlendMode(image, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, texture);
#ifdef SDL_RenderClear
    capture_number(capture, "scroll ARGB accepted", POCKET_ScrollTexture(image, -3, 0));
    SDL_RenderTexture(renderer, image, NULL, NULL);
#else
    capture_number(capture, "scroll ARGB accepted", 1);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    scroll_reference(renderer, texture, temp, -3, 0);
#endif
    exact_target(renderer, capture, "scroll opaque ARGB");
    SDL_DestroyTexture(image);
    SDL_DestroyTexture(shared);
    SDL_DestroyTexture(temp);
    SDL_DestroyTexture(texture);
    SDL_SetRenderTarget(renderer, NULL);
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

/* Full-width copies can be batched only while rows remain contiguous.
 * Distinct row colours expose wrong source offsets, strides and flips. */
static void window_copies(SDL_Renderer *renderer, Capture *capture)
{
    SDL_Texture *image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_SetRenderTarget(renderer, image);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (int y = 0; y < 240; y++)
    {
        const SDL_FRect row = {0, (float) y, 320, 1};
        SDL_SetRenderDrawColor(renderer, (Uint8) y, (Uint8) (255 - y), (Uint8) (y * 3), 255);
        SDL_RenderFillRect(renderer, &row);
    }
    SDL_SetTextureBlendMode(image, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    capture_target(renderer, capture, "packed window copy");
    const SDL_FRect source = {0, 37, 320, 91};
    const SDL_FRect target = {0, 55, 320, 91};
    SDL_RenderTexture(renderer, image, &source, &target);
    capture_target(renderer, capture, "packed row slice");
    const SDL_FRect clipped = {-9, 0, 320, 240};
    SDL_RenderTexture(renderer, image, NULL, &clipped);
    capture_target(renderer, capture, "clipped window copy");
    SDL_RenderTextureRotated(renderer, image, NULL, NULL, 0, NULL, SDL_FLIP_VERTICAL);
    capture_target(renderer, capture, "flipped window copy");
    const SDL_FRect above = {0, -73, 320, 240};
    const SDL_FRect below = {0, 211, 320, 240};
    SDL_RenderTexture(renderer, image, NULL, &above);
    SDL_RenderTexture(renderer, image, NULL, &below);
    capture_target(renderer, capture, "vertically clipped copy");
#ifdef SDL_RenderClear
    assert(POCKET_ScrollTexture(image, 0, 13));
#else
    SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_SetRenderTarget(renderer, temp);
    SDL_RenderClear(renderer);
    const SDL_FRect shifted = {0, 13, 320, 240};
    SDL_RenderTexture(renderer, image, NULL, &shifted);
    SDL_SetRenderTarget(renderer, image);
    SDL_RenderTexture(renderer, temp, NULL, NULL);
    SDL_DestroyTexture(temp);
#endif
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    capture_target(renderer, capture, "row ring window copy");
    SDL_DestroyTexture(image);
}

static void repeat_tiles(SDL_Renderer *renderer, SDL_Texture *sheet,
    const SDL_Rect *source, const SDL_Rect *area, bool supported)
{
#ifdef SDL_RenderClear
    const bool drawn = POCKET_RepeatTexture(renderer, sheet, source, area);
    assert(drawn == supported);
    if (drawn) return;
#else
    (void) supported;
#endif
    const SDL_FRect from = {(float) source->x, (float) source->y, (float) source->w, (float) source->h};
    for (int y = area->y; y < area->y + area->h; y += source->h)
        for (int x = area->x; x < area->x + area->w; x += source->w)
        {
            const SDL_FRect to = {(float) x, (float) y, (float) source->w, (float) source->h};
            SDL_RenderTexture(renderer, sheet, &from, &to);
        }
}

static void repeated_patterns(SDL_Renderer *renderer, Capture *capture)
{
    Uint32 pixels[32 * 16];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            pixels[y * 32 + x] = (x >= 16 ? 0x80000000u : (x + y) % 5 ? 0xFF000000u : 0)
                | (x % 2 ? 0x00FF0000u : 0) | (y % 2 ? 0x0000FF00u : 0) | 0xFF;
    SDL_Surface *surface = SDL_CreateSurfaceFrom(32, 16, SDL_PIXELFORMAT_ARGB8888, pixels, 32 * 4);
    SDL_Texture *sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
    const SDL_Rect source = {0, 0, 16, 16};
    const SDL_Rect top = {-5, -7, 64, 64}, bottom = {280, 212, 64, 64};
    repeat_tiles(renderer, sheet, &source, &top, true);
    exact_target(renderer, capture, "repeat keyed clipped top");
    repeat_tiles(renderer, sheet, &source, &bottom, true);
    exact_target(renderer, capture, "repeat clipped bottom");
    const SDL_Rect changed = {3, 4, 1, 1};
    Uint32 pixel = 0xFFFF0000u;
    SDL_UpdateTexture(sheet, &changed, &pixel, sizeof(pixel));
    repeat_tiles(renderer, sheet, &source, &top, true);
    exact_target(renderer, capture, "repeat changed pixels");
    pixel = 0x80FF00FFu;
    SDL_UpdateTexture(sheet, &changed, &pixel, sizeof(pixel));
    repeat_tiles(renderer, sheet, &source, &top, false);
    capture_target(renderer, capture, "repeat partial fallback");
    pixel = 0xFF00FFFFu;
    SDL_UpdateTexture(sheet, &changed, &pixel, sizeof(pixel));
    SDL_SetTextureColorMod(sheet, 128, 255, 255);
    repeat_tiles(renderer, sheet, &source, &top, false);
    capture_target(renderer, capture, "repeat tint fallback");
    SDL_SetTextureColorMod(sheet, 255, 255, 255);
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_NONE);
    repeat_tiles(renderer, sheet, &source, &top, false);
    capture_target(renderer, capture, "repeat none fallback");
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
    SDL_Texture *layer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_Texture *snapshot = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    SDL_SetRenderTarget(renderer, layer);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, snapshot);
    SDL_RenderTexture(renderer, layer, NULL, NULL);
    SDL_SetRenderTarget(renderer, layer);
    repeat_tiles(renderer, sheet, &source, &top, true);
    exact_target(renderer, capture, "repeat detaches target");
    SDL_SetRenderTarget(renderer, snapshot);
    exact_target(renderer, capture, "repeat preserves snapshot");
    SDL_SetRenderTarget(renderer, layer);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    repeat_tiles(renderer, sheet, &source, &top, false);
    capture_target(renderer, capture, "repeat ARGB fallback");
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(snapshot);
    SDL_DestroyTexture(layer);
    SDL_DestroyTexture(sheet);
}

/* Keep repainting for longer than a full wrap, without readback flattening
 * storage; then exercise both packed spans and operations that normalize. */
static void column_origins(SDL_Renderer *renderer, Capture *capture)
{
    SDL_Texture *image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *other = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    Uint32 pixels[16 * 16];
    SDL_Texture *stamp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    SDL_SetTextureBlendMode(image, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(temp, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(other, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(stamp, SDL_BLENDMODE_NONE);
    scroll_pattern(renderer, image);
    const SDL_Rect tile = {0, 0, 16, 16}, edge = {320, 0, 16, 336};
    for (int frame = 0; frame < 160; frame++)
    {
        for (int i = 0; i < 16 * 16; i++)
            pixels[i] = 0xFF000000u | ((frame + i) % 3 == 0 ? 0xFF0000u : (frame + i) % 3 == 1 ? 0xFF00u : 0xFFu);
        SDL_UpdateTexture(stamp, NULL, pixels, 16 * 4);
        scroll_offset(renderer, image, temp, -3, 0);
        SDL_SetRenderTarget(renderer, image);
        repeat_tiles(renderer, stamp, &tile, &edge, true);
    }
    exact_target(renderer, capture, "column wrap with repaint");
    for (int i = 0; i < 16 * 16; i += 3) pixels[i] &= 0x00FFFFFFu;
    SDL_UpdateTexture(stamp, NULL, pixels, 16 * 4);
    SDL_SetTextureBlendMode(stamp, SDL_BLENDMODE_BLEND);
    const SDL_Rect across = {184, 19, 32, 32};
    repeat_tiles(renderer, stamp, &tile, &across, true);
    exact_target(renderer, capture, "column keyed spans wrap");
    scroll_pattern(renderer, other);
    scroll_offset(renderer, other, temp, -7, 0);
    SDL_SetRenderTarget(renderer, other);
    const SDL_FRect from = {2, 11, 320, 240}, to = {5, 9, 320, 240};
    SDL_RenderTexture(renderer, image, &from, &to);
    exact_target(renderer, capture, "both column origins");
    SDL_SetRenderTarget(renderer, NULL);
    const SDL_FRect window = {8, 0, 320, 240};
    SDL_RenderTexture(renderer, image, &window, NULL);
    exact_target(renderer, capture, "column origin to window");
    SDL_SetRenderTarget(renderer, other);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    SDL_SetRenderTarget(renderer, image);
    repeat_tiles(renderer, stamp, &tile, &edge, true);
    exact_target(renderer, capture, "column source detached");
    SDL_SetRenderTarget(renderer, other);
    exact_target(renderer, capture, "column snapshot retained");
    SDL_RenderTexture(renderer, image, NULL, NULL);
    SDL_SetRenderTarget(renderer, image);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
    exact_target(renderer, capture, "column source cleared");
    SDL_SetRenderTarget(renderer, other);
    exact_target(renderer, capture, "column snapshot after clear");
    SDL_SetRenderTarget(renderer, image);
    SDL_RenderTextureRotated(renderer, other, NULL, NULL, 0, NULL, SDL_FLIP_VERTICAL);
    exact_target(renderer, capture, "column vertical flip");
    scroll_offset(renderer, image, temp, -3, 0);
    SDL_RenderTextureRotated(renderer, other, NULL, NULL, 0, NULL, SDL_FLIP_HORIZONTAL);
    exact_target(renderer, capture, "column horizontal flip");
    scroll_offset(renderer, image, temp, -3, 0);
    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
    const SDL_FRect mark = {17, 21, 90, 80};
    SDL_RenderFillRect(renderer, &mark);
    SDL_RenderLine(renderer, 0, 0, 200, 150);
    exact_target(renderer, capture, "column fill and line");
    scroll_offset(renderer, image, temp, -3, 0);
    SDL_Rect update = {13, 17, 16, 16};
    /* Finish queued target draws before a direct texture update on SDL. */
    SDL_SetRenderTarget(renderer, NULL);
    SDL_UpdateTexture(image, &update, pixels, 16 * 4);
    SDL_SetRenderTarget(renderer, image);
    exact_target(renderer, capture, "column partial update");
    scroll_pattern(renderer, image);
    scroll_offset(renderer, image, temp, -13, -5);
    SDL_SetRenderTarget(renderer, other);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, image, &from, &to);
    capture_target(renderer, capture, "column to ARGB");
    SDL_SetRenderTarget(renderer, image);
    scroll_offset(renderer, image, temp, -3, 0);
    SDL_SetTextureColorMod(image, 128, 255, 255);
    SDL_SetRenderTarget(renderer, other);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    capture_target(renderer, capture, "column tinted source");
    SDL_SetTextureColorMod(image, 255, 255, 255);
    SDL_SetRenderTarget(renderer, image);
    scroll_offset(renderer, image, temp, -3, 0);
    SDL_SetRenderTarget(renderer, other);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    SDL_DestroyTexture(image);
    exact_target(renderer, capture, "column destroyed source");
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(other);
    SDL_DestroyTexture(temp);
    SDL_DestroyTexture(stamp);
}

static void draw_grid(SDL_Renderer *renderer, SDL_Texture *sheet, const Uint16 *tiles,
    int columns, int rows, int x, int y, bool supported)
{
#ifdef SDL_RenderClear
    const bool drawn = POCKET_CopyOpaqueGrid(renderer, sheet, tiles, columns, rows, x, y);
    assert(drawn == supported);
    if (drawn) return;
#else
    (void) supported;
#endif
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < columns; i++)
        {
            const int tile = tiles[j * columns + i];
            const SDL_FRect from = {(float) (tile % 4 * 8), (float) (tile / 4 * 8), 8, 8};
            const SDL_FRect to = {(float) (x + i * 8), (float) (y + j * 8), 8, 8};
            SDL_RenderTexture(renderer, sheet, &from, &to);
        }
}

static void opaque_grid(SDL_Renderer *renderer, Capture *capture)
{
    SDL_Texture *sheet = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 32, 24);
    SDL_SetRenderTarget(renderer, sheet);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (int tile = 0; tile < 12; tile++)
    {
        SDL_SetRenderDrawColor(renderer, tile & 1 ? 255 : 0, tile & 2 ? 255 : 0, tile & 4 ? 255 : 0, 255);
        const SDL_FRect rect = {(float) (tile % 4 * 8), (float) (tile / 4 * 8), 8, 8};
        SDL_RenderFillRect(renderer, &rect);
    }
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_NONE);
    SDL_Texture *image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *temp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_Texture *snapshot = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 336, 336);
    SDL_SetTextureBlendMode(image, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(temp, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(snapshot, SDL_BLENDMODE_NONE);
    scroll_pattern(renderer, image);
    static const Uint16 tiles[12] = {11, 0, 5, 2, 8, 3, 7, 1, 4, 10, 6, 9};
    draw_grid(renderer, sheet, tiles, 4, 3, -3, -5, true);
    exact_target(renderer, capture, "grid clipped top");
    scroll_offset(renderer, image, temp, -7, -3);
    draw_grid(renderer, sheet, tiles, 4, 3, 314, 319, true);
    exact_target(renderer, capture, "grid circular target");
    SDL_SetRenderTarget(renderer, snapshot);
    SDL_RenderTexture(renderer, image, NULL, NULL);
    SDL_SetRenderTarget(renderer, image);
    draw_grid(renderer, sheet, tiles, 4, 3, 19, 23, true);
    exact_target(renderer, capture, "grid detached target");
    SDL_SetRenderTarget(renderer, snapshot);
    exact_target(renderer, capture, "grid snapshot retained");
    SDL_SetRenderTarget(renderer, image);
    SDL_SetTextureColorMod(sheet, 128, 255, 255);
    draw_grid(renderer, sheet, tiles, 4, 3, 45, 67, false);
    capture_target(renderer, capture, "grid tint fallback");
    SDL_SetTextureColorMod(sheet, 255, 255, 255);
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
#ifdef SDL_RenderClear
    Uint16 invalid[12];
    SDL_memcpy(invalid, tiles, sizeof(invalid));
    invalid[11] = 99;
    capture_number(capture, "grid rejects invalid tile", POCKET_CopyOpaqueGrid(renderer, sheet, invalid, 4, 3, 0, 0));
    capture_number(capture, "grid rejects overflowing area", POCKET_CopyOpaqueGrid(renderer, sheet, tiles, 4, 3, INT_MAX, 0));
#else
    capture_number(capture, "grid rejects invalid tile", 0);
    capture_number(capture, "grid rejects overflowing area", 0);
#endif
    exact_target(renderer, capture, "invalid grid draws nothing");
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    draw_grid(renderer, sheet, tiles, 4, 3, 20, 30, false);
    capture_target(renderer, capture, "grid ARGB fallback");
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(snapshot);
    SDL_DestroyTexture(temp);
    SDL_DestroyTexture(image);
    SDL_DestroyTexture(sheet);
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

static void put_keyed_grid(SDL_Renderer *renderer, SDL_Texture *sheet, const int *ids)
{
#ifdef SDL_RenderClear
    assert(POCKET_CopyKeyedGrid(renderer, sheet, ids, 3, 2));
#else
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    for (int i = 0; i < 6; i++)
    {
        if (ids[i] < 0) continue;
        const SDL_FRect src = {(ids[i] % 2) * 8, (ids[i] / 2) * 8, 8, 8};
        const SDL_FRect dst = {(i % 3) * 8, (i / 3) * 8, 8, 8};
        SDL_RenderTexture(renderer, sheet, &src, &dst);
    }
#endif
}

static void bulk_tile_grid(SDL_Renderer *renderer, Capture *capture)
{
    Uint32 pixels[16 * 16];
    for (int i = 0; i < 256; i++) pixels[i] = i % 3 ? 0xff80c040 : 0;
    SDL_Surface *surface = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_ARGB8888, pixels, 16 * 4);
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_Texture *sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 24, 16);
    const int ids[] = {0, 1, -1, 2, 3, 0};
#ifdef SDL_RenderClear
    POCKET_TileLayer *layer = POCKET_CreateTileLayer(24, 16);
#endif
    for (int round = 0; round < 3; round++)
    {
        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderDrawColor(renderer, 24, 64, 120, 255);
        SDL_RenderClear(renderer);
#ifdef SDL_RenderClear
        if (round == 2)
        {
            /* A scroll retains the old bottom row, then uploads only the
             * newly exposed row through the layer's circular origin. */
            POCKET_ShiftTileLayer(layer, -8);
            assert(POCKET_PutTileGrid(layer, sheet, ids, 3, 1, 8));
        }
        else assert(POCKET_PutTileGrid(layer, sheet, ids, 3, 2, 0));
        const int invalid[] = {3, 1, -1, 2, 3, 99};
        assert(!POCKET_PutTileGrid(layer, sheet, invalid, 3, 2, 0));
        assert(!POCKET_PutTileGrid(layer, sheet, ids, 3, 2, 8));
        assert(POCKET_DrawTileLayer(renderer, layer, 0));
#else
        for (int i = 0; i < 6; i++)
        {
            const int tile = ids[round == 2 ? (i + 3) % 6 : i];
            if (tile < 0) continue;
            const SDL_FRect src = {tile % 2 * 8, tile / 2 * 8, 8, 8};
            const SDL_FRect dst = {i % 3 * 8, i / 3 * 8, 8, 8};
            SDL_RenderTexture(renderer, sheet, &src, &dst);
        }
#endif
        capture_target(renderer, capture, round == 2 ? "bulk grid shifted rows" : "bulk grid atomic rejection/source change");
        for (int i = 0; i < 256; i++) pixels[i] = i % 4 ? 0xff204080 : 0;
        SDL_UpdateTexture(sheet, NULL, pixels, 16 * 4);
    }
#ifdef SDL_RenderClear
    POCKET_DestroyTileLayer(layer);
#endif
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(sheet);
    SDL_DestroyTexture(target);
}

static void run_cache_patterns(SDL_Renderer *renderer, Capture *capture)
{
    Uint32 pixels[24 * 16];
    SDL_Texture *sheet = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 24, 16);
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
#ifdef SDL_RenderClear
    POCKET_SetTextureRuns(sheet, true);
#endif
    for (int round = 0; round < 4; round++)
    {
        for (int i = 0; i < 24 * 16; i++)
        {
            const Uint32 alpha = round % 2 == 0 ? (i % 3 == 0 ? 0 : i % 3 == 1 ? 128 : 255) : 255;
            pixels[i] = (alpha << 24) | 0x4080c0u;
        }
        if (round == 2)
        {
            const SDL_Rect area = {0, 0, 24, 1};
            SDL_UpdateTexture(sheet, &area, pixels, 24 * 4);
        }
        else SDL_UpdateTexture(sheet, NULL, pixels, 24 * 4);
        SDL_SetRenderTarget(renderer, NULL);
        SDL_SetRenderDrawColor(renderer, 24, 64, 120, 255);
        SDL_RenderClear(renderer);
        draw_sprite(renderer, sheet, 10, 20, 24, 16, SDL_FLIP_NONE);
        capture_target(renderer, capture, "cache full/partial run pattern");
    }
    SDL_DestroyTexture(sheet);
}

static void keyed_grid(SDL_Renderer *renderer, Capture *capture)
{
    Uint32 pixels[16 * 16];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            pixels[y * 16 + x] = ((x + y) % 3 ? 0xff000000u : 0)
                | ((Uint32) x * 13 << 16) | ((Uint32) y * 15 << 8) | 99;
    SDL_Surface *surface = SDL_CreateSurfaceFrom(16, 16, SDL_PIXELFORMAT_ARGB8888, pixels, 16 * 4);
    SDL_Texture *sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 24, 16);
    const int ids[] = {0, 1, -1, 2, 3, 0};
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 22, 55, 88, 255);
    SDL_RenderClear(renderer);
    put_keyed_grid(renderer, sheet, ids);
    capture_target(renderer, capture, "keyed grid transparent replacement");
    SDL_Texture *snapshot = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 24, 16);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, snapshot);
    SDL_RenderTexture(renderer, target, NULL, NULL);
    SDL_SetRenderTarget(renderer, target);
    const int changed[] = {3, 2, 1, 0, -1, 3};
    put_keyed_grid(renderer, sheet, changed);
    capture_target(renderer, capture, "keyed grid snapshot detached");
    SDL_SetRenderTarget(renderer, snapshot);
    capture_target(renderer, capture, "keyed grid snapshot preserved");
    SDL_SetRenderTarget(renderer, target);
#ifdef SDL_RenderClear
    const int invalid[] = {0, 1, -1, 2, 3, 4};
    assert(!POCKET_CopyKeyedGrid(renderer, sheet, invalid, 3, 2));
    assert(!POCKET_CopyKeyedGrid(renderer, sheet, ids, INT_MAX, 2));
    assert(!POCKET_CopyKeyedGrid(renderer, target, ids, 3, 2));
    SDL_SetTextureColorMod(sheet, 128, 255, 255);
    assert(!POCKET_CopyKeyedGrid(renderer, sheet, ids, 3, 2));
    SDL_SetTextureColorMod(sheet, 255, 255, 255);
    pixels[0] = 0x80ffffff;
    SDL_UpdateTexture(sheet, NULL, pixels, 16 * 4);
    assert(!POCKET_CopyKeyedGrid(renderer, sheet, ids, 3, 2));
#endif
    capture_target(renderer, capture, "keyed grid atomic rejection");
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(target);
    SDL_DestroyTexture(snapshot);
    SDL_DestroyTexture(sheet);
}

void RENDER_OPS(SDL_Renderer *renderer, Capture *capture)
{
    keyed_grid(renderer, capture);
    bulk_tile_grid(renderer,capture);
    run_cache_patterns(renderer,capture);
    /* As Screen.cpp does; SDL's default is linear. */
    SDL_SetDefaultTextureScaleMode(renderer, SDL_SCALEMODE_NEAREST);
    SDL_Texture *sprite = make_sprite(renderer);
#ifdef SDL_RenderClear
    POCKET_SetTextureRuns(sprite, true);
#endif
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 40, 60, 90, 255);
    SDL_RenderClear(renderer);
    draw_sprite(renderer, sprite, 15, 25, 16, 16, SDL_FLIP_NONE);
    capture_target(renderer, capture, "first copy with run cache");
#ifdef SDL_RenderClear
    POCKET_SetTextureRuns(sprite, false);
#endif
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
    scroll_axes(renderer, capture);
    tower_cache(renderer, sprite, capture);
    tile_layer(renderer, capture);
    opaque_tiles(renderer, sprite, capture);
    frames(renderer, sprite, capture);
    window_copies(renderer, capture);
    repeated_patterns(renderer, capture);
    column_origins(renderer, capture);
    opaque_grid(renderer, capture);
    surfaces(capture);
    SDL_DestroyTexture(sprite);
}
