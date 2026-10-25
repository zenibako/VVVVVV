/* SDL3 standard-library wrappers on musl, plus errors, hints and
 * properties.  The game calls these through SDL_stdinc.h. */
#include <SDL3/SDL.h>

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* SDL_stdinc.h maps SDL_memset to memset, but Music.cpp calls the SDL
 * symbol directly, so it needs a real definition. */
#undef SDL_memset

void *SDL_malloc(size_t size) { return malloc(size == 0 ? 1 : size); }
void *SDL_calloc(size_t nmemb, size_t size) { return calloc(nmemb == 0 ? 1 : nmemb, size == 0 ? 1 : size); }
void *SDL_realloc(void *mem, size_t size) { return realloc(mem, size == 0 ? 1 : size); }
void SDL_free(void *mem) { free(mem); }

void *SDL_memset(void *dst, int c, size_t len) { return memset(dst, c, len); }
int SDL_memcmp(const void *s1, const void *s2, size_t len) { return memcmp(s1, s2, len); }

size_t SDL_strlen(const char *str) { return strlen(str); }
int SDL_strcmp(const char *str1, const char *str2) { return strcmp(str1, str2); }
int SDL_strncmp(const char *str1, const char *str2, size_t maxlen) { return strncmp(str1, str2, maxlen); }
int SDL_strcasecmp(const char *str1, const char *str2) { return strcasecmp(str1, str2); }
char *SDL_strchr(const char *str, int c) { return strchr(str, c); }
char *SDL_strrchr(const char *str, int c) { return strrchr(str, c); }
char *SDL_strstr(const char *haystack, const char *needle) { return strstr(haystack, needle); }
char *SDL_strdup(const char *str) { return strdup(str); }

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen)
{
    const size_t srclen = strlen(src);
    if (maxlen > 0)
    {
        const size_t n = srclen < maxlen - 1 ? srclen : maxlen - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return srclen;
}

size_t SDL_strlcat(char *dst, const char *src, size_t maxlen)
{
    const size_t dstlen = strnlen(dst, maxlen);
    if (dstlen == maxlen)
    {
        return dstlen + strlen(src);
    }
    return dstlen + SDL_strlcpy(dst + dstlen, src, maxlen - dstlen);
}

long SDL_strtol(const char *str, char **endp, int base) { return strtol(str, endp, base); }
long long SDL_strtoll(const char *str, char **endp, int base) { return strtoll(str, endp, base); }
int SDL_atoi(const char *str) { return atoi(str); }
double SDL_atof(const char *str) { return atof(str); }

int SDL_tolower(int x) { return tolower(x); }
int SDL_isdigit(int x) { return isdigit(x); }
int SDL_isxdigit(int x) { return isxdigit(x); }
int SDL_abs(int x) { return abs(x); }

double SDL_ceil(double x) { return ceil(x); }
float SDL_ceilf(float x) { return ceilf(x); }
double SDL_cos(double x) { return cos(x); }
double SDL_exp(double x) { return exp(x); }
float SDL_fabsf(float x) { return fabsf(x); }
double SDL_floor(double x) { return floor(x); }
float SDL_fmodf(float x, float y) { return fmodf(x, y); }
double SDL_log(double x) { return log(x); }
double SDL_pow(double x, double y) { return pow(x, y); }
double SDL_scalbn(double x, int n) { return scalbn(x, n); }
double SDL_sin(double x) { return sin(x); }

int SDL_snprintf(char *text, size_t maxlen, const char *fmt, ...)
{
    va_list ap;
    int result;
    va_start(ap, fmt);
    result = vsnprintf(text, maxlen, fmt, ap);
    va_end(ap);
    return result;
}

int SDL_sscanf(const char *text, const char *fmt, ...)
{
    va_list ap;
    int result;
    va_start(ap, fmt);
    result = vsscanf(text, fmt, ap);
    va_end(ap);
    return result;
}

void SDL_qsort(void *base, size_t nmemb, size_t size, SDL_CompareCallback compare)
{
    qsort(base, nmemb, size, compare);
}

/* Errors: one static buffer, as the game is single-threaded. */
static char error_text[256];

bool SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_text, sizeof(error_text), fmt, ap);
    va_end(ap);
    return false;
}

const char *SDL_GetError(void) { return error_text; }

bool SDL_ClearError(void)
{
    error_text[0] = '\0';
    return true;
}

/* Hints only steer desktop drivers; none apply on the Pocket. */
bool SDL_SetHintWithPriority(const char *name, const char *value, SDL_HintPriority priority)
{
    (void) name;
    (void) value;
    (void) priority;
    return true;
}

bool SDL_GetHintBoolean(const char *name, bool default_value)
{
    (void) name;
    return default_value;
}
