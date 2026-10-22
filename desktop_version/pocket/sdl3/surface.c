/* SDL3 pixel formats and surfaces on openfpgaOS.
 *
 * The game keeps surfaces for image loading, pixel collision, screenshots
 * and the bad signal filter, in the 32-bit formats and RGB24.  Pixels are
 * read and written through the format masks, so every blit converts the
 * same way SDL does; none of this runs per frame except the filter. */
#include <SDL3/SDL.h>

#include "render_internal.h"

#define DETAILS(fmt, bpp, bytes, r, g, b, a, rbits, gbits, bbits, abits, rs, gs, bs, as) \
    { fmt, bpp, bytes, {0, 0}, r, g, b, a, rbits, gbits, bbits, abits, rs, gs, bs, as }

static const SDL_PixelFormatDetails formats[] = {
    DETAILS(SDL_PIXELFORMAT_ARGB8888, 32, 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,
            8, 8, 8, 8, 16, 8, 0, 24),
    DETAILS(SDL_PIXELFORMAT_XRGB8888, 24, 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0,
            8, 8, 8, 0, 16, 8, 0, 0),
    DETAILS(SDL_PIXELFORMAT_ABGR8888, 32, 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000,
            8, 8, 8, 8, 0, 8, 16, 24),
    DETAILS(SDL_PIXELFORMAT_RGBA8888, 32, 4, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF,
            8, 8, 8, 8, 24, 16, 8, 0),
    /* Byte order R, G, B, read as a little-endian 24-bit value. */
    DETAILS(SDL_PIXELFORMAT_RGB24, 24, 3, 0x000000FF, 0x0000FF00, 0x00FF0000, 0,
            8, 8, 8, 0, 0, 8, 16, 0),
};

#undef DETAILS

const SDL_PixelFormatDetails *SDL_GetPixelFormatDetails(SDL_PixelFormat format)
{
    for (size_t i = 0; i < SDL_arraysize(formats); i++)
    {
        if (formats[i].format == format)
        {
            return &formats[i];
        }
    }
    SDL_SetError("Unsupported pixel format 0x%08x", (unsigned) format);
    return NULL;
}

SDL_PixelFormat SDL_GetPixelFormatForMasks(int bpp, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    if (Rmask == 0 && Gmask == 0 && Bmask == 0 && Amask == 0)
    {
        switch (bpp)
        {
        case 24:
            return SDL_PIXELFORMAT_RGB24;
        case 32:
            return SDL_PIXELFORMAT_XRGB8888;
        default:
            return SDL_PIXELFORMAT_UNKNOWN;
        }
    }
    for (size_t i = 0; i < SDL_arraysize(formats); i++)
    {
        const SDL_PixelFormatDetails *f = &formats[i];
        if (f->bits_per_pixel == bpp && f->Rmask == Rmask && f->Gmask == Gmask
            && f->Bmask == Bmask && f->Amask == Amask)
        {
            return f->format;
        }
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}

Uint32 POCKET_MapRGBA(const SDL_PixelFormatDetails *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    return ((Uint32) r << f->Rshift) | ((Uint32) g << f->Gshift)
        | ((Uint32) b << f->Bshift) | (((Uint32) a << f->Ashift) & f->Amask);
}

void POCKET_GetRGBA(Uint32 pixel, const SDL_PixelFormatDetails *f, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    *r = (Uint8) ((pixel & f->Rmask) >> f->Rshift);
    *g = (Uint8) ((pixel & f->Gmask) >> f->Gshift);
    *b = (Uint8) ((pixel & f->Bmask) >> f->Bshift);
    *a = f->Amask ? (Uint8) ((pixel & f->Amask) >> f->Ashift) : 255;
}

void SDL_GetRGB(Uint32 pixelvalue, const SDL_PixelFormatDetails *format, const SDL_Palette *palette, Uint8 *r, Uint8 *g, Uint8 *b)
{
    Uint8 a;
    (void) palette;
    POCKET_GetRGBA(pixelvalue, format, r, g, b, &a);
}

void SDL_GetRGBA(Uint32 pixelvalue, const SDL_PixelFormatDetails *format, const SDL_Palette *palette, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    (void) palette;
    POCKET_GetRGBA(pixelvalue, format, r, g, b, a);
}

/* The public struct comes first so an SDL_Surface pointer is one of these. */
typedef struct
{
    SDL_Surface surface;
    SDL_BlendMode blend;
    bool owns_pixels;
} Surface;

static Surface *surface_data(SDL_Surface *surface)
{
    return (Surface *) surface;
}

static SDL_Surface *new_surface(int width, int height, SDL_PixelFormat format, void *pixels, int pitch)
{
    const SDL_PixelFormatDetails *f = SDL_GetPixelFormatDetails(format);
    if (f == NULL)
    {
        return NULL;
    }
    if (width < 0 || height < 0)
    {
        SDL_SetError("Invalid surface size %dx%d", width, height);
        return NULL;
    }
    Surface *s = (Surface *) SDL_calloc(1, sizeof(*s));
    if (s == NULL)
    {
        SDL_SetError("Out of memory");
        return NULL;
    }
    s->surface.format = format;
    s->surface.w = width;
    s->surface.h = height;
    s->surface.refcount = 1;
    s->blend = f->Amask ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    if (pixels != NULL)
    {
        s->surface.pixels = pixels;
        s->surface.pitch = pitch;
        return &s->surface;
    }
    /* Rows are 4-byte aligned, as in SDL. */
    s->surface.pitch = (width * f->bytes_per_pixel + 3) & ~3;
    s->surface.pixels = SDL_calloc(1, (size_t) s->surface.pitch * (height ? height : 1));
    if (s->surface.pixels == NULL)
    {
        SDL_free(s);
        SDL_SetError("Out of memory");
        return NULL;
    }
    s->owns_pixels = true;
    return &s->surface;
}

SDL_Surface *SDL_CreateSurface(int width, int height, SDL_PixelFormat format)
{
    return new_surface(width, height, format, NULL, 0);
}

SDL_Surface *SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void *pixels, int pitch)
{
    if (pixels == NULL)
    {
        return new_surface(width, height, format, NULL, 0);
    }
    return new_surface(width, height, format, pixels, pitch);
}

void SDL_DestroySurface(SDL_Surface *surface)
{
    if (surface == NULL || --surface->refcount > 0)
    {
        return;
    }
    if (surface_data(surface)->owns_pixels)
    {
        SDL_free(surface->pixels);
    }
    SDL_free(surface);
}

bool SDL_LockSurface(SDL_Surface *surface)
{
    if (surface == NULL)
    {
        return SDL_SetError("Parameter 'surface' is invalid");
    }
    return true;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
    (void) surface;
}

SDL_Palette *SDL_GetSurfacePalette(SDL_Surface *surface)
{
    (void) surface;
    return NULL;
}

bool SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode)
{
    if (surface == NULL)
    {
        return SDL_SetError("Parameter 'surface' is invalid");
    }
    surface_data(surface)->blend = blendMode;
    return true;
}

bool SDL_GetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode *blendMode)
{
    if (surface == NULL)
    {
        return SDL_SetError("Parameter 'surface' is invalid");
    }
    if (blendMode != NULL)
    {
        *blendMode = surface_data(surface)->blend;
    }
    return true;
}

Uint32 SDL_MapSurfaceRGB(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapSurfaceRGBA(surface, r, g, b, 255);
}

Uint32 SDL_MapSurfaceRGBA(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    const SDL_PixelFormatDetails *f = SDL_GetPixelFormatDetails(surface->format);
    return f ? POCKET_MapRGBA(f, r, g, b, a) : 0;
}

static Uint32 read_pixel(const Uint8 *p, int bytes)
{
    if (bytes == 4)
    {
        return *(const Uint32 *) p;
    }
    return (Uint32) p[0] | ((Uint32) p[1] << 8) | ((Uint32) p[2] << 16);
}

static void write_pixel(Uint8 *p, int bytes, Uint32 pixel)
{
    if (bytes == 4)
    {
        *(Uint32 *) p = pixel;
        return;
    }
    p[0] = (Uint8) pixel;
    p[1] = (Uint8) (pixel >> 8);
    p[2] = (Uint8) (pixel >> 16);
}

/* Copies (or blends) a w by h block, with nearest scaling when the
 * rectangles differ in size.  Both rectangles are already clipped. */
static void blit(SDL_Surface *src, const SDL_Rect *sr, SDL_Surface *dst, const SDL_Rect *dr)
{
    const SDL_PixelFormatDetails *sf = SDL_GetPixelFormatDetails(src->format);
    const SDL_PixelFormatDetails *df = SDL_GetPixelFormatDetails(dst->format);
    const SDL_BlendMode blend = surface_data(src)->blend;
    /* SDL's blits fill the unused byte of XRGB8888 with ones. */
    const Uint32 pad = df->bytes_per_pixel == 4 && df->Amask == 0
        ? ~(df->Rmask | df->Gmask | df->Bmask) : 0;

    /* Between surfaces of one format, an unblended blit copies the pixels
     * as they are. */
    const bool raw = sf == df && (blend == SDL_BLENDMODE_NONE || sf->Amask == 0);

    for (int y = 0; y < dr->h; y++)
    {
        const int sy = sr->y + POCKET_NearestStep(y, sr->h, dr->h);
        const Uint8 *srow = (const Uint8 *) src->pixels + (size_t) sy * src->pitch;
        Uint8 *drow = (Uint8 *) dst->pixels + (size_t) (dr->y + y) * dst->pitch;
        for (int x = 0; x < dr->w; x++)
        {
            const int sx = sr->x + POCKET_NearestStep(x, sr->w, dr->w);
            Uint8 *dp = drow + (size_t) (dr->x + x) * df->bytes_per_pixel;
            if (raw)
            {
                SDL_memcpy(dp, srow + (size_t) sx * sf->bytes_per_pixel, sf->bytes_per_pixel);
                continue;
            }
            Uint8 r, g, b, a;
            POCKET_GetRGBA(read_pixel(srow + (size_t) sx * sf->bytes_per_pixel, sf->bytes_per_pixel), sf, &r, &g, &b, &a);
            if (blend == SDL_BLENDMODE_BLEND && a != 255)
            {
                Uint8 dr8, dg, db, da;
                POCKET_GetRGBA(read_pixel(dp, df->bytes_per_pixel), df, &dr8, &dg, &db, &da);
                r = POCKET_Blend(r, dr8, a);
                g = POCKET_Blend(g, dg, a);
                b = POCKET_Blend(b, db, a);
                a = (Uint8) (a + POCKET_Div255(da * (255 - a)));
            }
            write_pixel(dp, df->bytes_per_pixel, POCKET_MapRGBA(df, r, g, b, a) | pad);
        }
    }
}

static bool blit_checked(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, const SDL_Rect *dstrect, bool scaled)
{
    if (src == NULL || dst == NULL)
    {
        return SDL_SetError("Parameter 'surface' is invalid");
    }
    if (SDL_GetPixelFormatDetails(src->format) == NULL || SDL_GetPixelFormatDetails(dst->format) == NULL)
    {
        return false;
    }

    SDL_Rect sr = {0, 0, src->w, src->h};
    if (srcrect != NULL && !POCKET_IntersectRect(srcrect, &sr, &sr))
    {
        return true;
    }

    SDL_Rect dr;
    if (dstrect == NULL)
    {
        dr.x = 0;
        dr.y = 0;
        dr.w = scaled ? dst->w : sr.w;
        dr.h = scaled ? dst->h : sr.h;
    }
    else
    {
        dr = *dstrect;
        if (!scaled)
        {
            dr.w = sr.w;
            dr.h = sr.h;
        }
    }
    if (sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0)
    {
        return true;
    }

    /* Clip the destination, moving the source by the same scaled amount. */
    const SDL_Rect bounds = {0, 0, dst->w, dst->h};
    SDL_Rect clipped;
    if (!POCKET_IntersectRect(&dr, &bounds, &clipped))
    {
        return true;
    }
    SDL_Rect csr;
    csr.x = sr.x + (int) ((Sint64) (clipped.x - dr.x) * sr.w / dr.w);
    csr.y = sr.y + (int) ((Sint64) (clipped.y - dr.y) * sr.h / dr.h);
    csr.w = (int) ((Sint64) clipped.w * sr.w / dr.w);
    csr.h = (int) ((Sint64) clipped.h * sr.h / dr.h);
    if (csr.w <= 0 || csr.h <= 0)
    {
        return true;
    }

    blit(src, &csr, dst, &clipped);
    return true;
}

bool SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, const SDL_Rect *dstrect)
{
    return blit_checked(src, srcrect, dst, dstrect, false);
}

bool SDL_BlitSurfaceScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, const SDL_Rect *dstrect, SDL_ScaleMode scaleMode)
{
    (void) scaleMode;
    return blit_checked(src, srcrect, dst, dstrect, true);
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *surface, SDL_PixelFormat format)
{
    if (surface == NULL)
    {
        SDL_SetError("Parameter 'surface' is invalid");
        return NULL;
    }
    SDL_Surface *converted = SDL_CreateSurface(surface->w, surface->h, format);
    if (converted == NULL)
    {
        return NULL;
    }
    /* A conversion copies the pixels as they are, alpha included. */
    const SDL_BlendMode blend = surface_data(surface)->blend;
    surface_data(surface)->blend = SDL_BLENDMODE_NONE;
    const bool ok = SDL_BlitSurface(surface, NULL, converted, NULL);
    surface_data(surface)->blend = blend;
    if (!ok)
    {
        SDL_DestroySurface(converted);
        return NULL;
    }
    surface_data(converted)->blend = blend;
    return converted;
}

bool POCKET_SurfaceToARGB(SDL_Surface *surface, Uint32 *out)
{
    const SDL_PixelFormatDetails *f = SDL_GetPixelFormatDetails(surface->format);
    if (f == NULL)
    {
        return false;
    }
    for (int y = 0; y < surface->h; y++)
    {
        const Uint8 *row = (const Uint8 *) surface->pixels + (size_t) y * surface->pitch;
        for (int x = 0; x < surface->w; x++)
        {
            Uint8 r, g, b, a;
            POCKET_GetRGBA(read_pixel(row + (size_t) x * f->bytes_per_pixel, f->bytes_per_pixel), f, &r, &g, &b, &a);
            *out++ = ((Uint32) a << 24) | ((Uint32) r << 16) | ((Uint32) g << 8) | b;
        }
    }
    return true;
}
