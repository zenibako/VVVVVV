/* The SDL3 renderer on openfpgaOS: a software renderer for the calls the
 * game makes.
 *
 * Every texture, render target and the window's back buffer is ARGB8888
 * in memory, so colour and alpha mods, blending and flips follow SDL's
 * rules exactly; RenderPresent hands the back buffer to video.c, which
 * converts it for the framebuffer.  Coordinates are truncated to whole
 * pixels as in SDL's own software renderer, and scaling is nearest
 * neighbour.  Rotation, viewports and clip rectangles are left out: the
 * game uses none of them. */
#include <SDL3/SDL.h>

#include "render_internal.h"

typedef struct
{
    SDL_Texture texture; /* first, so an SDL_Texture pointer is one of these */
    Uint32 *pixels;      /* w by h, no padding */
    SDL_TextureAccess access;
    SDL_BlendMode blend;
    Uint8 mod_r, mod_g, mod_b, mod_a;
    SDL_PropertiesID props;
} Texture;

struct SDL_Renderer
{
    Texture *target; /* NULL draws to the back buffer */
    Uint8 r, g, b, a;
    SDL_BlendMode blend;
};

static SDL_Renderer renderer_state;
static bool renderer_created;
static Texture back_buffer;
static Uint32 back_pixels[POCKET_SCREEN_W * POCKET_SCREEN_H];

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
    back_buffer.pixels = back_pixels;
    back_buffer.access = SDL_TEXTUREACCESS_TARGET;
    SDL_memset(back_pixels, 0, sizeof(back_pixels));
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
    if (!POCKET_SurfaceToARGB(surface, texture_data(texture)->pixels)
        || !SDL_GetSurfaceBlendMode(surface, &blend))
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    texture_data(texture)->blend = blend;
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
    for (int y = 0; y < r.h; y++)
    {
        SDL_memcpy(t->pixels + (size_t) (r.y + y) * t->texture.w + r.x,
                   (const Uint8 *) pixels + (size_t) y * pitch, (size_t) r.w * 4);
    }
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

static void fill(Texture *target, const SDL_Rect *rect, Uint8 r, Uint8 g, Uint8 b, Uint8 a, SDL_BlendMode blend)
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
    const SDL_Rect all = {0, 0, target->texture.w, target->texture.h};
    fill(target, &all, renderer->r, renderer->g, renderer->b, renderer->a, SDL_BLENDMODE_NONE);
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

/* Source x for each destination column of the current copy. */
static int *column_map;
static int column_map_size;

static bool copy_texture(SDL_Renderer *renderer, Texture *src, const SDL_FRect *srcrect, const SDL_FRect *dstrect, SDL_FlipMode flip)
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

    for (int y = clip.y; y < clip.y + clip.h; y++)
    {
        int v = y - dr.y;
        if (flip_v)
        {
            v = dr.h - 1 - v;
        }
        const int sy = sr.y + POCKET_NearestStep(v, sr.h, dr.h);
        const Uint32 *srow = src->pixels + (size_t) sy * src->texture.w;
        Uint32 *drow = dst->pixels + (size_t) y * dst->texture.w + clip.x;
        const Uint32 *first = srow + sr.x + (clip.x - dr.x);

        if (!modulated && blend == SDL_BLENDMODE_NONE && contiguous)
        {
            SDL_memcpy(drow, first, (size_t) clip.w * 4);
            continue;
        }
        for (int i = 0; i < clip.w; i++)
        {
            const Uint32 s = contiguous ? first[i] : srow[column_map[i]];
            Uint32 a = s >> 24;
            if (!modulated)
            {
                if (blend == SDL_BLENDMODE_NONE || (blend == SDL_BLENDMODE_BLEND && a == 255))
                {
                    drow[i] = s;
                    continue;
                }
                if (a == 0 && (blend == SDL_BLENDMODE_BLEND || blend == SDL_BLENDMODE_ADD))
                {
                    continue;
                }
                drow[i] = blend_pixel((s >> 16) & 0xFF, (s >> 8) & 0xFF, s & 0xFF, a, drow[i], blend);
                continue;
            }
            Uint32 r = (s >> 16) & 0xFF;
            Uint32 g = (s >> 8) & 0xFF;
            Uint32 b = s & 0xFF;
            if (mr != 255)
            {
                r = POCKET_Div255(r * mr);
            }
            if (mg != 255)
            {
                g = POCKET_Div255(g * mg);
            }
            if (mb != 255)
            {
                b = POCKET_Div255(b * mb);
            }
            if (ma != 255)
            {
                a = POCKET_Div255(a * ma);
            }
            if (blend == SDL_BLENDMODE_NONE)
            {
                drow[i] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            else if (blend != SDL_BLENDMODE_BLEND || a != 0)
            {
                drow[i] = blend_pixel(r, g, b, a, drow[i], blend);
            }
        }
    }
    return true;
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
    const Texture *target = current_target(renderer);
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
        SDL_memcpy((Uint8 *) surface->pixels + (size_t) y * surface->pitch,
                   target->pixels + (size_t) (r.y + y) * target->texture.w + r.x, (size_t) r.w * 4);
    }
    return surface;
}

bool SDL_RenderPresent(SDL_Renderer *renderer)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    POCKET_PresentFrame(back_pixels);
    return true;
}
