/* The SDL3 renderer on openfpgaOS: a software renderer for the calls the
 * game makes.
 *
 * Every texture and render target is ARGB8888 in memory, so colour and
 * alpha mods, blending and flips follow SDL's rules exactly.  The window is
 * the RGB565 framebuffer itself (video.c): what is drawn onto it is blended
 * in ARGB8888 and stored as RGB565, and RenderPresent shows it.  Coordinates are truncated to whole
 * pixels as in SDL's own software renderer, and scaling is nearest
 * neighbour.  Rotation, viewports and clip rectangles are left out: the
 * game uses none of them.
 *
 * The game redraws the whole screen several times a frame, so the copies
 * that matter skip work where the result is known: a clear is held back
 * until something needs the pixels (and dropped when an opaque copy covers
 * it), a texture known to be opaque blends as a plain row copy, a
 * texture copied more than once after it last changed keeps a list of its
 * visible runs so its transparent pixels are never visited, and a whole
 * texture copied unchanged onto another of its size is shared until one of
 * the two is drawn on. */
#include <SDL3/SDL.h>

#include "render_internal.h"

#ifdef POCKET_SHOW_FPS
#include "of_timer.h"
Uint32 POCKET_render_us;
/* Microseconds in held back clears, row copies, copies by run, pixel by
 * pixel copies, fills and building runs. */
Uint32 POCKET_render_part_us[6];
#define PROFILE_BEGIN() const Uint32 profile_start = of_time_us()
#define PROFILE_END() (POCKET_render_us += of_time_us() - profile_start)
#define PART_BEGIN() const Uint32 part_start = of_time_us()
#define PART_END(part) (POCKET_render_part_us[part] += of_time_us() - part_start)
#else
#define PROFILE_BEGIN() ((void) 0)
#define PROFILE_END() ((void) 0)
#define PART_BEGIN() ((void) 0)
#define PART_END(part) ((void) 0)
#endif

typedef struct Texture
{
    SDL_Texture texture; /* first, so an SDL_Texture pointer is one of these */
    Uint32 *pixels;      /* w by h, no padding; NULL for the window */
    Uint16 *pixels565;   /* the window's framebuffer */
    int pitch565;        /* in pixels */

    /* A full copy not yet made: this texture's pixels are alias_of's. */
    struct Texture *alias_of;
    struct Texture *aliased_by;
    SDL_TextureAccess access;
    SDL_BlendMode blend;
    Uint8 mod_r, mod_g, mod_b, mod_a;
    SDL_PropertiesID props;

    bool opaque;         /* every pixel's alpha is 255 */
    bool clear_pending;  /* every pixel is clear_color, not yet written */
    Uint32 clear_color;

    /* Runs of visible pixels: row y's are runs[row_runs[y]] up to
     * runs[row_runs[y + 1]], each {x, length, kind}. */
    Uint16 *runs;
    int *row_runs;
    bool runs_valid;
    int copies_since_change;
} Texture;

enum
{
    RUN_OPAQUE,
    RUN_TRANSLUCENT
};

struct SDL_Renderer
{
    Texture *target; /* NULL draws to the back buffer */
    Uint8 r, g, b, a;
    SDL_BlendMode blend;
};

static SDL_Renderer renderer_state;
static bool renderer_created;
static Texture back_buffer;

/* Texture properties: an ID is an index into this list, plus one. */
static Texture **property_owners;
static int property_count;

static Texture *texture_data(SDL_Texture *texture)
{
    return (Texture *) texture;
}

static Texture *current_target(const SDL_Renderer *renderer)
{
    return renderer->target ? renderer->target : &back_buffer;
}

/* Write the clear that was held back. */
static void resolve_clear(Texture *t)
{
    if (!t->clear_pending)
    {
        return;
    }
    PART_BEGIN();
    t->clear_pending = false;
    if (t->pixels565 != NULL)
    {
        const Uint16 color = POCKET_ToRGB565(t->clear_color);
        for (int y = 0; y < t->texture.h; y++)
        {
            Uint16 *row = t->pixels565 + (size_t) y * t->pitch565;
            for (int x = 0; x < t->texture.w; x++)
            {
                row[x] = color;
            }
        }
    }
    else
    {
        const size_t count = (size_t) t->texture.w * t->texture.h;
        const Uint32 color = t->clear_color;
        for (size_t i = 0; i < count; i++)
        {
            t->pixels[i] = color;
        }
    }
    PART_END(0);
}

/* Before t's pixels change: covers_all says every pixel is about to be
 * replaced without being read, so a held back clear can be dropped. */
static void begin_change(Texture *t, bool covers_all)
{
    const size_t bytes = (size_t) t->texture.w * t->texture.h * 4;
    if (t->aliased_by != NULL)
    {
        /* The texture sharing these pixels keeps them: hand it the buffer
         * outright when all of t is about to be replaced. */
        Texture *copy = t->aliased_by;
        if (covers_all)
        {
            Uint32 *mine = t->pixels;
            t->pixels = copy->pixels;
            copy->pixels = mine;
        }
        else
        {
            SDL_memcpy(copy->pixels, t->pixels, bytes);
        }
        copy->alias_of = NULL;
        t->aliased_by = NULL;
    }
    if (t->alias_of != NULL)
    {
        if (!covers_all)
        {
            SDL_memcpy(t->pixels, t->alias_of->pixels, bytes);
        }
        t->alias_of->aliased_by = NULL;
        t->alias_of = NULL;
    }
    if (covers_all)
    {
        t->clear_pending = false;
    }
    else
    {
        resolve_clear(t);
    }
    t->runs_valid = false;
    t->copies_since_change = 0;
}

/* The pixels a texture shows, its own or the ones it shares. */
static const Uint32 *texels(const Texture *t)
{
    return t->alias_of != NULL ? t->alias_of->pixels : t->pixels;
}

static bool all_opaque(const Uint32 *pixels, int w, int h, int pitch)
{
    for (int y = 0; y < h; y++)
    {
        const Uint32 *row = pixels + (size_t) y * pitch;
        for (int x = 0; x < w; x++)
        {
            if ((row[x] >> 24) != 255)
            {
                return false;
            }
        }
    }
    return true;
}

static void free_runs(Texture *t)
{
    SDL_free(t->runs);
    SDL_free(t->row_runs);
    t->runs = NULL;
    t->row_runs = NULL;
    t->runs_valid = false;
}

static int run_kind(Uint32 pixel)
{
    const Uint32 a = pixel >> 24;
    return a == 0 ? -1 : a == 255 ? RUN_OPAQUE : RUN_TRANSLUCENT;
}

static bool build_runs(Texture *t)
{
    const int w = t->texture.w;
    const int h = t->texture.h;
    int count = 0;
    for (int y = 0; y < h; y++)
    {
        const Uint32 *row = texels(t) + (size_t) y * w;
        int kind = -1;
        for (int x = 0; x < w; x++)
        {
            const int k = run_kind(row[x]);
            if (k != kind && k >= 0)
            {
                count++;
            }
            kind = k;
        }
    }
    Uint16 *runs = (Uint16 *) SDL_realloc(t->runs, sizeof(Uint16) * 3 * (count ? count : 1));
    if (runs == NULL)
    {
        return false;
    }
    t->runs = runs;
    if (t->row_runs == NULL)
    {
        t->row_runs = (int *) SDL_malloc(sizeof(int) * (h + 1));
        if (t->row_runs == NULL)
        {
            return false;
        }
    }
    int n = 0;
    for (int y = 0; y < h; y++)
    {
        const Uint32 *row = texels(t) + (size_t) y * w;
        t->row_runs[y] = n;
        int x = 0;
        while (x < w)
        {
            const int kind = run_kind(row[x]);
            int end = x + 1;
            while (end < w && run_kind(row[end]) == kind)
            {
                end++;
            }
            if (kind >= 0)
            {
                runs[n * 3] = (Uint16) x;
                runs[n * 3 + 1] = (Uint16) (end - x);
                runs[n * 3 + 2] = (Uint16) kind;
                n++;
            }
            x = end;
        }
    }
    t->row_runs[h] = n;
    t->runs_valid = true;
    return true;
}

static bool check_renderer(const SDL_Renderer *renderer)
{
    if (renderer == NULL || renderer != &renderer_state || !renderer_created)
    {
        return SDL_SetError("Parameter 'renderer' is invalid");
    }
    return true;
}

static bool check_texture(const SDL_Texture *texture)
{
    if (texture == NULL)
    {
        return SDL_SetError("Parameter 'texture' is invalid");
    }
    return true;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name)
{
    (void) name;
    if (window == NULL)
    {
        SDL_SetError("Parameter 'window' is invalid");
        return NULL;
    }
    if (renderer_created)
    {
        SDL_SetError("Renderer already associated with window");
        return NULL;
    }
    SDL_zero(renderer_state);
    renderer_state.a = 255;
    SDL_zero(back_buffer);
    back_buffer.texture.format = SDL_PIXELFORMAT_ARGB8888;
    back_buffer.texture.w = POCKET_SCREEN_W;
    back_buffer.texture.h = POCKET_SCREEN_H;
    back_buffer.pixels565 = POCKET_FrameBuffer(&back_buffer.pitch565);
    back_buffer.access = SDL_TEXTUREACCESS_TARGET;
    back_buffer.opaque = true;
    renderer_created = true;
    return &renderer_state;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (renderer == &renderer_state)
    {
        renderer_created = false;
    }
}

bool SDL_SetRenderVSync(SDL_Renderer *renderer, int vsync)
{
    (void) vsync;
    return check_renderer(renderer);
}

bool SDL_SetDefaultTextureScaleMode(SDL_Renderer *renderer, SDL_ScaleMode scale_mode)
{
    (void) scale_mode;
    return check_renderer(renderer);
}

bool SDL_GetCurrentRenderOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    const Texture *target = current_target(renderer);
    if (w != NULL)
    {
        *w = target->texture.w;
    }
    if (h != NULL)
    {
        *h = target->texture.h;
    }
    return true;
}

static SDL_PropertiesID register_properties(Texture *t)
{
    for (int i = 0; i < property_count; i++)
    {
        if (property_owners[i] == NULL)
        {
            property_owners[i] = t;
            return (SDL_PropertiesID) (i + 1);
        }
    }
    Texture **grown = (Texture **) SDL_realloc(property_owners, sizeof(*grown) * (property_count + 32));
    if (grown == NULL)
    {
        return 0;
    }
    SDL_memset(grown + property_count, 0, sizeof(*grown) * 32);
    property_owners = grown;
    property_owners[property_count] = t;
    property_count += 32;
    return (SDL_PropertiesID) (property_count - 31);
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, SDL_PixelFormat format, SDL_TextureAccess access, int w, int h)
{
    if (!check_renderer(renderer))
    {
        return NULL;
    }
    const SDL_PixelFormatDetails *f = SDL_GetPixelFormatDetails(format);
    if (f == NULL || f->bytes_per_pixel != 4)
    {
        SDL_SetError("Unsupported texture format");
        return NULL;
    }
    if (w <= 0 || h <= 0)
    {
        SDL_SetError("Texture dimensions can't be 0");
        return NULL;
    }
    Texture *t = (Texture *) SDL_calloc(1, sizeof(*t));
    if (t == NULL)
    {
        SDL_SetError("Out of memory");
        return NULL;
    }
    t->pixels = (Uint32 *) SDL_calloc((size_t) w * h, sizeof(Uint32));
    if (t->pixels == NULL)
    {
        SDL_free(t);
        SDL_SetError("Out of memory");
        return NULL;
    }
    t->props = register_properties(t);
    if (t->props == 0)
    {
        SDL_free(t->pixels);
        SDL_free(t);
        SDL_SetError("Out of memory");
        return NULL;
    }
    /* Pixels are always stored as ARGB8888. */
    t->texture.format = SDL_PIXELFORMAT_ARGB8888;
    t->texture.w = w;
    t->texture.h = h;
    t->texture.refcount = 1;
    t->access = access;
    t->blend = f->Amask ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    t->mod_r = t->mod_g = t->mod_b = t->mod_a = 255;
    return &t->texture;
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
    if (surface == NULL)
    {
        SDL_SetError("Parameter 'surface' is invalid");
        return NULL;
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, surface->w, surface->h);
    if (texture == NULL)
    {
        return NULL;
    }
    SDL_BlendMode blend;
    Texture *t = texture_data(texture);
    if (!POCKET_SurfaceToARGB(surface, t->pixels)
        || !SDL_GetSurfaceBlendMode(surface, &blend))
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    t->blend = blend;
    t->opaque = all_opaque(t->pixels, surface->w, surface->h, surface->w);
    return texture;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture == NULL)
    {
        return;
    }
    Texture *t = texture_data(texture);
    if (renderer_state.target == t)
    {
        renderer_state.target = NULL;
    }
    if (t->props > 0 && (int) t->props <= property_count)
    {
        property_owners[t->props - 1] = NULL;
    }
    begin_change(t, false);
    free_runs(t);
    SDL_free(t->pixels);
    SDL_free(t);
}

SDL_PropertiesID SDL_GetTextureProperties(SDL_Texture *texture)
{
    if (!check_texture(texture))
    {
        return 0;
    }
    return texture_data(texture)->props;
}

/* The only properties the game reads are a texture's. */
Sint64 SDL_GetNumberProperty(SDL_PropertiesID props, const char *name, Sint64 default_value)
{
    if (props == 0 || (int) props > property_count || property_owners[props - 1] == NULL || name == NULL)
    {
        return default_value;
    }
    const Texture *t = property_owners[props - 1];
    if (SDL_strcmp(name, SDL_PROP_TEXTURE_FORMAT_NUMBER) == 0)
    {
        return t->texture.format;
    }
    if (SDL_strcmp(name, SDL_PROP_TEXTURE_ACCESS_NUMBER) == 0)
    {
        return t->access;
    }
    if (SDL_strcmp(name, SDL_PROP_TEXTURE_WIDTH_NUMBER) == 0)
    {
        return t->texture.w;
    }
    if (SDL_strcmp(name, SDL_PROP_TEXTURE_HEIGHT_NUMBER) == 0)
    {
        return t->texture.h;
    }
    return default_value;
}

bool SDL_SetTextureColorMod(SDL_Texture *texture, Uint8 r, Uint8 g, Uint8 b)
{
    if (!check_texture(texture))
    {
        return false;
    }
    Texture *t = texture_data(texture);
    t->mod_r = r;
    t->mod_g = g;
    t->mod_b = b;
    return true;
}

bool SDL_GetTextureColorMod(SDL_Texture *texture, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (!check_texture(texture))
    {
        return false;
    }
    const Texture *t = texture_data(texture);
    if (r != NULL)
    {
        *r = t->mod_r;
    }
    if (g != NULL)
    {
        *g = t->mod_g;
    }
    if (b != NULL)
    {
        *b = t->mod_b;
    }
    return true;
}

bool SDL_SetTextureAlphaMod(SDL_Texture *texture, Uint8 alpha)
{
    if (!check_texture(texture))
    {
        return false;
    }
    texture_data(texture)->mod_a = alpha;
    return true;
}

bool SDL_GetTextureAlphaMod(SDL_Texture *texture, Uint8 *alpha)
{
    if (!check_texture(texture))
    {
        return false;
    }
    if (alpha != NULL)
    {
        *alpha = texture_data(texture)->mod_a;
    }
    return true;
}

static bool supported_blend(SDL_BlendMode blend)
{
    switch (blend)
    {
    case SDL_BLENDMODE_NONE:
    case SDL_BLENDMODE_BLEND:
    case SDL_BLENDMODE_ADD:
    case SDL_BLENDMODE_MOD:
    case SDL_BLENDMODE_MUL:
        return true;
    default:
        return SDL_SetError("Unsupported blend mode");
    }
}

bool SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode)
{
    if (!check_texture(texture) || !supported_blend(blendMode))
    {
        return false;
    }
    texture_data(texture)->blend = blendMode;
    return true;
}

bool SDL_SetTextureScaleMode(SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    /* Nothing is drawn scaled on the Pocket's own screen size. */
    (void) scaleMode;
    return check_texture(texture);
}

bool SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    if (!check_texture(texture) || pixels == NULL)
    {
        return SDL_SetError("Parameter 'pixels' is invalid");
    }
    Texture *t = texture_data(texture);
    const SDL_Rect bounds = {0, 0, t->texture.w, t->texture.h};
    SDL_Rect r = bounds;
    if (rect != NULL && !POCKET_IntersectRect(rect, &bounds, &r))
    {
        return true;
    }
    const bool covers_all = r.w == t->texture.w && r.h == t->texture.h;
    begin_change(t, covers_all);
    for (int y = 0; y < r.h; y++)
    {
        SDL_memcpy(t->pixels + (size_t) (r.y + y) * t->texture.w + r.x,
                   (const Uint8 *) pixels + (size_t) y * pitch, (size_t) r.w * 4);
    }
    const bool opaque = all_opaque(t->pixels + (size_t) r.y * t->texture.w + r.x, r.w, r.h, t->texture.w);
    t->opaque = opaque && (covers_all || t->opaque);
    return true;
}

bool SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    if (texture != NULL && texture_data(texture)->access != SDL_TEXTUREACCESS_TARGET)
    {
        return SDL_SetError("Texture not created with SDL_TEXTUREACCESS_TARGET");
    }
    renderer->target = texture_data(texture);
    return true;
}

SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *renderer)
{
    if (!check_renderer(renderer) || renderer->target == NULL)
    {
        return NULL;
    }
    return &renderer->target->texture;
}

bool SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    renderer->r = r;
    renderer->g = g;
    renderer->b = b;
    renderer->a = a;
    return true;
}

bool SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    if (!check_renderer(renderer) || !supported_blend(blendMode))
    {
        return false;
    }
    renderer->blend = blendMode;
    return true;
}

/* One source pixel, already colour and alpha modulated, onto one
 * destination pixel, with SDL's blend equations. */
static inline Uint32 blend_pixel(Uint32 r, Uint32 g, Uint32 b, Uint32 a, Uint32 d, SDL_BlendMode blend)
{
    const Uint32 dr = (d >> 16) & 0xFF;
    const Uint32 dg = (d >> 8) & 0xFF;
    const Uint32 db = d & 0xFF;
    const Uint32 da = d >> 24;
    switch (blend)
    {
    case SDL_BLENDMODE_BLEND:
        r = POCKET_Blend(r, dr, a);
        g = POCKET_Blend(g, dg, a);
        b = POCKET_Blend(b, db, a);
        a = a + POCKET_Div255(da * (255 - a));
        break;
    case SDL_BLENDMODE_ADD:
        r = SDL_min(POCKET_Div255(r * a) + dr, 255);
        g = SDL_min(POCKET_Div255(g * a) + dg, 255);
        b = SDL_min(POCKET_Div255(b * a) + db, 255);
        a = da;
        break;
    case SDL_BLENDMODE_MOD:
        r = POCKET_Div255(r * dr);
        g = POCKET_Div255(g * dg);
        b = POCKET_Div255(b * db);
        a = da;
        break;
    case SDL_BLENDMODE_MUL:
        r = SDL_min(POCKET_Div255(r * dr) + POCKET_Div255(dr * (255 - a)), 255);
        g = SDL_min(POCKET_Div255(g * dg) + POCKET_Div255(dg * (255 - a)), 255);
        b = SDL_min(POCKET_Div255(b * db) + POCKET_Div255(db * (255 - a)), 255);
        a = da;
        break;
    default:
        break;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void fill_rect(Texture *target, const SDL_Rect *rect, Uint8 r, Uint8 g, Uint8 b, Uint8 a, SDL_BlendMode blend)
{
    const SDL_Rect bounds = {0, 0, target->texture.w, target->texture.h};
    SDL_Rect clipped;
    if (!POCKET_IntersectRect(rect, &bounds, &clipped))
    {
        return;
    }
    const Uint32 packed = ((Uint32) a << 24) | ((Uint32) r << 16) | ((Uint32) g << 8) | b;
    const bool opaque = blend == SDL_BLENDMODE_NONE || (blend == SDL_BLENDMODE_BLEND && a == 255);
    if (blend == SDL_BLENDMODE_BLEND && a == 0)
    {
        return;
    }
    const bool covers_all = clipped.w == target->texture.w && clipped.h == target->texture.h;
    begin_change(target, opaque && covers_all);
    if (opaque)
    {
        /* Blending keeps an opaque target opaque; the rest keep its alpha. */
        if (a == 255 && covers_all)
        {
            target->opaque = true;
        }
        else if (a != 255)
        {
            target->opaque = false;
        }
    }
    if (target->pixels565 != NULL)
    {
        const Uint16 packed565 = POCKET_ToRGB565(packed);
        for (int y = clipped.y; y < clipped.y + clipped.h; y++)
        {
            Uint16 *row = target->pixels565 + (size_t) y * target->pitch565 + clipped.x;
            for (int x = 0; x < clipped.w; x++)
            {
                row[x] = opaque ? packed565 : POCKET_ToRGB565(blend_pixel(r, g, b, a, POCKET_FromRGB565(row[x]), blend));
            }
        }
        return;
    }
    for (int y = clipped.y; y < clipped.y + clipped.h; y++)
    {
        Uint32 *row = target->pixels + (size_t) y * target->texture.w + clipped.x;
        if (opaque)
        {
            for (int x = 0; x < clipped.w; x++)
            {
                row[x] = packed;
            }
        }
        else
        {
            for (int x = 0; x < clipped.w; x++)
            {
                row[x] = blend_pixel(r, g, b, a, row[x], blend);
            }
        }
    }
}

static void fill(Texture *target, const SDL_Rect *rect, Uint8 r, Uint8 g, Uint8 b, Uint8 a, SDL_BlendMode blend)
{
    PROFILE_BEGIN();
    PART_BEGIN();
    fill_rect(target, rect, r, g, b, a, blend);
    PART_END(4);
    PROFILE_END();
}

static void draw_point(SDL_Renderer *renderer, int x, int y)
{
    const SDL_Rect rect = {x, y, 1, 1};
    fill(current_target(renderer), &rect, renderer->r, renderer->g, renderer->b, renderer->a, renderer->blend);
}

static SDL_Rect truncate_rect(const SDL_FRect *rect)
{
    SDL_Rect r;
    r.x = (int) rect->x;
    r.y = (int) rect->y;
    r.w = (int) rect->w;
    r.h = (int) rect->h;
    return r;
}

bool SDL_RenderClear(SDL_Renderer *renderer)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    Texture *target = current_target(renderer);
    begin_change(target, true);
    target->clear_pending = true;
    target->clear_color = ((Uint32) renderer->a << 24) | ((Uint32) renderer->r << 16) | ((Uint32) renderer->g << 8) | renderer->b;
    target->opaque = renderer->a == 255;
    return true;
}

bool SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rect)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    Texture *target = current_target(renderer);
    const SDL_Rect all = {0, 0, target->texture.w, target->texture.h};
    const SDL_Rect r = rect ? truncate_rect(rect) : all;
    fill(target, &r, renderer->r, renderer->g, renderer->b, renderer->a, renderer->blend);
    return true;
}

bool SDL_RenderRect(SDL_Renderer *renderer, const SDL_FRect *rect)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    Texture *target = current_target(renderer);
    const SDL_Rect all = {0, 0, target->texture.w, target->texture.h};
    const SDL_Rect r = rect ? truncate_rect(rect) : all;
    if (r.w <= 0 || r.h <= 0)
    {
        return true;
    }
    /* Each edge pixel once, so a translucent outline blends evenly. */
    const SDL_Rect edges[4] = {
        {r.x, r.y, r.w, 1},
        {r.x, r.y + r.h - 1, r.w, r.h > 1 ? 1 : 0},
        {r.x, r.y + 1, 1, r.h - 2},
        {r.x + r.w - 1, r.y + 1, r.w > 1 ? 1 : 0, r.h - 2},
    };
    for (int i = 0; i < 4; i++)
    {
        fill(target, &edges[i], renderer->r, renderer->g, renderer->b, renderer->a, renderer->blend);
    }
    return true;
}

bool SDL_RenderLine(SDL_Renderer *renderer, float x1, float y1, float x2, float y2)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    /* Bresenham, both ends included. */
    int x = (int) x1;
    int y = (int) y1;
    const int xe = (int) x2;
    const int ye = (int) y2;
    const int dx = SDL_abs(xe - x);
    const int dy = -SDL_abs(ye - y);
    const int sx = x < xe ? 1 : -1;
    const int sy = y < ye ? 1 : -1;
    int err = dx + dy;
    for (;;)
    {
        draw_point(renderer, x, y);
        if (x == xe && y == ye)
        {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y += sy;
        }
    }
    return true;
}

bool SDL_RenderPoint(SDL_Renderer *renderer, float x, float y)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    draw_point(renderer, (int) x, (int) y);
    return true;
}

bool SDL_RenderPoints(SDL_Renderer *renderer, const SDL_FPoint *points, int count)
{
    if (!check_renderer(renderer) || (points == NULL && count > 0))
    {
        return false;
    }
    for (int i = 0; i < count; i++)
    {
        draw_point(renderer, (int) points[i].x, (int) points[i].y);
    }
    return true;
}

/* One source pixel onto one destination pixel, with the source's colour
 * and alpha mods and blend mode. */
static inline void copy_pixel(Uint32 *d, Uint32 s, const Texture *src, bool modulated)
{
    const SDL_BlendMode blend = src->blend;
    Uint32 a = s >> 24;
    if (!modulated)
    {
        if (blend == SDL_BLENDMODE_NONE || (blend == SDL_BLENDMODE_BLEND && a == 255))
        {
            *d = s;
            return;
        }
        if (a == 0 && (blend == SDL_BLENDMODE_BLEND || blend == SDL_BLENDMODE_ADD))
        {
            return;
        }
        *d = blend_pixel((s >> 16) & 0xFF, (s >> 8) & 0xFF, s & 0xFF, a, *d, blend);
        return;
    }
    Uint32 r = (s >> 16) & 0xFF;
    Uint32 g = (s >> 8) & 0xFF;
    Uint32 b = s & 0xFF;
    if (src->mod_r != 255)
    {
        r = POCKET_Div255(r * src->mod_r);
    }
    if (src->mod_g != 255)
    {
        g = POCKET_Div255(g * src->mod_g);
    }
    if (src->mod_b != 255)
    {
        b = POCKET_Div255(b * src->mod_b);
    }
    if (src->mod_a != 255)
    {
        a = POCKET_Div255(a * src->mod_a);
    }
    if (blend == SDL_BLENDMODE_NONE)
    {
        *d = (a << 24) | (r << 16) | (g << 8) | b;
    }
    else if (blend != SDL_BLENDMODE_BLEND || a != 0)
    {
        *d = blend_pixel(r, g, b, a, *d, blend);
    }
}

static void copy_span(Uint32 *d, const Uint32 *s, int count, const Texture *src, bool modulated)
{
    for (int i = 0; i < count; i++)
    {
        copy_pixel(d + i, s[i], src, modulated);
    }
}

/* Source x for each destination column of the current copy. */
static int *column_map;
static int column_map_size;
/* One widened row of the window. */
static Uint32 line_buffer[POCKET_SCREEN_W];

static bool copy_rows(SDL_Renderer *renderer, Texture *src, const SDL_FRect *srcrect, const SDL_FRect *dstrect, SDL_FlipMode flip)
{
    Texture *dst = current_target(renderer);
    if (src == dst)
    {
        return SDL_SetError("Can't copy a texture onto itself");
    }

    const SDL_Rect src_bounds = {0, 0, src->texture.w, src->texture.h};
    SDL_Rect sr = src_bounds;
    SDL_Rect dr = {0, 0, dst->texture.w, dst->texture.h};
    if (dstrect != NULL)
    {
        dr = truncate_rect(dstrect);
    }
    if (srcrect != NULL)
    {
        const SDL_Rect wanted = truncate_rect(srcrect);
        /* SDL stretches what is left of an overhanging source rectangle
         * over the whole destination. */
        if (!POCKET_IntersectRect(&wanted, &src_bounds, &sr) || wanted.w <= 0 || wanted.h <= 0)
        {
            return true;
        }
    }
    if (dr.w <= 0 || dr.h <= 0)
    {
        return true;
    }

    const SDL_Rect dst_bounds = {0, 0, dst->texture.w, dst->texture.h};
    SDL_Rect clip;
    if (!POCKET_IntersectRect(&dr, &dst_bounds, &clip))
    {
        return true;
    }

    const bool flip_h = (flip & SDL_FLIP_HORIZONTAL) != 0;
    const bool flip_v = (flip & SDL_FLIP_VERTICAL) != 0;
    const bool contiguous = !flip_h && sr.w == dr.w;
    if (!contiguous)
    {
        if (column_map_size < clip.w)
        {
            int *grown = (int *) SDL_realloc(column_map, sizeof(int) * clip.w);
            if (grown == NULL)
            {
                return SDL_SetError("Out of memory");
            }
            column_map = grown;
            column_map_size = clip.w;
        }
        for (int i = 0; i < clip.w; i++)
        {
            int u = clip.x + i - dr.x;
            if (flip_h)
            {
                u = dr.w - 1 - u;
            }
            column_map[i] = sr.x + POCKET_NearestStep(u, sr.w, dr.w);
        }
    }

    const Uint32 mr = src->mod_r, mg = src->mod_g, mb = src->mod_b, ma = src->mod_a;
    const bool modulated = (mr & mg & mb & ma) != 255;
    const SDL_BlendMode blend = src->blend;

    resolve_clear(src);
    /* Copying an opaque texture with blending gives its pixels unchanged. */
    const bool replaces = blend == SDL_BLENDMODE_NONE
        || (blend == SDL_BLENDMODE_BLEND && src->opaque && ma == 255);
    const bool covers_all = clip.w == dst->texture.w && clip.h == dst->texture.h;
    begin_change(dst, replaces && covers_all);
    if (replaces)
    {
        if (src->opaque && ma == 255)
        {
            dst->opaque = dst->opaque || covers_all;
        }
        else
        {
            dst->opaque = false;
        }
    }
    const bool raw = !modulated && replaces && contiguous;

    /* A whole texture copied unchanged onto another the same size: share
     * its pixels until one of them changes. */
    if (raw && !flip_v && dst->pixels != NULL && covers_all
        && sr.x == 0 && sr.y == 0 && sr.w == src->texture.w && sr.h == src->texture.h
        && dr.w == dst->texture.w && dr.h == dst->texture.h
        && src->texture.w == dst->texture.w && src->texture.h == dst->texture.h
        && src->alias_of == NULL && src->aliased_by == NULL && dst->aliased_by == NULL)
    {
        dst->alias_of = src;
        src->aliased_by = dst;
        return true;
    }

    /* Transparent pixels change nothing when blending or adding; skip them
     * by run once a texture is drawn more than once unchanged. */
    const bool skips_clear = blend == SDL_BLENDMODE_BLEND || blend == SDL_BLENDMODE_ADD;
    if (!raw && skips_clear && contiguous && !src->runs_valid && src->copies_since_change > 0)
    {
        PART_BEGIN();
        build_runs(src);
        PART_END(5);
    }
    if (src->copies_since_change < 2)
    {
        src->copies_since_change++;
    }
    const bool by_runs = !raw && skips_clear && contiguous && src->runs_valid;

    PART_BEGIN();
    for (int y = clip.y; y < clip.y + clip.h; y++)
    {
        int v = y - dr.y;
        if (flip_v)
        {
            v = dr.h - 1 - v;
        }
        const int sy = sr.y + POCKET_NearestStep(v, sr.h, dr.h);
        const Uint32 *srow = texels(src) + (size_t) sy * src->texture.w;
        const int sx0 = sr.x + (clip.x - dr.x);
        const Uint32 *first = srow + sx0;
        Uint16 *row565 = NULL;
        Uint32 *drow;
        if (dst->pixels565 != NULL)
        {
            /* The window: convert straight in, or blend on a widened copy
             * of the row and store it back. */
            row565 = dst->pixels565 + (size_t) y * dst->pitch565 + clip.x;
            if (raw)
            {
                for (int i = 0; i < clip.w; i++)
                {
                    row565[i] = POCKET_ToRGB565(first[i]);
                }
                continue;
            }
            drow = line_buffer;
            for (int i = 0; i < clip.w; i++)
            {
                drow[i] = POCKET_FromRGB565(row565[i]);
            }
        }
        else
        {
            drow = dst->pixels + (size_t) y * dst->texture.w + clip.x;
            if (raw)
            {
                SDL_memcpy(drow, first, (size_t) clip.w * 4);
                continue;
            }
        }
        if (by_runs)
        {
            const Uint16 *run = src->runs + (size_t) src->row_runs[sy] * 3;
            const Uint16 *last = src->runs + (size_t) src->row_runs[sy + 1] * 3;
            for (; run < last; run += 3)
            {
                const int from = SDL_max((int) run[0], sx0);
                const int to = SDL_min((int) run[0] + run[1], sx0 + clip.w);
                if (from >= to)
                {
                    continue;
                }
                if (run[2] == RUN_OPAQUE && !modulated && blend == SDL_BLENDMODE_BLEND)
                {
                    SDL_memcpy(drow + (from - sx0), srow + from, (size_t) (to - from) * 4);
                }
                else
                {
                    copy_span(drow + (from - sx0), srow + from, to - from, src, modulated);
                }
            }
        }
        else if (contiguous)
        {
            copy_span(drow, first, clip.w, src, modulated);
        }
        else
        {
            for (int i = 0; i < clip.w; i++)
            {
                copy_pixel(drow + i, srow[column_map[i]], src, modulated);
            }
        }
        if (row565 != NULL)
        {
            for (int i = 0; i < clip.w; i++)
            {
                row565[i] = POCKET_ToRGB565(drow[i]);
            }
        }
    }
    PART_END(raw ? 1 : by_runs ? 2 : 3);
    return true;
}

static bool copy_texture(SDL_Renderer *renderer, Texture *src, const SDL_FRect *srcrect, const SDL_FRect *dstrect, SDL_FlipMode flip)
{
    PROFILE_BEGIN();
    const bool ok = copy_rows(renderer, src, srcrect, dstrect, flip);
    PROFILE_END();
    return ok;
}

bool SDL_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect *srcrect, const SDL_FRect *dstrect)
{
    if (!check_renderer(renderer) || !check_texture(texture))
    {
        return false;
    }
    return copy_texture(renderer, texture_data(texture), srcrect, dstrect, SDL_FLIP_NONE);
}

bool SDL_RenderTextureRotated(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect *srcrect, const SDL_FRect *dstrect, double angle, const SDL_FPoint *center, SDL_FlipMode flip)
{
    (void) center;
    if (!check_renderer(renderer) || !check_texture(texture))
    {
        return false;
    }
    if (angle != 0.0)
    {
        return SDL_SetError("Rotation is not supported");
    }
    return copy_texture(renderer, texture_data(texture), srcrect, dstrect, flip);
}

SDL_Surface *SDL_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!check_renderer(renderer))
    {
        return NULL;
    }
    Texture *target = current_target(renderer);
    resolve_clear(target);
    const SDL_Rect bounds = {0, 0, target->texture.w, target->texture.h};
    SDL_Rect r = bounds;
    if (rect != NULL && !POCKET_IntersectRect(rect, &bounds, &r))
    {
        SDL_SetError("Rectangle is outside the render target");
        return NULL;
    }
    SDL_Surface *surface = SDL_CreateSurface(r.w, r.h, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL)
    {
        return NULL;
    }
    for (int y = 0; y < r.h; y++)
    {
        Uint32 *out = (Uint32 *) ((Uint8 *) surface->pixels + (size_t) y * surface->pitch);
        if (target->pixels565 != NULL)
        {
            const Uint16 *row = target->pixels565 + (size_t) (r.y + y) * target->pitch565 + r.x;
            for (int x = 0; x < r.w; x++)
            {
                out[x] = POCKET_FromRGB565(row[x]);
            }
        }
        else
        {
            SDL_memcpy(out, texels(target) + (size_t) (r.y + y) * target->texture.w + r.x, (size_t) r.w * 4);
        }
    }
    return surface;
}

bool SDL_RenderPresent(SDL_Renderer *renderer)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    {
        PROFILE_BEGIN();
        resolve_clear(&back_buffer);
        PROFILE_END();
    }
    POCKET_PresentFrame();
    back_buffer.pixels565 = POCKET_FrameBuffer(&back_buffer.pitch565);
    return true;
}
