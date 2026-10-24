/* The SDL3 renderer on openfpgaOS: a software renderer for the calls the
 * game makes.
 *
 * Every texture is ARGB8888 in memory, so colour and alpha mods, blending
 * and flips follow SDL's rules exactly.  The Pocket's memory is slow, so
 * whatever is wholly opaque is kept in RGB565 instead, at half the
 * traffic: the window, which is the framebuffer itself (video.c), and any
 * render target while every pixel of it is opaque.  What is drawn onto
 * those is blended in ARGB8888 and stored as RGB565.  Coordinates are truncated to whole
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
 * pixel copies, fills and building runs. Each timer read is a call into
 * the OS costing about 16 us, so a draw is timed once, around its pixel
 * work, and copies and fills only one in PART_SAMPLE, counted that many
 * times; the render total is the sum of the parts. */
Uint32 POCKET_render_part_us[6];
/* What one timer read adds to a timed span, measured at the first frame. */
Uint32 POCKET_timer_read_us;
/* Copies and fills asked for, drawn or not. */
Uint32 POCKET_render_calls;
#define PART_SAMPLE 16
#define COUNT_CALL() (POCKET_render_calls++)
#define PART_BEGIN() PART_TIMED(1)
#define PART_SAMPLE_BEGIN() PART_TIMED(POCKET_render_calls % PART_SAMPLE == 0 ? PART_SAMPLE : 0)
#define PART_TIMED(weight) \
    const Uint32 part_weight = (weight); \
    const Uint32 part_start = part_weight ? of_time_us() : 0
#define PART_END(part) do { \
        if (part_weight) \
        { \
            const Uint32 part_span = of_time_us() - part_start; \
            const Uint32 part_time = (part_span > POCKET_timer_read_us \
                ? part_span - POCKET_timer_read_us : 0) * part_weight; \
            POCKET_render_part_us[part] += part_time; \
            POCKET_render_us += part_time; \
        } \
    } while (0)
#else
#define COUNT_CALL() ((void) 0)
#define PART_BEGIN() ((void) 0)
#define PART_SAMPLE_BEGIN() ((void) 0)
#define PART_END(part) ((void) 0)
#endif

typedef struct Texture
{
    SDL_Texture texture; /* first, so an SDL_Texture pointer is one of these */
    Uint32 *pixels;      /* w by h, no padding; NULL for the window */
    /* Set while the pixels are kept in RGB565: the window's framebuffer,
     * or a render target's buffer565 while it is opaque. */
    Uint16 *pixels565;
    int pitch565;        /* in pixels */
    Uint16 *buffer565;   /* render targets only, w by h */

    /* A full copy not yet made: this texture's pixels are alias_of's. */
    struct Texture *alias_of;
    struct Texture *aliased_by;
    SDL_TextureAccess access;
    SDL_BlendMode blend;
    Uint8 mod_r, mod_g, mod_b, mod_a;
    SDL_PropertiesID props;

    /* Row y is kept at row (y + row0) % h, so that scrolling a texture up
     * or down moves no pixels (POCKET_ScrollTextureRows). */
    int row0;

    bool opaque;         /* every pixel's alpha is 255 */
    bool clear_pending;  /* every pixel is clear_color, not yet written */
    Uint32 clear_color;

    /* Runs of visible pixels: kept row k's are runs[row_runs[k]] up to
     * runs[row_runs[k + 1]], each {x, length, kind}. Rows are indexed as
     * kept, so a row shift leaves the runs of the rows it keeps good. */
    Uint16 *runs;
    /* The texture in RGB565 while its runs are valid, for copying opaque
     * runs onto RGB565 targets as they are. */
    Uint16 *runs565;
    int *row_runs;
    bool runs_valid;
    /* Runs that are good except for the kept rows marked in dirty_rows,
     * which a change to only some rows leaves to be built again. */
    bool runs_partial;
    Uint8 *dirty_rows;
    /* Build runs on a texture's first copy after a change, not its
     * second: for a texture copied every frame (POCKET_KeepTextureRuns). */
    bool always_runs;
    int copies_since_change;
    /* Counts changes to the pixels, for what is kept made from them. */
    Uint32 changes;
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
/* Copies from's pixels into to, the same size and kept the same way. */
static void copy_whole(Texture *to, const Texture *from)
{
    const size_t count = (size_t) to->texture.w * to->texture.h;
    to->row0 = from->row0;
    if (to->pixels565 != NULL)
    {
        SDL_memcpy(to->pixels565, from->pixels565, count * 2);
    }
    else
    {
        SDL_memcpy(to->pixels, from->pixels, count * 4);
    }
}

static void swap_buffers(Texture *a, Texture *b)
{
    Uint32 *pixels = a->pixels;
    a->pixels = b->pixels;
    b->pixels = pixels;
    Uint16 *buffer565 = a->buffer565;
    a->buffer565 = b->buffer565;
    b->buffer565 = buffer565;
    const int row0 = a->row0;
    a->row0 = b->row0;
    b->row0 = row0;
    if (a->pixels565 != NULL)
    {
        a->pixels565 = a->buffer565;
    }
    if (b->pixels565 != NULL)
    {
        b->pixels565 = b->buffer565;
    }
}

static void begin_change(Texture *t, bool covers_all)
{
    if (t->aliased_by != NULL)
    {
        /* The texture sharing these pixels keeps them: hand it the buffer
         * outright when all of t is about to be replaced. */
        Texture *copy = t->aliased_by;
        if (covers_all)
        {
            swap_buffers(t, copy);
        }
        else
        {
            copy_whole(copy, t);
        }
        copy->alias_of = NULL;
        t->aliased_by = NULL;
    }
    if (t->alias_of != NULL)
    {
        if (!covers_all)
        {
            copy_whole(t, t->alias_of);
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
    t->runs_partial = false;
    t->copies_since_change = 0;
    t->changes++;
}

/* The pixels a texture shows, its own or the ones it shares. */
static const Uint32 *texels(const Texture *t)
{
    return t->alias_of != NULL ? t->alias_of->pixels : t->pixels;
}

static const Uint16 *texels565(const Texture *t)
{
    return t->alias_of != NULL ? t->alias_of->pixels565 : t->pixels565;
}

/* Where row y of t is kept in its own pixels, and in the pixels it shows
 * (another texture's while it shares them). */
static inline size_t kept_row(const Texture *t, int y)
{
    const int row = y + t->row0;
    return (size_t) (row >= t->texture.h ? row - t->texture.h : row);
}

static inline size_t shown_row(const Texture *t, int y)
{
    return kept_row(t->alias_of != NULL ? t->alias_of : t, y);
}

/* After begin_change: t is about to be wholly replaced with opaque
 * pixels, so a render target can keep them in RGB565. */
static void keep_565(Texture *t)
{
    if (t->buffer565 != NULL && t->pixels565 == NULL)
    {
        t->pixels565 = t->buffer565;
        t->pitch565 = t->texture.w;
    }
    t->opaque = true;
}

/* After begin_change: t is about to hold pixels that are not opaque.
 * keep says whether its present pixels matter. */
static void keep_argb(Texture *t, bool keep)
{
    if (t->pixels == NULL || t->pixels565 == NULL)
    {
        return;
    }
    if (keep)
    {
        const size_t count = (size_t) t->texture.w * t->texture.h;
        for (size_t i = 0; i < count; i++)
        {
            t->pixels[i] = POCKET_FromRGB565(t->pixels565[i]);
        }
    }
    t->pixels565 = NULL;
}

/* A row buffer that grows to the widest texture drawn. */
static Uint32 *row_buffer(Uint32 **buffer, int *size, int w)
{
    if (*size < w)
    {
        Uint32 *grown = (Uint32 *) SDL_realloc(*buffer, sizeof(Uint32) * w);
        if (grown == NULL)
        {
            return NULL;
        }
        *buffer = grown;
        *size = w;
    }
    return *buffer;
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
    SDL_free(t->runs565);
    SDL_free(t->dirty_rows);
    t->runs = NULL;
    t->runs565 = NULL;
    t->row_runs = NULL;
    t->dirty_rows = NULL;
    t->runs_valid = false;
    t->runs_partial = false;
}

static int run_kind(Uint32 pixel)
{
    const Uint32 a = pixel >> 24;
    return a == 0 ? -1 : a == 255 ? RUN_OPAQUE : RUN_TRANSLUCENT;
}

static int count_runs(const Uint32 *row, int w)
{
    int count = 0;
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
    return count;
}

static int write_runs(const Uint32 *row, int w, Uint16 *runs)
{
    int n = 0;
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
    return n;
}

/* Builds the runs of every kept row, or with runs_partial only of the
 * rows marked dirty, keeping the others' as they are. */
static bool build_runs(Texture *t)
{
    const int w = t->texture.w;
    const int h = t->texture.h;
    if (t->row_runs == NULL)
    {
        t->row_runs = (int *) SDL_calloc((size_t) h + 1, sizeof(int));
    }
    if (t->dirty_rows == NULL)
    {
        t->dirty_rows = (Uint8 *) SDL_malloc((size_t) h);
    }
    if (t->runs565 == NULL)
    {
        t->runs565 = (Uint16 *) SDL_malloc(sizeof(Uint16) * w * h);
    }
    if (t->row_runs == NULL || t->dirty_rows == NULL || t->runs565 == NULL)
    {
        free_runs(t);
        return false;
    }
    if (!t->runs_partial)
    {
        SDL_memset(t->dirty_rows, 1, (size_t) h);
    }
    const Uint32 *pixels = texels(t);
    int count = 0;
    for (int k = 0; k < h; k++)
    {
        count += t->dirty_rows[k]
            ? count_runs(pixels + (size_t) k * w, w)
            : t->row_runs[k + 1] - t->row_runs[k];
    }
    Uint16 *runs = (Uint16 *) SDL_malloc(sizeof(Uint16) * 3 * (count ? count : 1));
    if (runs == NULL)
    {
        free_runs(t);
        return false;
    }
    int n = 0;
    int old_start = t->row_runs[0];
    for (int k = 0; k < h; k++)
    {
        const int old_end = t->row_runs[k + 1];
        t->row_runs[k] = n;
        const Uint32 *row = pixels + (size_t) k * w;
        if (t->dirty_rows[k])
        {
            n += write_runs(row, w, runs + (size_t) n * 3);
            Uint16 *out = t->runs565 + (size_t) k * w;
            for (int x = 0; x < w; x++)
            {
                out[x] = POCKET_ToRGB565(row[x]);
            }
            t->dirty_rows[k] = 0;
        }
        else
        {
            SDL_memcpy(runs + (size_t) n * 3, t->runs + (size_t) old_start * 3, sizeof(Uint16) * 3 * (old_end - old_start));
            n += old_end - old_start;
        }
        old_start = old_end;
    }
    t->row_runs[h] = n;
    SDL_free(t->runs);
    t->runs = runs;
    t->runs_valid = true;
    t->runs_partial = false;
    return true;
}

/* Whether a change to some of t's rows can leave the others' runs. */
static bool keeps_runs(const Texture *t)
{
    return (t->runs_valid || t->runs_partial) && t->dirty_rows != NULL
        && t->alias_of == NULL && t->aliased_by == NULL && !t->clear_pending
        && t->pixels565 == NULL;
}

/* Before rows y to y + h - 1 of t change, and none of the others. */
static void begin_rows_change(Texture *t, int y, int h)
{
    if (!keeps_runs(t))
    {
        begin_change(t, false);
        return;
    }
    for (int i = y; i < y + h; i++)
    {
        t->dirty_rows[kept_row(t, i)] = 1;
    }
    t->runs_valid = false;
    t->runs_partial = true;
    t->changes++;
}

/* The first of kept row y's runs that ends after column x, found by halves
 * since a row's runs are in order: a tile copied from a wide sheet then
 * looks at its own few runs, not the whole row's. */
static const Uint16 *runs_from(const Texture *t, int y, int x, const Uint16 **last)
{
    const Uint16 *run = t->runs + (size_t) t->row_runs[y] * 3;
    int n = t->row_runs[y + 1] - t->row_runs[y];
    *last = run + (size_t) n * 3;
    while (n > 0)
    {
        const int half = n / 2;
        const Uint16 *mid = run + (size_t) half * 3;
        if (mid[0] + mid[1] <= x)
        {
            run = mid + 3;
            n -= half + 1;
        }
        else
        {
            n = half;
        }
    }
    return run;
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

static void forget_keyed_sheet(const Texture *t);

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
    if (access == SDL_TEXTUREACCESS_TARGET)
    {
        t->buffer565 = (Uint16 *) SDL_malloc((size_t) w * h * 2);
        if (t->buffer565 == NULL)
        {
            SDL_free(t->pixels);
            SDL_free(t);
            SDL_SetError("Out of memory");
            return NULL;
        }
    }
    t->props = register_properties(t);
    if (t->props == 0)
    {
        SDL_free(t->buffer565);
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
    forget_keyed_sheet(t);
    free_runs(t);
    SDL_free(t->buffer565);
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
    keep_argb(t, !covers_all);
    bool opaque = true;
    for (int y = 0; y < r.h; y++)
    {
        Uint32 *row = t->pixels + kept_row(t, r.y + y) * t->texture.w + r.x;
        SDL_memcpy(row, (const Uint8 *) pixels + (size_t) y * pitch, (size_t) r.w * 4);
        opaque = opaque && all_opaque(row, r.w, 1, r.w);
    }
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
    if (covers_all)
    {
        begin_change(target, opaque);
    }
    else
    {
        begin_rows_change(target, clipped.y, clipped.h);
    }
    if (opaque)
    {
        /* Blending keeps an opaque target opaque; the rest keep its alpha. */
        if (a == 255 && covers_all)
        {
            keep_565(target);
        }
        else if (a != 255)
        {
            keep_argb(target, !covers_all);
            target->opaque = false;
        }
    }
    if (target->pixels565 != NULL)
    {
        const Uint16 packed565 = POCKET_ToRGB565(packed);
        for (int y = clipped.y; y < clipped.y + clipped.h; y++)
        {
            Uint16 *row = target->pixels565 + kept_row(target, y) * target->pitch565 + clipped.x;
            for (int x = 0; x < clipped.w; x++)
            {
                row[x] = opaque ? packed565 : POCKET_ToRGB565(blend_pixel(r, g, b, a, POCKET_FromRGB565(row[x]), blend));
            }
        }
        return;
    }
    for (int y = clipped.y; y < clipped.y + clipped.h; y++)
    {
        Uint32 *row = target->pixels + kept_row(target, y) * target->texture.w + clipped.x;
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
    COUNT_CALL();
    PART_SAMPLE_BEGIN();
    fill_rect(target, rect, r, g, b, a, blend);
    PART_END(4);
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
    if (renderer->a == 255)
    {
        keep_565(target);
    }
    else
    {
        keep_argb(target, false);
    }
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

/* The same onto an RGB565 pixel: only pixels that blend are widened. */
static inline void copy_pixel565(Uint16 *d, Uint32 s, const Texture *src, bool modulated)
{
    if (!modulated)
    {
        const SDL_BlendMode blend = src->blend;
        const Uint32 a = s >> 24;
        if (blend == SDL_BLENDMODE_NONE || (blend == SDL_BLENDMODE_BLEND && a == 255))
        {
            *d = POCKET_ToRGB565(s);
            return;
        }
        if (a == 0 && (blend == SDL_BLENDMODE_BLEND || blend == SDL_BLENDMODE_ADD))
        {
            return;
        }
    }
    Uint32 wide = POCKET_FromRGB565(*d);
    copy_pixel(&wide, s, src, modulated);
    *d = POCKET_ToRGB565(wide);
}

static void copy_span565(Uint16 *d, const Uint32 *s, int count, const Texture *src, bool modulated)
{
    for (int i = 0; i < count; i++)
    {
        copy_pixel565(d + i, s[i], src, modulated);
    }
}

/* Source x for each destination column of the current copy. */
static int *column_map;
static int column_map_size;
/* A widened row of an RGB565 source. */
static Uint32 *src_line;
static int src_line_size;

#ifdef POCKET_BENCH
/* The benchmark turns the tinted-run copy off to time it against the
 * general one. */
bool POCKET_tint_runs = true;
#else
#define POCKET_tint_runs true
#endif

static bool copy_rows(SDL_Renderer *renderer, Texture *src, const SDL_FRect *srcrect, const SDL_FRect *dstrect, SDL_FlipMode flip)
{
    COUNT_CALL();
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

    if (row_buffer(&src_line, &src_line_size, src->texture.w) == NULL)
    {
        return SDL_SetError("Out of memory");
    }

    const Uint32 mr = src->mod_r, mg = src->mod_g, mb = src->mod_b, ma = src->mod_a;
    const bool modulated = (mr & mg & mb & ma) != 255;
    const SDL_BlendMode blend = src->blend;

    resolve_clear(src);
    /* Copying an opaque texture with blending gives its pixels unchanged. */
    const bool replaces = blend == SDL_BLENDMODE_NONE
        || (blend == SDL_BLENDMODE_BLEND && src->opaque && ma == 255);
    const bool covers_all = clip.w == dst->texture.w && clip.h == dst->texture.h;
    if (covers_all)
    {
        begin_change(dst, replaces);
    }
    else
    {
        begin_rows_change(dst, clip.y, clip.h);
    }
    const bool raw = !modulated && replaces && contiguous;

    /* A whole texture copied unchanged onto another the same size: share
     * its pixels, kept the same way, until one of them changes. */
    if (raw && !flip_v && dst->pixels != NULL && covers_all
        && sr.x == 0 && sr.y == 0 && sr.w == src->texture.w && sr.h == src->texture.h
        && dr.w == dst->texture.w && dr.h == dst->texture.h
        && src->texture.w == dst->texture.w && src->texture.h == dst->texture.h
        && src->alias_of == NULL && src->aliased_by == NULL && dst->aliased_by == NULL
        && (src->pixels565 == NULL || dst->buffer565 != NULL))
    {
        if (src->pixels565 != NULL)
        {
            keep_565(dst);
        }
        else
        {
            keep_argb(dst, false);
        }
        dst->opaque = src->opaque;
        dst->alias_of = src;
        src->aliased_by = dst;
        return true;
    }
    if (replaces)
    {
        if (src->opaque && ma == 255)
        {
            if (covers_all)
            {
                keep_565(dst);
            }
        }
        else
        {
            keep_argb(dst, !covers_all);
            dst->opaque = false;
        }
    }

    /* Transparent pixels change nothing when blending or adding; skip them
     * by run once a texture is drawn more than once unchanged. Building
     * runs also keeps an RGB565 copy, which raw copies onto RGB565 use. */
    const bool skips_clear = (blend == SDL_BLENDMODE_BLEND || blend == SDL_BLENDMODE_ADD)
        && src->pixels565 == NULL;
    const bool wants_runs = contiguous && src->pixels565 == NULL
        && (raw ? dst->pixels565 != NULL : skips_clear);
    if (wants_runs && !src->runs_valid && (src->copies_since_change > 0 || src->runs_partial || src->always_runs))
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

    PART_SAMPLE_BEGIN();
    for (int y = clip.y; y < clip.y + clip.h; y++)
    {
        int v = y - dr.y;
        if (flip_v)
        {
            v = dr.h - 1 - v;
        }
        const int sy = sr.y + POCKET_NearestStep(v, sr.h, dr.h);
        const int sx0 = sr.x + (clip.x - dr.x);
        const Uint32 *srow;
        if (src->pixels565 != NULL)
        {
            /* An RGB565 source: copied as it is onto RGB565, otherwise
             * widened first. */
            const Uint16 *row = texels565(src) + shown_row(src, sy) * src->pitch565;
            if (raw && dst->pixels565 != NULL)
            {
                SDL_memcpy(dst->pixels565 + kept_row(dst, y) * dst->pitch565 + clip.x, row + sx0, (size_t) clip.w * 2);
                continue;
            }
            const int from = contiguous ? sx0 : 0;
            const int to = contiguous ? sx0 + clip.w : src->texture.w;
            for (int i = from; i < to; i++)
            {
                src_line[i] = POCKET_FromRGB565(row[i]);
            }
            srow = src_line;
        }
        else if (raw && dst->pixels565 != NULL && src->runs_valid && src->runs565 != NULL)
        {
            SDL_memcpy(dst->pixels565 + kept_row(dst, y) * dst->pitch565 + clip.x,
                src->runs565 + shown_row(src, sy) * src->texture.w + sx0, (size_t) clip.w * 2);
            continue;
        }
        else
        {
            srow = texels(src) + shown_row(src, sy) * src->texture.w;
        }
        const Uint32 *first = srow + sx0;
        if (dst->pixels565 != NULL)
        {
            /* Converted straight in; only blended pixels are widened. */
            Uint16 *row565 = dst->pixels565 + kept_row(dst, y) * dst->pitch565 + clip.x;
            if (raw)
            {
                for (int i = 0; i < clip.w; i++)
                {
                    row565[i] = POCKET_ToRGB565(first[i]);
                }
            }
            else if (by_runs)
            {
                const Uint16 *last;
                const Uint16 *run = runs_from(src, (int) shown_row(src, sy), sx0, &last);
                for (; run < last && run[0] < sx0 + clip.w; run += 3)
                {
                    const int from = SDL_max((int) run[0], sx0);
                    const int to = SDL_min((int) run[0] + run[1], sx0 + clip.w);
                    if (from >= to)
                    {
                        continue;
                    }
                    Uint16 *d = row565 + (from - sx0);
                    if (run[2] == RUN_OPAQUE && !modulated && blend == SDL_BLENDMODE_BLEND
                        && src->runs565 != NULL)
                    {
                        SDL_memcpy(d, src->runs565 + shown_row(src, sy) * src->texture.w + from, (size_t) (to - from) * 2);
                    }
                    else if (run[2] == RUN_OPAQUE && !modulated && blend == SDL_BLENDMODE_BLEND)
                    {
                        for (int x = from; x < to; x++)
                        {
                            *d++ = POCKET_ToRGB565(srow[x]);
                        }
                    }
                    else if (run[2] == RUN_OPAQUE && blend == SDL_BLENDMODE_BLEND && src->mod_a == 255
                        && POCKET_tint_runs)
                    {
                        /* Opaque pixels only tinted, as text is: what is
                         * under them does not matter, and a glyph's pixels
                         * are mostly one colour, tinted once. */
                        Uint32 last = ~srow[from];
                        Uint16 tinted = 0;
                        for (int x = from; x < to; x++)
                        {
                            if (srow[x] != last)
                            {
                                last = srow[x];
                                tinted = POCKET_ToRGB565(0xFF000000u
                                    | POCKET_Div255(((last >> 16) & 0xFF) * src->mod_r) << 16
                                    | POCKET_Div255(((last >> 8) & 0xFF) * src->mod_g) << 8
                                    | POCKET_Div255((last & 0xFF) * src->mod_b));
                            }
                            *d++ = tinted;
                        }
                    }
                    else
                    {
                        copy_span565(d, srow + from, to - from, src, modulated);
                    }
                }
            }
            else if (contiguous)
            {
                copy_span565(row565, first, clip.w, src, modulated);
            }
            else
            {
                for (int i = 0; i < clip.w; i++)
                {
                    copy_pixel565(row565 + i, srow[column_map[i]], src, modulated);
                }
            }
            continue;
        }
        Uint32 *drow = dst->pixels + kept_row(dst, y) * dst->texture.w + clip.x;
        if (raw)
        {
            SDL_memcpy(drow, first, (size_t) clip.w * 4);
            continue;
        }
        if (by_runs)
        {
            const Uint16 *last;
            const Uint16 *run = runs_from(src, (int) shown_row(src, sy), sx0, &last);
            for (; run < last && run[0] < sx0 + clip.w; run += 3)
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
    }
    PART_END(raw ? 1 : by_runs ? 2 : 3);
    return true;
}

static bool copy_texture(SDL_Renderer *renderer, Texture *src, const SDL_FRect *srcrect, const SDL_FRect *dstrect, SDL_FlipMode flip)
{
    return copy_rows(renderer, src, srcrect, dstrect, flip);
}

/* Moves t's rows down by dy (up when dy is negative) by changing which
 * row is kept first, and sets the rows that exposes to fill. */
static void shift_rows(Texture *t, int dy, Uint32 fill)
{
    const int h = t->texture.h;
    const int exposed = SDL_min(dy > 0 ? dy : -dy, h);
    const int first = dy > 0 ? 0 : h - exposed;
    /* The rows kept stay where they are, and so do their runs. */
    if (keeps_runs(t))
    {
        if (fill >> 24 != 255)
        {
            t->opaque = false;
        }
        t->runs_valid = false;
        t->runs_partial = true;
        t->changes++;
        t->row0 = ((t->row0 - dy) % h + h) % h;
        for (int y = first; y < first + exposed; y++)
        {
            t->dirty_rows[kept_row(t, y)] = 1;
        }
    }
    else
    {
        begin_change(t, false);
        if (fill >> 24 != 255)
        {
            keep_argb(t, true);
            t->opaque = false;
        }
        t->row0 = ((t->row0 - dy) % h + h) % h;
    }
    for (int y = first; y < first + exposed; y++)
    {
        if (t->pixels565 != NULL)
        {
            const Uint16 color = POCKET_ToRGB565(fill);
            Uint16 *row = t->pixels565 + kept_row(t, y) * t->pitch565;
            for (int x = 0; x < t->texture.w; x++)
            {
                row[x] = color;
            }
        }
        else
        {
            Uint32 *row = t->pixels + kept_row(t, y) * t->texture.w;
            for (int x = 0; x < t->texture.w; x++)
            {
                row[x] = fill;
            }
        }
    }
}

/* Scrolls an opaque, unmodulated texture down by dy rows (up when dy is
 * negative), filling the rows it exposes with opaque black: what clearing
 * a second texture, copying this one onto it at (0, dy) and copying that
 * back would do, but by moving where rows are kept instead of the rows.
 * Returns false, changing nothing, when the copy would differ. */
bool POCKET_ScrollTextureRows(SDL_Texture *texture, int dy)
{
    if (!check_texture(texture))
    {
        return false;
    }
    Texture *t = texture_data(texture);
    if (!t->opaque || t->mod_r != 255 || t->mod_g != 255 || t->mod_b != 255 || t->mod_a != 255)
    {
        return false;
    }
    if (dy != 0)
    {
        shift_rows(t, dy, 0xFF000000);
    }
    return true;
}

void POCKET_KeepTextureRuns(SDL_Texture *texture)
{
    if (check_texture(texture))
    {
        texture_data(texture)->always_runs = true;
    }
}

/* The same for any texture, filling the rows it exposes with transparent
 * pixels, for a texture kept as a scrolling cache. */
bool POCKET_ShiftTextureRows(SDL_Texture *texture, int dy)
{
    if (!check_texture(texture))
    {
        return false;
    }
    if (dy != 0)
    {
        shift_rows(texture_data(texture), dy, 0);
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
            const Uint16 *row = texels565(target) + shown_row(target, r.y + y) * target->pitch565 + r.x;
            for (int x = 0; x < r.w; x++)
            {
                out[x] = POCKET_FromRGB565(row[x]);
            }
        }
        else
        {
            SDL_memcpy(out, texels(target) + shown_row(target, r.y + y) * target->texture.w + r.x, (size_t) r.w * 4);
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
    resolve_clear(&back_buffer);
    POCKET_PresentFrame();
    back_buffer.pixels565 = POCKET_FrameBuffer(&back_buffer.pitch565);
    return true;
}

/* A layer of tiles that are wholly opaque or clear, kept as RGB565 and a
 * mask instead of a texture: drawing tiles into it and blending it onto
 * an RGB565 target touch half the memory an ARGB8888 texture would, and
 * its runs are built from the mask a row at a time as rows change. Rows
 * are kept as a ring, like a texture's, so it scrolls without moving. */
typedef struct POCKET_TileLayer POCKET_TileLayer;
struct POCKET_TileLayer
{
    int w, h, row0;
    Uint16 *color;
    Uint8 *mask;
    /* Kept row k's runs of opaque pixels are runs[k * run_cap ...], as
     * {x, length} pairs, run_count[k] of them. */
    Uint16 *runs;
    Uint16 *run_count;
    int run_cap;
    Uint8 *dirty;
};

static inline int layer_row(const POCKET_TileLayer *layer, int y)
{
    const int row = y + layer->row0;
    return row >= layer->h ? row - layer->h : row;
}

void POCKET_DestroyTileLayer(POCKET_TileLayer *layer)
{
    if (layer == NULL)
    {
        return;
    }
    SDL_free(layer->color);
    SDL_free(layer->mask);
    SDL_free(layer->runs);
    SDL_free(layer->run_count);
    SDL_free(layer->dirty);
    SDL_free(layer);
}

POCKET_TileLayer *POCKET_CreateTileLayer(int w, int h)
{
    POCKET_TileLayer *layer = (POCKET_TileLayer *) SDL_calloc(1, sizeof(*layer));
    if (layer == NULL)
    {
        return NULL;
    }
    layer->w = w;
    layer->h = h;
    layer->run_cap = w / 2 + 1;
    layer->color = (Uint16 *) SDL_calloc((size_t) w * h, sizeof(Uint16));
    layer->mask = (Uint8 *) SDL_calloc((size_t) w * h, 1);
    layer->runs = (Uint16 *) SDL_malloc(sizeof(Uint16) * 2 * layer->run_cap * h);
    layer->run_count = (Uint16 *) SDL_calloc((size_t) h, sizeof(Uint16));
    layer->dirty = (Uint8 *) SDL_calloc((size_t) h, 1);
    if (layer->color == NULL || layer->mask == NULL || layer->runs == NULL
        || layer->run_count == NULL || layer->dirty == NULL)
    {
        POCKET_DestroyTileLayer(layer);
        return NULL;
    }
    return layer;
}

/* Whether every pixel of a tile sheet is wholly opaque or wholly clear,
 * so its tiles can go into a tile layer. */
bool POCKET_TileSheetIsKeyed(SDL_Texture *sheet)
{
    static const Texture *checked;
    static Uint32 checked_changes;
    static bool keyed;
    if (!check_texture(sheet))
    {
        return false;
    }
    const Texture *t = texture_data(sheet);
    if (t->pixels565 != NULL || t->alias_of != NULL || t->pixels == NULL || t->clear_pending)
    {
        return false;
    }
    if (t != checked || t->changes != checked_changes)
    {
        keyed = true;
        const size_t count = (size_t) t->texture.w * t->texture.h;
        for (size_t i = 0; i < count && keyed; i++)
        {
            const Uint32 a = t->pixels[i] >> 24;
            keyed = a == 0 || a == 255;
        }
        checked = t;
        checked_changes = t->changes;
    }
    return keyed;
}

/* A keyed sheet's pixels in RGB565, with a mask of the opaque ones, rows
 * in order: putting a tile is then row copies, not a conversion of each
 * pixel, which matters when a tower's colour change puts every tile. */
static struct
{
    const Texture *of;
    Uint32 changes;
    Uint16 *color;
    Uint8 *mask;
} keyed_sheet;

static void forget_keyed_sheet(const Texture *t)
{
    if (keyed_sheet.of == t)
    {
        SDL_free(keyed_sheet.color);
        SDL_free(keyed_sheet.mask);
        SDL_zero(keyed_sheet);
    }
}

static bool keep_keyed_sheet(const Texture *t)
{
    if (keyed_sheet.of == t && keyed_sheet.changes == t->changes)
    {
        return true;
    }
    forget_keyed_sheet(keyed_sheet.of);
    const size_t count = (size_t) t->texture.w * t->texture.h;
    keyed_sheet.color = (Uint16 *) SDL_malloc(count * sizeof(Uint16));
    keyed_sheet.mask = (Uint8 *) SDL_malloc(count);
    if (keyed_sheet.color == NULL || keyed_sheet.mask == NULL)
    {
        SDL_free(keyed_sheet.color);
        SDL_free(keyed_sheet.mask);
        SDL_zero(keyed_sheet);
        return false;
    }
    for (int y = 0; y < t->texture.h; y++)
    {
        const Uint32 *s = t->pixels + kept_row(t, y) * t->texture.w;
        Uint16 *c = keyed_sheet.color + (size_t) y * t->texture.w;
        Uint8 *m = keyed_sheet.mask + (size_t) y * t->texture.w;
        for (int x = 0; x < t->texture.w; x++)
        {
            c[x] = POCKET_ToRGB565(s[x]);
            m[x] = (Uint8) (s[x] >> 31);
        }
    }
    keyed_sheet.of = t;
    keyed_sheet.changes = t->changes;
    return true;
}

/* Moves the layer's rows down by dy (up when negative); the rows that
 * exposes are clear. */
void POCKET_ShiftTileLayer(POCKET_TileLayer *layer, int dy)
{
    const int h = layer->h;
    const int exposed = SDL_min(dy > 0 ? dy : -dy, h);
    const int first = dy > 0 ? 0 : h - exposed;
    layer->row0 = ((layer->row0 - dy) % h + h) % h;
    for (int y = first; y < first + exposed; y++)
    {
        const int k = layer_row(layer, y);
        SDL_memset(layer->mask + (size_t) k * layer->w, 0, (size_t) layer->w);
        layer->dirty[k] = 1;
    }
}

void POCKET_ClearTileLayer(POCKET_TileLayer *layer)
{
    SDL_memset(layer->mask, 0, (size_t) layer->w * layer->h);
    SDL_memset(layer->dirty, 1, (size_t) layer->h);
}

/* Copies the w by h tile at (sx, sy) of a keyed sheet into the layer at
 * (x, y), clear pixels included, as an unblended copy would. */
void POCKET_PutTile(POCKET_TileLayer *layer, SDL_Texture *sheet, int sx, int sy, int x, int y, int w, int h)
{
    const Texture *t = texture_data(sheet);
    const SDL_Rect wanted = {x, y, w, h};
    const SDL_Rect bounds = {0, 0, layer->w, layer->h};
    SDL_Rect clip;
    if (!POCKET_IntersectRect(&wanted, &bounds, &clip)
        || sx < 0 || sy < 0 || sx + w > t->texture.w || sy + h > t->texture.h)
    {
        return;
    }
    if (keep_keyed_sheet(t))
    {
        for (int row = clip.y; row < clip.y + clip.h; row++)
        {
            const size_t from = (size_t) (sy + row - y) * t->texture.w + sx + (clip.x - x);
            const int k = layer_row(layer, row);
            const size_t to = (size_t) k * layer->w + clip.x;
            SDL_memcpy(layer->color + to, keyed_sheet.color + from, (size_t) clip.w * 2);
            SDL_memcpy(layer->mask + to, keyed_sheet.mask + from, (size_t) clip.w);
            layer->dirty[k] = 1;
        }
        return;
    }
    for (int row = clip.y; row < clip.y + clip.h; row++)
    {
        const Uint32 *s = t->pixels + kept_row(t, sy + row - y) * t->texture.w + sx + (clip.x - x);
        const int k = layer_row(layer, row);
        Uint16 *c = layer->color + (size_t) k * layer->w + clip.x;
        Uint8 *m = layer->mask + (size_t) k * layer->w + clip.x;
        for (int i = 0; i < clip.w; i++)
        {
            c[i] = POCKET_ToRGB565(s[i]);
            m[i] = (Uint8) (s[i] >> 31);
        }
        layer->dirty[k] = 1;
    }
}

/* Blends layer rows from src_y on, as many as the target is tall, onto
 * the RGB565 target at its top left: what blending the same tiles onto it
 * one by one would do. Returns false, drawing nothing, for a target that
 * is not RGB565 or not as wide as the layer. */
bool POCKET_DrawTileLayer(SDL_Renderer *renderer, POCKET_TileLayer *layer, int src_y)
{
    if (!check_renderer(renderer))
    {
        return false;
    }
    COUNT_CALL();
    Texture *dst = current_target(renderer);
    if (dst->texture.w != layer->w || src_y < 0)
    {
        return false;
    }
    resolve_clear(dst);
    if (dst->pixels565 == NULL)
    {
        return false;
    }
    const int rows = SDL_min(dst->texture.h, layer->h - src_y);
    begin_rows_change(dst, 0, rows);
    if (dst->pixels565 == NULL)
    {
        return false;
    }
    PART_BEGIN();
    for (int y = 0; y < rows; y++)
    {
        const int k = layer_row(layer, src_y + y);
        Uint16 *runs = layer->runs + (size_t) k * layer->run_cap * 2;
        const Uint16 *color = layer->color + (size_t) k * layer->w;
        if (layer->dirty[k])
        {
            const Uint8 *m = layer->mask + (size_t) k * layer->w;
            int n = 0;
            int x = 0;
            while (x < layer->w)
            {
                if (!m[x])
                {
                    x++;
                    continue;
                }
                int end = x + 1;
                while (end < layer->w && m[end])
                {
                    end++;
                }
                runs[n * 2] = (Uint16) x;
                runs[n * 2 + 1] = (Uint16) (end - x);
                n++;
                x = end;
            }
            layer->run_count[k] = (Uint16) n;
            layer->dirty[k] = 0;
        }
        Uint16 *row = dst->pixels565 + kept_row(dst, y) * dst->pitch565;
        for (int i = 0; i < layer->run_count[k]; i++)
        {
            SDL_memcpy(row + runs[i * 2], color + runs[i * 2], (size_t) runs[i * 2 + 1] * 2);
        }
    }
    PART_END(2);
    return true;
}

/* Copies the w by h part at (sx, sy) of an opaque, unmodulated RGB565
 * texture to (x, y) of the RGB565 render target, as drawing that part
 * would, but as row copies without a draw's setup: for a caller putting
 * many tiles, as the tower background does on each colour change. Returns
 * false, drawing nothing, where the draw would do something else. */
bool POCKET_CopyOpaqueTile(SDL_Renderer *renderer, SDL_Texture *sheet, int sx, int sy, int x, int y, int w, int h)
{
    if (!check_renderer(renderer) || !check_texture(sheet))
    {
        return false;
    }
    Texture *src = texture_data(sheet);
    Texture *dst = current_target(renderer);
    if (src == dst || dst->pixels565 == NULL || src->clear_pending
        || texels565(src) == NULL || !src->opaque
        || (src->mod_r & src->mod_g & src->mod_b & src->mod_a) != 255
        || (src->blend != SDL_BLENDMODE_NONE && src->blend != SDL_BLENDMODE_BLEND)
        || sx < 0 || sy < 0 || w <= 0 || h <= 0
        || sx + w > src->texture.w || sy + h > src->texture.h)
    {
        return false;
    }
    COUNT_CALL();
    const SDL_Rect wanted = {x, y, w, h};
    const SDL_Rect bounds = {0, 0, dst->texture.w, dst->texture.h};
    SDL_Rect clip;
    if (!POCKET_IntersectRect(&wanted, &bounds, &clip))
    {
        return true;
    }
    PART_SAMPLE_BEGIN();
    if (clip.w == dst->texture.w && clip.h == dst->texture.h)
    {
        begin_change(dst, true);
    }
    else
    {
        begin_rows_change(dst, clip.y, clip.h);
    }
    const Uint16 *pixels = texels565(src);
    const int pitch = src->alias_of != NULL ? src->alias_of->pitch565 : src->pitch565;
    for (int row = clip.y; row < clip.y + clip.h; row++)
    {
        const Uint16 *s = pixels + shown_row(src, sy + row - y) * pitch + sx + (clip.x - x);
        SDL_memcpy(dst->pixels565 + kept_row(dst, row) * dst->pitch565 + clip.x, s, (size_t) clip.w * 2);
    }
    PART_END(1);
    return true;
}

/* For a caller that is about to write every pixel of a texture it has just
 * cleared: the held back clear need not be written. */
void POCKET_SkipPendingClear(SDL_Texture *texture)
{
    if (check_texture(texture))
    {
        texture_data(texture)->clear_pending = false;
    }
}
